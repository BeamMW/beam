#include "equihash.h"

#include "impl/crypto/equihash.h"
#include "impl/uint256.h"
#include "impl/arith_uint256.h"
#include <utility>

namespace pow
{
const int N = 200;
const int K = 9;

Proof::Proof(Proof &&other) : nonce(std::move(nonce)), solution(std::move(solution))
{
}

pow::Proof get_solution(const Input &input, const uint256_t &initial_nonce, const Cancel cancel)
{

    Proof proof;
    proof.nonce = initial_nonce;

    std::function<bool(Input)> valid_block =
        [&proof](Input soln) {
            proof.solution = soln;
            return true;
        };

    std::function<bool(EhSolverCancelCheck)> cancelled = [cancel](EhSolverCancelCheck pos) {
        return cancel();
    };

    while (true)
    {
        crypto_generichash_blake2b_state state;
        EhInitialiseState(N, K, state);

        // H(I||...
        crypto_generichash_blake2b_update(&state, &input[0], input.size());

        // H(I||V||...
        crypto_generichash_blake2b_state curr_state;
        curr_state = state;
        crypto_generichash_blake2b_update(&curr_state, proof.nonce.begin(), proof.nonce.size());

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

bool is_valid_proof(const Input &input, const Proof &proof)
{
    crypto_generichash_blake2b_state state;
    EhInitialiseState(N, K, state);

    // H(I||V||...
    crypto_generichash_blake2b_update(&state, &input[0], input.size());
    crypto_generichash_blake2b_update(&state, proof.nonce.begin(), proof.nonce.size());

    bool is_valid = false;
    EhIsValidSolution(N, K, state, proof.solution, is_valid);
    return is_valid;
}
}