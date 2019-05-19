// Copyright (c) 2016 Jack Grigg
// Copyright (c) 2016 The Zcash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_EQUIHASH_H
#define BITCOIN_EQUIHASH_H

#include "crypto/sha256.h"
#include "utilstrencodings.h"

#if defined(__ANDROID__) || !defined(BEAM_USE_AVX)
#include "blake/ref/blake2.h"
#else
#include "blake/sse/blake2.h"
#endif

#include <cstring>
#include <exception>
#include <stdexcept>
#include <functional>
#include <memory>
#include <set>
#include <vector>

typedef blake2b_state eh_HashState;
typedef uint32_t eh_index;
typedef uint8_t eh_trunc;

void ExpandArray(const unsigned char* in, size_t in_len,
                 unsigned char* out, size_t out_len,
                 size_t bit_len, size_t byte_pad=0);
void CompressArray(const unsigned char* in, size_t in_len,
                   unsigned char* out, size_t out_len,
                   size_t bit_len, size_t byte_pad=0);

eh_index ArrayToEhIndex(const unsigned char* array);
eh_trunc TruncateIndex(const eh_index i, const unsigned int ilen);

std::vector<eh_index> GetIndicesFromMinimal(std::vector<unsigned char> minimal,
                                            size_t cBitLen);
std::vector<unsigned char> GetMinimalFromIndices(std::vector<eh_index> indices,
                                                 size_t cBitLen);

template<size_t WIDTH>
class StepRow
{
    template<size_t W>
    friend class StepRow;
    friend class CompareSR;

protected:
    unsigned char hash[WIDTH];

public:
    StepRow(const unsigned char* hashIn, size_t hInLen,
            size_t hLen, size_t cBitLen);
    ~StepRow() { }

    template<size_t W>
    StepRow(const StepRow<W>& a);

    bool IsZero(size_t len);
    std::string GetHex(size_t len) { return HexStr(hash, hash+len); }

    template<size_t W>
    friend bool HasCollision(StepRow<W>& a, StepRow<W>& b, size_t l);
};

class CompareSR
{
private:
    size_t len;

public:
    CompareSR(size_t l) : len {l} { }

    template<size_t W>
    inline bool operator()(const StepRow<W>& a, const StepRow<W>& b) { return memcmp(a.hash, b.hash, len) < 0; }
};

template<size_t WIDTH>
bool HasCollision(StepRow<WIDTH>& a, StepRow<WIDTH>& b, size_t l);

template<size_t WIDTH>
class FullStepRow : public StepRow<WIDTH>
{
    template<size_t W>
    friend class FullStepRow;

    using StepRow<WIDTH>::hash;

public:
    FullStepRow(const unsigned char* hashIn, size_t hInLen,
                size_t hLen, size_t cBitLen, eh_index i);
    ~FullStepRow() { }

    FullStepRow(const FullStepRow<WIDTH>& a) : StepRow<WIDTH> {a} { }
    template<size_t W>
    FullStepRow(const FullStepRow<W>& a, const FullStepRow<W>& b, size_t len, size_t lenIndices, size_t trim);
    FullStepRow& operator=(const FullStepRow<WIDTH>& a);

    inline bool IndicesBefore(const FullStepRow<WIDTH>& a, size_t len, size_t lenIndices) const { return memcmp(hash+len, a.hash+len, lenIndices) < 0; }
    std::vector<unsigned char> GetIndices(size_t len, size_t lenIndices,
                                          size_t cBitLen) const;

    template<size_t W>
    friend bool DistinctIndices(const FullStepRow<W>& a, const FullStepRow<W>& b,
                                size_t len, size_t lenIndices);
    template<size_t W>
    friend bool IsValidBranch(const FullStepRow<W>& a, const size_t len, const unsigned int ilen, const eh_trunc t);
};

template<size_t WIDTH>
class TruncatedStepRow : public StepRow<WIDTH>
{
    template<size_t W>
    friend class TruncatedStepRow;

