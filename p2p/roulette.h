#pragma once
#include <stdint.h>
#include <vector>
#include <random>

namespace beam {

/// Pulls random peer IDs to connect to.
/// Probabilities are weighted, P(weight=i) == i*P(1).
/// TODO Ineffective algorithms, to be rewritten as soon as real data sizes are known
class Roulette {
public:
    /// At the moment
    using ID = uint64_t;

    /// ID==0 considered invalid or inexistent here
    static constexpr ID INVALID_ID=0;

    /// Creates a roulette with limited max weight
    explicit Roulette(uint32_t maxItemWeight);

    /// Pushes an element into buckets
    void push(ID id, uint32_t weight);

    /// Pulls one random element
    ID pull();

private:
    struct Bucket {
        std::vector<ID> items;
        uint32_t weightBoundary=0;
    };
    using Buckets = std::vector<Bucket>;

    // returns random number in range [a,b]
    uint32_t rnd(uint32_t a, uint32_t b);

    // returns index of nonempty bucket with weight range which contains x
    uint32_t find_bucket(uint32_t x, uint32_t i, uint32_t j);

    const uint32_t _maxItemWeight;
    uint32_t _totalWeight=0;
    uint32_t _totalItems=0;
    Buckets _buckets;
    std::mt19937 _rdGen;
};

} //namespace
