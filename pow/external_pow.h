#pragma once
#include "core/block_crypt.h"

namespace beam {

class IExternalPOW {
public:
    using BlockFound = std::function<void(const Block::PoW& pow)>;
    using CancelCallback = std::function<bool()>;

    // creates stratum server
    static std::unique_ptr<IExternalPOW> create(uint16_t port);

    // creates local solver (stub)
    static std::unique_ptr<IExternalPOW> create_local_solver();

    virtual ~IExternalPOW() = default;
    virtual void new_job(
        const Merkle::Hash& input,
        const Block::PoW& pow,
        const BlockFound& callback,
        const CancelCallback& cancelCallback) = 0;
    virtual void stop_current() = 0;
    virtual void stop() = 0;
};

} //namespace