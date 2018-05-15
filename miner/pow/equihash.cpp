#include "equihash.h"

#include "impl/crypto/equihash.h"
#include "impl/uint256.h"
#include "impl/arith_uint256.h"
#include <utility>

namespace equi
{

const int N = beam::Block::PoW::N;
const int K = beam::Block::PoW::K;


beam::Block::PoWPtr getSolution(const beam::ByteBuffer& input, const beam::Block::PoW::NonceType& initialNonce, const Cancel& cancel)
{
    beam::Block::PoWPtr proof = std::make_unique<beam::Block::PoW>();
    proof->m_Nonce = initialNonce;

    std::function<bool(beam::ByteBuffer)> validBlock =
        [&proof](beam::ByteBuffer solution) {
            std::copy(solution.begin(), solution.end(), proof->m_Indices.begin());
            return true;
        };

    std::function<bool(EhSolverCancelCheck)> cancelled = [&cancel](EhSolverCancelCheck pos) {
        return cancel();
    };

	Equihash<N, K> eh;

    while (true)
    {
        blake2b_state state;
        eh.InitialiseState(state);

        // H(I||...
        blake2b_update(&state, &input[0], input.size());

        // H(I||V||...
        blake2b_state currState;
        currState = state;
        blake2b_update(&currState, &proof->m_Nonce.m_pData[0], proof->m_Nonce.size());

        bool found = eh.OptimisedSolve(currState, validBlock, cancelled);
        if (found)
        {
            break;
        }

        proof->m_Nonce.Inc();
    }

    return proof;
}

bool isValidProof(const beam::ByteBuffer& input, const beam::Block::PoW& proof)
{
    blake2b_state state;
	Equihash<N, K> eh;
	eh.InitialiseState(state);

    // H(I||V||...
    blake2b_update(&state, &input[0], input.size());
    blake2b_update(&state, &proof.m_Nonce.m_pData[0], proof.m_Nonce.size());

    return eh.IsValidSolution(state, beam::ByteBuffer(proof.m_Indices.begin(), proof.m_Indices.end()));
}
}

