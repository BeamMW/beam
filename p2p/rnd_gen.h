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
#include <random>
#include <limits>

namespace beam {

/// Pseudo-random numbers for P2P purposes
class RandomGen {
public:
    /// Seeds mersenne-twister
    RandomGen();

    /// Returns random number in range [a,b]
    template <typename T> T rnd(
        T a = std::numeric_limits<T>::min(),
        T b = std::numeric_limits<T>::max()
    ) {
        if (a == b) return a;
        std::uniform_int_distribution<T> dis(a, b);
        return dis(_rdGen);
    }

private:
    std::mt19937 _rdGen;
};

} //namespace
