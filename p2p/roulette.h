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
    explicit Roulette(size_t maxItemWeight);
    
    /// Pushes an element into buckets
    void push(ID id, size_t weight);
    
    /// Pulls one random element
    ID pull();
    
    /// Pulls n random elements
    void pull_n(std::vector<ID>& ids, size_t n);
    
private:
    struct Bucket {
        std::vector<ID> items;
        size_t weightBoundary=0;
    };
    using Buckets = std::vector<Bucket>;
    
    // returns random number in range [a,b]
    size_t rnd(size_t a, size_t b);
    
    // returns index of nonempty bucket with weight range which contains x
    size_t find_bucket(size_t x, size_t i, size_t j);
    
    const size_t _maxItemWeight;
    size_t _totalWeight=0;
    size_t _totalItems=0;
    Buckets _buckets;
    std::mt19937 _rdGen;
};
    
} //namespace
