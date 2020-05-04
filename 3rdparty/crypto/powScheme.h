#include <vector>

#ifndef POWSCHEME_H
#define POWSCHEME_H

#if defined(__ANDROID__) || !defined(BEAM_USE_AVX)
#include "blake/ref/blake2.h"
#else
#include "blake/sse/blake2.h"
#endif

enum SolverCancelCheck
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
    PartialEnd,
    MixElements
};

class SolverCancelledException : public std::exception
{
    virtual const char* what() const throw() {
        return "BeamHash solver was cancelled";
    }
};


class PoWScheme {
public:
    virtual int InitialiseState(blake2b_state& base_state) = 0;
    virtual bool IsValidSolution(const blake2b_state& base_state, std::vector<unsigned char> soln) = 0;	

#ifdef ENABLE_MINING
    virtual bool OptimisedSolve(const blake2b_state& base_state,
                        const std::function<bool(const std::vector<unsigned char>&)> validBlock,
                        const std::function<bool(SolverCancelCheck)> cancelled) = 0;
#endif
    
};  

#endif
