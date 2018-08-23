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

#pragma once
#include <stdint.h>
#include <vector>

namespace beam {

class RandomGen;

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
    Roulette(RandomGen& rdGen, uint32_t maxItemWeight);

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

    // returns index of nonempty bucket with weight range which contains x
    uint32_t find_bucket(uint32_t x, uint32_t i, uint32_t j);

    RandomGen& _rdGen;
    const uint32_t _maxItemWeight;
    uint32_t _minWeight=0xFFFFFFFF;
    uint32_t _maxWeight=0;
    uint32_t _totalWeight=0;
    uint32_t _totalItems=0;
    Buckets _buckets;
};

} //namespace
