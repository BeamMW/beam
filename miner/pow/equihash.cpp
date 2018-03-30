#include "equihash.h"

#include "impl/crypto/equihash.h"
#include "impl/uint256.h"
#include "impl/arith_uint256.h"
#include <utility>

namespace equi
{

const int N = 200;
const int K = 9;

beam::Proof getSolution(const beam::ByteBuffer &input, const beam::uint256_t &initial_nonce, const Cancel cancel)
{

    beam::Proof proof;
    proof.nonce = initial_nonce;

    std::function<bool(beam::ByteBuffer)> valid_block =
        [&proof](beam::ByteBuffer soln) {
            proof.solution = soln;
            return true;
        };

    std::function<bool(EhSolverCancelCheck)> cancelled = [cancel](EhSolverCancelCheck pos) {
        return cancel();
    };

    while (true)
    {
        blake2b_state state;
        EhInitialiseState(N, K, state);

        // H(I||...
        blake2b_update(&state, &input[0], input.size());

        // H(I||V||...
        blake2b_state curr_state;
        curr_state = state;
        blake2b_update(&curr_state, &proof.nonce[0], proof.nonce.size());

        bool found = EhOptimisedSolve(N, K, curr_state, valid_block, cancelled);
        if (found)
        {
            break;
        }
        auto tt = uint256(std::vector<uint8_t>(proof.nonce.begin(), proof.nonce.end()));
        auto t = ArithToUint256(UintToArith256(tt) + 1);
        std::copy(t.begin(), t.end(), proof.nonce.begin());
    }

    return proof;
}

bool isValidProof(const beam::ByteBuffer &input, const beam::Proof &proof)
{
    blake2b_state state;
    EhInitialiseState(N, K, state);

    // H(I||V||...
    blake2b_update(&state, &input[0], input.size());
    blake2b_update(&state, &proof.nonce[0], proof.nonce.size());

    bool is_valid = false;
    EhIsValidSolution(N, K, state, proof.solution, is_valid);
    return is_valid;
}
}