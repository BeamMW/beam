// Copyright 2018 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "external_pow.h"
#include "utility/helpers.h"
#include <mutex>
#include <condition_variable>
#include <atomic>
#include "utility/logger.h"
#include <vector>
#include "3rdparty/opencl-miner/clHost.h"
#include "3rdparty/crypto/equihash.h"
#include <random>

using namespace std;

namespace
{
    struct Job 
    {
        string jobID;
        beam::Merkle::Hash input;
        beam::Block::PoW pow;
        beam::IExternalPOW::BlockFound callback;
    };

    using SolutionCallback = function<void(Job*)>;

    class WorkProvider : public beamMiner::minerBridge
    {
    public:
        WorkProvider(beam::IExternalPOW& externalPow, SolutionCallback&& solutionCallback)
            : //_isSolutionFound(false)
            //, 
            
            _externalPow(externalPow)
            , _solutionCallback(move(solutionCallback))
            , _job{nullptr}
           // , _host(host)
        {
         //   _input.reserve(32);
            random_device rd;
            default_random_engine generator(rd());
            uniform_int_distribution<uint64_t> distribution(0, 0xFFFFFFFFFFFFFFFF);

            // We pick a random start nonce
            _nonce = distribution(generator);
        }
        virtual ~WorkProvider()
        {

        }

 /*       void setWork(const void* input, uint32_t sizeInput, const EquihashGpu::IsValid& valid, const EquihashGpu::Cancel& cancel)
        {
            LOG_DEBUG() << "-=[GPU Miner]=- Set new GPU miner work";
            unique_lock<mutex> guard(_mutex);
            auto *p = static_cast<const uint8_t*>(input);
            _input.assign(p, p + sizeInput);

            _valid = valid;
            _cancel = cancel;
            ++_workID;
            _isSolutionFound = false;
        }*/

        //bool isSolutionFound() const
        //{
        //    unique_lock<mutex> guard(_mutex);
        //    return _isSolutionFound;
        //}

        void feedJob(Job* job)
        {
            unique_lock<mutex> guard(_mutex);
            _job = job;
        }

    private:

        bool hasWork() override
        {
            return _job;// !isSolutionFound();
        }

        void getWork(int64_t* workOut, uint64_t* nonceOut, uint8_t* dataOut) override
        {
            unique_lock<mutex> guard(_mutex);
            *workOut = stoll(_job->jobID);
            *nonceOut = _nonce.fetch_add(1);
            memcpy(dataOut, _job->input.m_pData, _job->input.nBytes);
        }

        void handleSolution(int64_t &workId, uint64_t &nonce, vector<uint32_t> &indices) override
        {
            {
                unique_lock<mutex> guard(_mutex);
                if (stoll(_job->jobID) != workId)
                {
                    return;
                }
                auto compressed = GetMinimalFromIndices(indices, 25);
                copy(compressed.begin(), compressed.end(), _job->pow.m_Indices.begin());
                beam::Block::PoW::NonceType t((const uint8_t*)&nonce);
                _job->pow.m_Nonce = t;
            }
            _solutionCallback(_job);

        }

    private:
        atomic<uint64_t> _nonce;
          mutable mutex _mutex;
      //  beam::ByteBuffer _input;
        Job* _job;
     //   EquihashGpu::IsValid _valid;
    //    EquihashGpu::Cancel _cancel;
        bool _isSolutionFound;
      //  vector<uint8_t> _compressedIndices;
     //   beam::Block::PoW::NonceType _foundNonce;
        beam::IExternalPOW& _externalPow;
     //   beamMiner::clHost& _host;
        SolutionCallback _solutionCallback;
    };
}

namespace beam {

