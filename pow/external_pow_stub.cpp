#include "external_pow.h"
#include "utility/helpers.h"
#include <mutex>
#include <condition_variable>
#include <atomic>
#include "utility/logger.h"

namespace beam {

class ExternalPOWStub : public IExternalPOW {
public:
    ExternalPOWStub() : _seed(0), _changed(false), _stop(false) {
        ECC::GenRandom(&_seed, 8);
        _thread.start(BIND_THIS_MEMFN(thread_func));
    }

    ~ExternalPOWStub() override {
        stop();
        _thread.join();
        LOG_INFO() << "Done";
    }

private:
    struct Job {
        std::string jobID;
        Merkle::Hash input;
        Block::PoW pow;
        BlockFound callback;
    };

    void new_job(
        const std::string& jobID,
        const Merkle::Hash& input,
        const Block::PoW& pow,
        const BlockFound& callback,
        const CancelCallback& cancelCallback
    ) override
    {
        {
            std::lock_guard<std::mutex> lk(_mutex);
            if (_currentJob.input == input) {
                return;
            }
            _currentJob.jobID = jobID;
            _currentJob.input = input;
            _currentJob.pow = pow;
            _currentJob.callback = callback;
            _changed = true;
        }
        _cond.notify_one();
    }

    void get_last_found_block(std::string& jobID, Block::PoW& pow) override {
        std::lock_guard<std::mutex> lk(_mutex);
        jobID = _lastFoundBlockID;
        pow = _lastFoundBlock;
    }

    void stop() override {
        {
            std::lock_guard<std::mutex> lk(_mutex);
            _stop = true;
            _changed = true;
        }
        _cond.notify_one();
    }

    void stop_current() override {
        // TODO do we need it?
    }

    bool get_new_job(Job& job) {
        std::unique_lock<std::mutex> lk(_mutex);
        _cond.wait(lk, [this]() { return _changed.load(); });

        if (_stop) return false;

        _changed = false;
        job = _currentJob;
        job.pow.m_Nonce = ++_seed;
        return true;
    }

    void thread_func() {
#if defined (BEAM_USE_GPU)
        auto SolveFn = &Block::PoW::SolveGPU;
#else
        auto SolveFn = &Block::PoW::Solve;
#endif

        Job job;
        Merkle::Hash hv;

        auto cancelFn = [this, &job](bool)->bool {
            if (_changed.load()) {
                LOG_INFO() << "job id=" << job.jobID << " cancelled";
                return true;
            }
            return false;
        };

        while (get_new_job(job)) {
            LOG_INFO() << "solving job id=" << job.jobID
                       << " with nonce=" << job.pow.m_Nonce << " and difficulty=" << job.pow.m_Difficulty;

            if ( (job.pow.*SolveFn) (job.input.m_pData, Merkle::Hash::nBytes, cancelFn)) {
                {
                    std::lock_guard<std::mutex> lk(_mutex);
                    _lastFoundBlock = job.pow;
                    _lastFoundBlockID = job.jobID;
                }
                job.callback();
            }
        }
    }

    Job _currentJob;
    std::string _lastFoundBlockID;
    Block::PoW _lastFoundBlock;
    uint64_t _seed;
    std::atomic<bool> _changed;
    bool _stop;
    Thread _thread;
    std::mutex _mutex;
    std::condition_variable _cond;
};

std::unique_ptr<IExternalPOW> IExternalPOW::create_local_solver() {
    return std::make_unique<ExternalPOWStub>();
}

} //namespace
