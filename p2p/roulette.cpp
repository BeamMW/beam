#include "roulette.h"
#include "rnd_gen.h"
#include <assert.h>

namespace beam {

Roulette::Roulette(RandomGen& rdGen, uint32_t maxItemWeight) :
    _rdGen(rdGen),
    _maxItemWeight(maxItemWeight)
{
    assert(_maxItemWeight > 0);
    _buckets.resize(_maxItemWeight + 1);
}

void Roulette::push(Roulette::ID id, uint32_t weight) {
    if (weight == 0 || id == INVALID_ID) return;
    if (weight > _maxItemWeight) weight = _maxItemWeight;
    if (weight < _minWeight) _minWeight = weight;
    if (weight > _maxWeight) _maxWeight = weight;
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
    uint32_t x = _rdGen.rnd<uint32_t>(0, _totalWeight-1);
    uint32_t bucketIdx = find_bucket(x, _minWeight, _maxWeight);
    Bucket& bucket = _buckets[bucketIdx];

    assert(!bucket.items.empty());

    // choose random item within the bucket
    uint32_t itemIdx = (x - bucket.weightBoundary) / bucketIdx;
    assert(itemIdx < bucket.items.size());

    id = bucket.items[itemIdx];

    size_t s = bucket.items.size();
    if (itemIdx < s-1) {
        bucket.items[itemIdx] = bucket.items[s-1];
    }
    bucket.items.resize(s-1);

    // update total weight and partial weights
    uint32_t weight = bucketIdx;
    _totalWeight -= weight;
    for (; bucketIdx<=_maxItemWeight; ++bucketIdx) {
        _buckets[bucketIdx].weightBoundary -= weight;
    }

    _totalItems--;

    return id;
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
