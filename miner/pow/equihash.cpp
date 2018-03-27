#include "equihash.h"

#include "impl/crypto/equihash.h"
#include "impl/uint256.h"
#include "impl/arith_uint256.h"

namespace pow
{

bool Equihash::solve()
{
    enum{N = 200, K = 9};

    uint256 nNonce;
    std::vector<unsigned char> nSolution;
    std::string testValue{"test string"};
    std::function<bool(std::vector<unsigned char>)> validBlock =
        [&nSolution](std::vector<unsigned char> soln) {
            nSolution = soln;
            return true;
        };

    std::function<bool(EhSolverCancelCheck)> cancelled = [](EhSolverCancelCheck pos) {
        return false;
    };

    while (true)
    {
        crypto_generichash_blake2b_state state;
        EhInitialiseState(N, K, state);

        // I = the block header minus nonce and solution.
        // CEquihashInput I{*pblock};
        // CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        // ss << I;

        // H(I||...
        crypto_generichash_blake2b_update(&state, (unsigned char *)&testValue[0], testValue.size());

        // H(I||V||...
        crypto_generichash_blake2b_state curr_state;
        curr_state = state;
        crypto_generichash_blake2b_update(&curr_state,
                                          nNonce.begin(),
                                          nNonce.size());

        bool found = EhOptimisedSolve(N, K, curr_state, validBlock, cancelled);
        if (found)
        {
            break;
        }
        nNonce = ArithToUint256(UintToArith256(nNonce) + 1);
    }

    // verifier code
    if(false)
    {
        // Hash state
        crypto_generichash_blake2b_state state;
        EhInitialiseState(N, K, state);

        // I = the block header minus nonce and solution.
        //CEquihashInput I{*pblock};
        // I||V
        //CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        //ss << I;
        //ss << pblock->nNonce;

        // H(I||V||...
        crypto_generichash_blake2b_update(&state, (unsigned char *)&testValue[0], testValue.size());
        crypto_generichash_blake2b_update(&state, nNonce.begin(), nNonce.size());

        bool isValid;
        EhIsValidSolution(N, K, state, nSolution, isValid);
        if (isValid)
        {
            printf("%s", "start miner test!!!\n");
        }

        //if (!isValid)
        //    return error("CheckEquihashSolution(): invalid solution");

        //return true;
    }

    return 0;
}

}