    using StepRow<WIDTH>::hash;

public:
    TruncatedStepRow(const unsigned char* hashIn, size_t hInLen,
                     size_t hLen, size_t cBitLen,
                     eh_index i, unsigned int ilen);
    ~TruncatedStepRow() { }

    TruncatedStepRow(const TruncatedStepRow<WIDTH>& a) : StepRow<WIDTH> {a} { }
    template<size_t W>
    TruncatedStepRow(const TruncatedStepRow<W>& a, const TruncatedStepRow<W>& b, size_t len, size_t lenIndices, int trim);
    TruncatedStepRow& operator=(const TruncatedStepRow<WIDTH>& a);

    inline bool IndicesBefore(const TruncatedStepRow<WIDTH>& a, size_t len, size_t lenIndices) const { return memcmp(hash+len, a.hash+len, lenIndices) < 0; }
    std::shared_ptr<eh_trunc> GetTruncatedIndices(size_t len, size_t lenIndices) const;
};

enum EhSolverCancelCheck
{
    ListGeneration,
    ListSorting,
    ListColliding,
    RoundEnd,
    FinalSorting,
    FinalColliding,
    PartialGeneration,
    PartialSorting,
    PartialSubtreeEnd,
    PartialIndexEnd,
    PartialEnd
};

class EhSolverCancelledException : public std::exception
{
    virtual const char* what() const throw() {
        return "Equihash solver was cancelled";
    }
};

inline constexpr const size_t max(const size_t A, const size_t B) { return A > B ? A : B; }

inline constexpr size_t equihash_solution_size(unsigned int N, unsigned int K) {
    return (1 << K)*(N/(K+1)+1)/8;
}

constexpr uint8_t GetSizeInBytes(size_t N)
{
    return static_cast<uint8_t>((N + 7) / 8);
}

template<unsigned int N, unsigned int K>
class Equihash
{
private:
    static_assert(K < N);
    static_assert((N/(K+1)) + 1 < 8*sizeof(eh_index));

public:
    enum : size_t { IndicesPerHashOutput=512/N };
    enum : size_t { HashOutput = IndicesPerHashOutput * GetSizeInBytes(N) };
    enum : size_t { CollisionBitLength=N/(K+1) };
    enum : size_t { CollisionByteLength=(CollisionBitLength+7)/8 };
    enum : size_t { HashLength=(K+1)*CollisionByteLength };
    enum : size_t { FullWidth=2*CollisionByteLength+sizeof(eh_index)*(1 << (K-1)) };
    enum : size_t { FinalFullWidth=2*CollisionByteLength+sizeof(eh_index)*(1 << (K)) };
    enum : size_t { TruncatedWidth=max(HashLength+sizeof(eh_trunc), 2*CollisionByteLength+sizeof(eh_trunc)*(1 << (K-1))) };
    enum : size_t { FinalTruncatedWidth=max(HashLength+sizeof(eh_trunc), 2*CollisionByteLength+sizeof(eh_trunc)*(1 << (K))) };
    enum : size_t { SolutionWidth=(1 << K)*(CollisionBitLength+1)/8 };

    Equihash() { }

    int InitialiseState(eh_HashState& base_state);
#ifdef ENABLE_MINING
    bool BasicSolve(const eh_HashState& base_state,
                    const std::function<bool(const std::vector<unsigned char>&)> validBlock,
                    const std::function<bool(EhSolverCancelCheck)> cancelled);
    bool OptimisedSolve(const eh_HashState& base_state,
                        const std::function<bool(const std::vector<unsigned char>&)> validBlock,
                        const std::function<bool(EhSolverCancelCheck)> cancelled);
#endif
    bool IsValidSolution(const eh_HashState& base_state, std::vector<unsigned char> soln);
};

#include "equihash.tcc"

