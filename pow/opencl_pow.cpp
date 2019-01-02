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
        WorkProvider(SolutionCallback&& solutionCallback)
            : _solutionCallback(move(solutionCallback))
            , _job{nullptr}
            , _stopped(false)
        {
            ECC::GenRandom(&_nonce, 8);
        }
        virtual ~WorkProvider()
        {

        }

        void feedJob(Job* job)
        {
            unique_lock<mutex> guard(_mutex);
            _job = job;
        }

        void stop()
        {
            unique_lock<mutex> guard(_mutex);
            _stopped = true;
        }

        bool hasWork() override
        {
            return _job;
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
        SolutionCallback _solutionCallback;
        Job* _job;
        bool _stopped;
        atomic<uint64_t> _nonce;
        mutable mutex _mutex;
    };
}

namespace beam {

    class OpenCLMiner : public IExternalPOW
    {
    public:
        OpenCLMiner() 
            : _changed(false)
            , _stop(false)
            , _workProvider(BIND_THIS_MEMFN(on_solution))
            , _solutionFound(false)
        {
            _thread.start(BIND_THIS_MEMFN(thread_func));
           // _minerThread.start(BIND_THIS_MEMFN(run_miner));
        }

        ~OpenCLMiner() override 
        {
            stop();
            _thread.join();
            _minerThread.join();
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
            _ClHost.stopMining();
            _cond.notify_one();
        }

        void stop_current() override
        {
            // TODO do we need it?
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

            // auto cancelFn = [this, &job](bool)->bool {
            //     if (_changed.load()) {
            //         LOG_INFO() << "job id=" << job.jobID << " cancelled";
            //         return true;
            //     }
            //     return false;
            // };

            while (true)
            {
                
                {
                    unique_lock<mutex> lock(_mutex);

                    _cond.wait(lock, [this] { return _solutionFound || _stop; });
                    if (_stop)
                    {
                        return;
                    }

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
        }

        void run_miner()
        {
            // TODO: we should use onle selected video cards
            vector<int32_t> devices{ -1 };
            bool cpuMine = false;

            LOG_DEBUG() << "runOpenclMiner()";

            
            
            LOG_INFO() << "Setup OpenCL devices:";
            LOG_INFO() << "=====================";

            _ClHost.setup(&_workProvider, devices, cpuMine);

            LOG_INFO() << "Waiting for work:";
            LOG_INFO() << "==============================";

            while (!_workProvider.hasWork())
            {
                this_thread::sleep_for(chrono::milliseconds(200));
            }

            LOG_INFO() << "Start mining:";
            LOG_INFO() << "=============";

            _ClHost.startMining();
        }

        void on_solution(Job* job)
        {
            {
                unique_lock<mutex> lock(_mutex);
                _solutionFound = true;
            }
            _cond.notify_one();
        }

    private:

        Job _currentJob;
        string _lastFoundBlockID;
        Block::PoW _lastFoundBlock;
        atomic<bool> _changed;
        bool _stop;
        Thread _thread;
        Thread _minerThread;
        mutex _mutex;
        condition_variable _cond;
        WorkProvider _workProvider;
        beamMiner::clHost _ClHost;
        bool _solutionFound;
    };

    unique_ptr<IExternalPOW> IExternalPOW::create_opencl_solver() {
        return make_unique<OpenCLMiner>();
    }

} //namespace
