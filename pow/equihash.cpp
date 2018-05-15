#include "core/common.h"
#include "impl/crypto/equihash.h"
#include "impl/uint256.h"
#include "impl/arith_uint256.h"
#include <utility>

namespace beam
{

struct Block::PoW::Helper
{
	blake2b_state m_Blake;
	Equihash<Block::PoW::N, Block::PoW::K> m_Eh;

	void Reset(const void* pInput, uint32_t nSizeInput, const NonceType& nonce)
	{
		m_Eh.InitialiseState(m_Blake);

		// H(I||...
		blake2b_update(&m_Blake, (uint8_t*) pInput, nSizeInput);
		blake2b_update(&m_Blake, nonce.m_pData, sizeof(nonce.m_pData));
	}
};

bool Block::PoW::Solve(const void* pInput, uint32_t nSizeInput, const Cancel& fnCancel)
{
    std::function<bool(const beam::ByteBuffer&)> fnValid =
        [this](const beam::ByteBuffer& solution) {
			assert(solution.size() == m_Indices.size());
            std::copy(solution.begin(), solution.end(), m_Indices.begin());
            return true;
        };


    std::function<bool(EhSolverCancelCheck)> fnCancelInternal = [fnCancel](EhSolverCancelCheck pos) {
        return fnCancel(false);
    };

	Helper hlp;

    while (true)
    {
		hlp.Reset(pInput, nSizeInput, m_Nonce);

        if (hlp.m_Eh.OptimisedSolve(hlp.m_Blake, fnValid, fnCancelInternal))
            break;

		if (fnCancel(true))
			return false; // retry not allowed

        m_Nonce.Inc();
    }

    return true;
}

bool Block::PoW::IsValid(const void* pInput, uint32_t nSizeInput) const
{
	Helper hlp;
	hlp.Reset(pInput, nSizeInput, m_Nonce);

	std::vector<uint8_t> v(m_Indices.begin(), m_Indices.end());
    return hlp.m_Eh.IsValidSolution(hlp.m_Blake, v);
}

} // namespace beam