static Equihash<96,3> Eh96_3;
static Equihash<200,9> Eh200_9;
static Equihash<96,5> Eh96_5;
static Equihash<48,5> Eh48_5;

#define EhInitialiseState(n, k, base_state)  \
    if (n == 96 && k == 3) {                 \
        Eh96_3.InitialiseState(base_state);  \
    } else if (n == 200 && k == 9) {         \
        Eh200_9.InitialiseState(base_state); \
    } else if (n == 96 && k == 5) {          \
        Eh96_5.InitialiseState(base_state);  \
    } else if (n == 48 && k == 5) {          \
        Eh48_5.InitialiseState(base_state);  \
    } else {                                 \
        throw std::invalid_argument("Unsupported Equihash parameters"); \
    }

#ifdef ENABLE_MINING
inline bool EhBasicSolve(unsigned int n, unsigned int k, const eh_HashState& base_state,
                    const std::function<bool(const std::vector<unsigned char>&)> validBlock,
                    const std::function<bool(EhSolverCancelCheck)> cancelled)
{
    if (n == 96 && k == 3) {
        return Eh96_3.BasicSolve(base_state, validBlock, cancelled);
    } else if (n == 200 && k == 9) {
        return Eh200_9.BasicSolve(base_state, validBlock, cancelled);
    } else if (n == 96 && k == 5) {
        return Eh96_5.BasicSolve(base_state, validBlock, cancelled);
    } else if (n == 48 && k == 5) {
        return Eh48_5.BasicSolve(base_state, validBlock, cancelled);
    } else {
        throw std::invalid_argument("Unsupported Equihash parameters");
    }
}

inline bool EhBasicSolveUncancellable(unsigned int n, unsigned int k, const eh_HashState& base_state,
                    const std::function<bool(const std::vector<unsigned char>&)> validBlock)
{
    return EhBasicSolve(n, k, base_state, validBlock,
                        [](EhSolverCancelCheck pos) { return false; });
}

inline bool EhOptimisedSolve(unsigned int n, unsigned int k, const eh_HashState& base_state,
                    const std::function<bool(const std::vector<unsigned char>&)> validBlock,
                    const std::function<bool(EhSolverCancelCheck)> cancelled)
{
    if (n == 96 && k == 3) {
        return Eh96_3.OptimisedSolve(base_state, validBlock, cancelled);
    } else if (n == 200 && k == 9) {
        return Eh200_9.OptimisedSolve(base_state, validBlock, cancelled);
    } else if (n == 96 && k == 5) {
        return Eh96_5.OptimisedSolve(base_state, validBlock, cancelled);
    } else if (n == 48 && k == 5) {
        return Eh48_5.OptimisedSolve(base_state, validBlock, cancelled);
    } else {
        throw std::invalid_argument("Unsupported Equihash parameters");
    }
}

inline bool EhOptimisedSolveUncancellable(unsigned int n, unsigned int k, const eh_HashState& base_state,
                    const std::function<bool(const std::vector<unsigned char>&)> validBlock)
{
    return EhOptimisedSolve(n, k, base_state, validBlock,
                            [](EhSolverCancelCheck pos) { return false; });
}
#endif // ENABLE_MINING

#define EhIsValidSolution(n, k, base_state, soln, ret)   \
    if (n == 96 && k == 3) {                             \
        ret = Eh96_3.IsValidSolution(base_state, soln);  \
    } else if (n == 200 && k == 9) {                     \
        ret = Eh200_9.IsValidSolution(base_state, soln); \
    } else if (n == 96 && k == 5) {                      \
        ret = Eh96_5.IsValidSolution(base_state, soln);  \
    } else if (n == 48 && k == 5) {                      \
        ret = Eh48_5.IsValidSolution(base_state, soln);  \
    } else {                                             \
        throw std::invalid_argument("Unsupported Equihash parameters"); \
    }

#endif // BITCOIN_EQUIHASH_H
