#include "roulette.h"
#include <assert.h>

namespace beam {

Roulette::Roulette(size_t maxItemWeight) :
    _maxItemWeight(maxItemWeight)
{
    assert(_maxItemWeight > 0);
    
    _buckets.resize(_maxItemWeight + 1);
    std::random_device _rd;
    _rdGen.seed(_rd());
}

void Roulette::push(Roulette::ID id, size_t weight) {
    if (weight == 0 || id == INVALID_ID) return;
    if (weight > _maxItemWeight) weight = _maxItemWeight;
    _buckets[weight].items.push_back(id);
    
    // update total weight and partial weights
    _totalWeight += weight;
    for (size_t i=weight+1; i<=_maxItemWeight; ++i) {
        _buckets[i].weightBoundary += weight;
    }
    
    _totalItems++;
}

Roulette::ID Roulette::pull() {
    ID id = INVALID_ID;
    
    if (_totalWeight == 0) return id;
    
    // choose random bucket according weighted distribution
    size_t x = rnd(0, _totalWeight);
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

void Roulette::pull_n(std::vector<Roulette::ID>& ids, size_t n) {
    ids.clear();
    if (n == 0 || _totalItems == 0) return;
    
    ids.reserve(n);
    
    if (n >= _totalItems) {
        // just move all items into ids
        for (size_t i=1; i<_totalItems; ++i) {
            Bucket& b = _buckets[i];
            size_t s = b.items.size();
            b.weightBoundary = 0;
            if (s == 0)
                continue;
            ids.insert(ids.end(), b.items.begin(), b.items.end());
            _totalItems -= s;
            if (_totalItems == 0)
                break;
        }
        assert(_totalItems == 0);
        _totalWeight = 0;
        return;
    }
    
    while (n && _totalWeight > 0) {
        --n;
        ids.push_back(pull());
    }
}

// returns random number in range [a,b]
size_t Roulette::rnd(size_t a, size_t b) {
    if (a == b) return a;
    std::uniform_int_distribution<size_t> dis(a, b);
    return dis(_rdGen);
}

// returns index of nonempty bucket with weight range which contains x
size_t Roulette::find_bucket(size_t x, size_t i, size_t j) {
    assert(i > 0 && j > 0 && i <= _maxItemWeight && j <= _maxItemWeight);
    if (i == j) {
        while (_buckets[i].items.empty()) --i;
        return i;
    }
    size_t m = (i+j)/2;
    if (x < _buckets[m].weightBoundary) {
        return find_bucket(x, 1, m-1);
    }
    return find_bucket(x, m, j);
}
    
} //namespace
