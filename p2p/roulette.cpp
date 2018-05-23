#include "roulette.h"
#include <assert.h>

namespace beam {

Roulette::Roulette(uint32_t maxItemWeight) :
    _maxItemWeight(maxItemWeight)
{
    assert(_maxItemWeight > 0);

    _buckets.resize(_maxItemWeight + 1);
    std::random_device _rd;
    _rdGen.seed(_rd());
}

void Roulette::push(Roulette::ID id, uint32_t weight) {
    if (weight == 0 || id == INVALID_ID) return;
    if (weight > _maxItemWeight) weight = _maxItemWeight;
    _buckets[weight].items.push_back(id);

    // update total weight and partial weights
    _totalWeight += weight;
    for (uint32_t i=weight+1; i<=_maxItemWeight; ++i) {
        _buckets[i].weightBoundary += weight;
    }

    _totalItems++;
}

Roulette::ID Roulette::pull() {
    ID id = INVALID_ID;

    if (_totalWeight == 0) return id;

    // choose random bucket according weighted distribution
    size_t x = rnd(0, _totalWeight-1);
    size_t i = find_bucket(x, 1, _maxItemWeight);
    Bucket& bucket = _buckets[i];

    assert(!bucket.items.empty());

    // choose random item within the bucket
    size_t index = (x - bucket.weightBoundary) / i;
    assert(index < bucket.items.size());

    id = bucket.items[i];

    size_t s = bucket.items.size();
    if (i < s-1) {
        bucket.items[i] = bucket.items[s-1];
    }
    bucket.items.resize(s-1);

    // update total weight and partial weights
    _totalWeight -= i;
    for (; i<=_maxItemWeight; ++i) {
        _buckets[i].weightBoundary -= i;
    }

    _totalItems--;

    return id;
}

// returns random number in range [a,b]
uint32_t Roulette::rnd(uint32_t a, uint32_t b) {
    if (a == b) return a;
    std::uniform_int_distribution<uint32_t> dis(a, b);
    return dis(_rdGen);
}

// returns index of nonempty bucket with weight range which contains x
uint32_t Roulette::find_bucket(uint32_t x, uint32_t i, uint32_t j) {
    assert(i > 0 && j > 0 && i <= _maxItemWeight && j <= _maxItemWeight);
    if (i == j) {
        while (_buckets[i].items.empty()) --i;
        return i;
    }
    uint32_t m = (i+j)/2;
    if (x < _buckets[m].weightBoundary) {
        return find_bucket(x, 1, m-1);
    }
    return find_bucket(x, m, j);
}

} //namespace