    class OpenCLMiner : public IExternalPOW
                     // , public beamMiner::minerBridge
    {
    public:
        OpenCLMiner() 
            : _seed(0)
            , _changed(false)
            , _stop(false)
            , _workProvider(*this, BIND_THIS_MEMFN(on_solution))
            , _solutionFound(false)
        {
            ECC::GenRandom(&_seed, 8);
            _thread.start(BIND_THIS_MEMFN(thread_func));
            _minerThread.start(BIND_THIS_MEMFN(run_miner));
        }

        ~OpenCLMiner() override 
        {
            stop();
            _thread.join();
            LOG_INFO() << "OpenCLMiner is done";
        }

    private:
        void new_job(
            const string& jobID,
            const Merkle::Hash& input,
            const Block::PoW& pow,
            const BlockFound& callback,
            const CancelCallback& cancelCallback
        ) override
        {
            {
                lock_guard<mutex> lk(_mutex);
                if (_currentJob.input == input)
                {
                    return;
                }
                _currentJob.jobID = jobID;
                _currentJob.input = input;
                _currentJob.pow = pow;
                _currentJob.callback = callback;
                _changed = true;
                _workProvider.feedJob(&_currentJob);
            }
            _cond.notify_one();
        }

        void get_last_found_block(string& jobID, Block::PoW& pow) override
        {
            lock_guard<mutex> lk(_mutex);
            jobID = _lastFoundBlockID;
            pow = _lastFoundBlock;
        }

        void stop() override 
        {
            {
                lock_guard<mutex> lk(_mutex);
                _stop = true;
                _changed = true;
            }
            _cond.notify_one();
        }

        void stop_current() override
        {
            // TODO do we need it?
        }

        bool get_new_job(Job& job) 
        {
            unique_lock<mutex> lk(_mutex);
            _cond.wait(lk, [this]() { return _changed.load(); });

            if (_stop) return false;

            _changed = false;
            job = _currentJob;
            job.pow.m_Nonce = ++_seed;
            return true;
        }

        bool TestDifficulty(const uint8_t* pSol, uint32_t nSol, Difficulty d) const
        {
            ECC::Hash::Value hv;
            ECC::Hash::Processor() << Blob(pSol, nSol) >> hv;

            return d.IsTargetReached(hv);
        }

        void thread_func()
        {
            Job job;
            Merkle::Hash hv;

            auto cancelFn = [this, &job](bool)->bool {
                if (_changed.load()) {
                    LOG_INFO() << "job id=" << job.jobID << " cancelled";
                    return true;
                }
                return false;
            };

            while (true)
            {
                
                {
                    unique_lock<mutex> lock(_mutex);
                    _solutionCond.wait(lock, [this] { return _solutionFound; });
                    _solutionFound = false;
                    job = _currentJob;

                    if (!TestDifficulty(&job.pow.m_Indices[0], (uint32_t)job.pow.m_Indices.size(), job.pow.m_Difficulty))
                    {
                        continue;
                    }
                     
                    _lastFoundBlock = job.pow;
                    _lastFoundBlockID = job.jobID;
                }
                job.callback();
            }

            //while (get_new_job(job)) {
            //    LOG_INFO() << "solving job id=" << job.jobID
            //        << " with nonce=" << job.pow.m_Nonce << " and difficulty=" << job.pow.m_Difficulty;
            //    _workProvider.feedJob(&job);
            //    //if ((job.pow.*SolveFn) (job.input.m_pData, Merkle::Hash::nBytes, cancelFn)) {
            //    //    {
            //    //        lock_guard<mutex> lk(_mutex);
            //    //        _lastFoundBlock = job.pow;
            //    //        _lastFoundBlockID = job.jobID;
            //    //    }
            //    //    job.callback();
            //    //}
            //}
        }

        void run_miner()
        {
            vector<int32_t> devices{ 1 };
            bool cpuMine = false;

            LOG_DEBUG() << "runOpenclMiner()";

            beamMiner::clHost myClHost;
            
            beamMiner::minerBridge& myStratum = _workProvider;

            LOG_INFO() << "Setup OpenCL devices:";
            LOG_INFO() << "=====================";

            myClHost.setup(&_workProvider, devices, cpuMine);

            LOG_INFO() << "Waiting for work from stratum:";
            LOG_INFO() << "==============================";

            while (!myStratum.hasWork())
            {
                this_thread::sleep_for(chrono::milliseconds(200));
            }

            LOG_INFO() << "Start mining:";
            LOG_INFO() << "=============";

            myClHost.startMining();

        }

        void on_solution(Job* job)
        {
            {
                unique_lock<mutex> lock(_mutex);
                _solutionFound = true;
            }
            _solutionCond.notify_one();
            /*           beam::Block::PoW::NonceType t((const uint8_t*)&nonce);
           EquihashGpu::IsValid isValid;
           EquihashGpu::Cancel cancel;
           {
               unique_lock<mutex> guard(_mutex);
               if (workId != _workID)
               {
                   return;
               }

               isValid = _valid;
               cancel = _cancel;
               if (_cancel())
               {
                   _host.stopMining();
                   return;
               }

               if (_isSolutionFound)
               {
                   return;
               }
           }

           auto compressed = GetMinimalFromIndices(indices, 25);

           if (isValid(compressed, t))
           {
               unique_lock<mutex> guard(_mutex);
               if (workId == _workID && !_isSolutionFound)
               {
                   _isSolutionFound = true;
                   _host.stopMining();
                   _compressedIndices = compressed;
                   _foundNonce = t;
               }
           }*/
        }

        Job _currentJob;
        string _lastFoundBlockID;
        Block::PoW _lastFoundBlock;
        uint64_t _seed;
        atomic<bool> _changed;
        bool _stop;
        Thread _thread;
        mutex _mutex;
        condition_variable _cond;
        condition_variable _solutionCond;
        WorkProvider _workProvider;
        Thread _minerThread;
        bool _solutionFound;
    };

    unique_ptr<IExternalPOW> IExternalPOW::create_opencl_solver() {
        return make_unique<OpenCLMiner>();
    }

} //namespace
