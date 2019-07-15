// Copyright (c) 2019 The Beam Team	

// Based on Reference Implementation of the Equihash Proof-of-Work algorithm.
// Copyright (c) 2016 Jack Grigg
// Copyright (c) 2016 The Zcash developers

// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Resources:
// Alex Biryukov and Dmitry Khovratovich
// Equihash: Asymmetric Proof-of-Work Based on the Generalized Birthday Problem
// NDSS ’16, 21-24 February 2016, San Diego, CA, USA
// https://www.internetsociety.org/sites/default/files/blogs-media/equihash-asymmetric-proof-of-work-based-generalized-birthday-problem.pdf

#ifndef EQUIHASHR_H
#define EQUIHASHR_H

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
        return "BeamHash solver was cancelled";
    }
};

inline constexpr const size_t max(const size_t A, const size_t B) { return A > B ? A : B; }

inline constexpr size_t beamhash_solution_size(unsigned int N, unsigned int K) {
    return (1 << K)*(N/(K+1)+1)/8;
}

constexpr uint8_t GetSizeInBytes(size_t N)
{
    return static_cast<uint8_t>((N + 7) / 8);
}



template<size_t WIDTH>
bool DistinctIndices(const FullStepRow<WIDTH>& a, const FullStepRow<WIDTH>& b, size_t len, size_t lenIndices)
{
    for(size_t i = 0; i < lenIndices; i += sizeof(eh_index)) {
        for(size_t j = 0; j < lenIndices; j += sizeof(eh_index)) {
            if (memcmp(a.hash+len+i, b.hash+len+j, sizeof(eh_index)) == 0) {
                return false;
            }
        }
    }
    return true;
}

template<size_t MAX_INDICES>
bool IsProbablyDuplicate(std::shared_ptr<eh_trunc> indices, size_t lenIndices)
{
    bool checked_index[MAX_INDICES] = {false};
    size_t count_checked = 0;
    for (size_t z = 0; z < lenIndices; z++) {
        // Skip over indices we have already paired
        if (!checked_index[z]) {
            for (size_t y = z+1; y < lenIndices; y++) {
                if (!checked_index[y] && indices.get()[z] == indices.get()[y]) {
                    // Pair found
                    checked_index[y] = true;
                    count_checked += 2;
                    break;
                }
            }
        }
    }
    return count_checked == lenIndices;
}

template<size_t WIDTH>
bool IsValidBranch(const FullStepRow<WIDTH>& a, const size_t len, const unsigned int ilen, const eh_trunc t)
{
    return TruncateIndex(ArrayToEhIndex(a.hash+len), ilen) == t;
}



class PoWScheme {
public:
    virtual int InitialiseState(eh_HashState& base_state) = 0;
    virtual bool IsValidSolution(const eh_HashState& base_state, std::vector<unsigned char> soln) = 0;	

#ifdef ENABLE_MINING
    virtual bool OptimisedSolve(const eh_HashState& base_state,
                        const std::function<bool(const std::vector<unsigned char>&)> validBlock,
                        const std::function<bool(EhSolverCancelCheck)> cancelled) = 0;
#endif
    
};  



template<unsigned int N, unsigned int K, unsigned int R>
class EquihashR : public PoWScheme
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

    EquihashR() { }

    int InitialiseState(eh_HashState& base_state);
    bool IsValidSolution(const eh_HashState& base_state, std::vector<unsigned char> soln);
#ifdef ENABLE_MINING
    bool OptimisedSolve(const eh_HashState& base_state,
                        const std::function<bool(const std::vector<unsigned char>&)> validBlock,
                        const std::function<bool(EhSolverCancelCheck)> cancelled);
#endif
};

static EquihashR<150,5,0> BeamHashI;
static EquihashR<150,5,3> BeamHashII;


#define EhRInitialiseState(n, k, r, base_state)  \
     if (n == 150 && k == 5 && r == 0) { 				\
        BeamHashI.InitialiseState(base_state); 	\
    } else if (n == 150 && k == 5 && r == 3)) {       			\
        BeamHashII.InitialiseState(base_state);	\
    } else {                                 				\
        throw std::invalid_argument("Unsupported Equihash parameters"); \
    }

#define EhRIsValidSolution(n, k, r, base_state, soln, ret)   	\
    if (n == 150 && k == 5 && r == 0) {                 	\
        ret = BeamHashI.IsValidSolution(base_state, soln); 	\
    } else if (n == 150 && k == 5 && r == 3)) {            	\
        ret = BeamHashII.IsValidSolution(base_state, soln);  	\
    } else {                                             	\
        throw std::invalid_argument("Unsupported Equihash parameters"); \
    }


#endif 
