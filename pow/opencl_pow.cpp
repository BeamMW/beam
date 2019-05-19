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

    using SolutionCallback = function<void(Job&& job)>;

    class WorkProvider : public beamMiner::minerBridge
    {
    public:
        WorkProvider(SolutionCallback&& solutionCallback)
            : _solutionCallback(move(solutionCallback))
            , _stopped(false)
        {
            ECC::GenRandom(&_nonce, 8);
        }
        virtual ~WorkProvider()
        {

        }

        void feedJob(const Job& job)
        {
            unique_lock<mutex> guard(_mutex);
            _input.assign(job.input.m_pData, job.input.m_pData + job.input.nBytes);
            _workID = stoll(job.jobID);
            _difficulty = job.pow.m_Difficulty.m_Packed;
        }

        void stop()
        {
            unique_lock<mutex> guard(_mutex);
            _stopped = true;
        }

        bool hasWork() override
        {
            unique_lock<mutex> guard(_mutex);
            return !_input.empty();
        }

        void getWork(int64_t* workOut, uint64_t* nonceOut, uint8_t* dataOut, uint32_t* difficulty) override
        {
            unique_lock<mutex> guard(_mutex);
            *workOut = _workID;
            *nonceOut = _nonce.fetch_add(1);
            *difficulty = _difficulty;
            copy_n(&_input[0], _input.size(), dataOut);
        }

        void handleSolution(int64_t &workId, uint64_t &nonce, vector<uint32_t> &indices, uint32_t difficulty) override
        {
            Job job;
            auto compressed = GetMinimalFromIndices(indices, 25);
            copy(compressed.begin(), compressed.end(), job.pow.m_Indices.begin());
            beam::Block::PoW::NonceType t((const uint8_t*)&nonce);
            job.pow.m_Nonce = t;
            job.pow.m_Difficulty = beam::Difficulty(difficulty);
            job.jobID = to_string(workId);
            _solutionCallback(move(job));
        }

    private:
        SolutionCallback _solutionCallback;
        bool _stopped;
        vector<uint8_t> _input;
        atomic<uint64_t> _nonce;
        uint32_t _difficulty;
        int64_t _workID = 0;
        mutable mutex _mutex;
    };
}

namespace beam {

    class OpenCLMiner : public IExternalPOW
    {
    public:
        OpenCLMiner(const vector<int32_t>& devices)
            : _changed(false)
            , _stop(false)
            , _workProvider(BIND_THIS_MEMFN(on_solution))
            , _devices(devices)
        {
            _thread.start(BIND_THIS_MEMFN(thread_func));
            _minerThread.start(BIND_THIS_MEMFN(run_miner));
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
            const Height& height,
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
                _workProvider.feedJob(_currentJob);
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
            while (true)
            {
                vector<Job> jobs;
                beam::IExternalPOW::BlockFound callback;
                {
                    unique_lock<mutex> lock(_mutex);

                    _cond.wait(lock, [this] { return !_solvedJobs.empty() || _stop; });
                    if (_stop)
                    {
                        return;
                    }
                    swap(jobs, _solvedJobs);
                    callback = _currentJob.callback;

                }
                for (const auto& job : jobs)
                {
                    if (!TestDifficulty(&job.pow.m_Indices[0], (uint32_t)job.pow.m_Indices.size(), job.pow.m_Difficulty))
                    {
                        continue;
                    }
                    {
                        unique_lock<mutex> lock(_mutex);
                        _lastFoundBlock = job.pow;
                        _lastFoundBlockID = job.jobID;
                    }
                    callback();
                }
            }
        }

        void run_miner()
        {
            bool cpuMine = false;

            LOG_DEBUG() << "runOpenclMiner()";

            
            
            LOG_INFO() << "Setup OpenCL devices:";
            LOG_INFO() << "=====================";

            _ClHost.setup(&_workProvider, _devices, cpuMine);

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

        void on_solution(Job&& job)
        {
            {
                unique_lock<mutex> lock(_mutex);
                _solvedJobs.push_back(move(job));
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
        vector<Job> _solvedJobs;
        const vector<int32_t> _devices;
        
    };

    unique_ptr<IExternalPOW> IExternalPOW::create_opencl_solver(const vector<int32_t>& devices)
    {
        return make_unique<OpenCLMiner>(devices);
    }

} //namespace
