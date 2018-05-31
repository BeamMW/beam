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
