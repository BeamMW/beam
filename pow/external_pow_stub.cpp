#include "external_pow.h"
#include "utility/helpers.h"
#include <mutex>
#include <condition_variable>
#include <atomic>

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
    }

private:
    struct Job {
        Merkle::Hash input;
        Block::PoW pow;
        BlockFound callback;
    };

    void new_job(const Merkle::Hash& input, const Block::PoW& pow, const BlockFound& callback) override {
        {
            std::lock_guard<std::mutex> lk(_mutex);
            if (_currentJob.input == input) {
                return;
            }
            _currentJob.input = input;
            _currentJob.pow = pow;
            _currentJob.callback = callback;
            _changed = true;
        }
        _cond.notify_one();
    }

    void stop() override {
        {
            std::lock_guard<std::mutex> lk(_mutex);
            _stop = true;
            _changed = true;
        }
        _cond.notify_one();
    }

    bool get_new_job(Job& job) {
        std::unique_lock<std::mutex> lk(_mutex);
        _cond.wait(lk, [this]() { return _changed.load(); });

        if (_stop) return false;

        job = _currentJob;

        ECC::Hash::Value hv; // pick pseudo-random initial nonce for mining.
        ECC::Hash::Processor() << ++_seed >> hv;
        job.pow.m_Nonce = hv;

        return true;
    }

    void thread_func() {
        Job job;
        Merkle::Hash hv;
        while (get_new_job(job)) {
            if (job.pow.Solve(job.input.m_pData, Merkle::Hash::nBytes, [this](bool)->bool { return !_changed.load(); })) {
                job.callback(job.pow);
            }
        }
    }

    Job _currentJob;
    uint64_t _seed;
    std::atomic<bool> _changed;
    bool _stop;
    Thread _thread;
    std::mutex _mutex;
    std::condition_variable _cond;
};

// TODO stub
std::unique_ptr<IExternalPOW> IExternalPOW::create(uint16_t /*port*/) {
    return create_local_solver();
}

std::unique_ptr<IExternalPOW> IExternalPOW::create_local_solver() {
    return std::make_unique<ExternalPOWStub>();
}

} //namespace
