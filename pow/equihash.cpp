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

#include "core/block_crypt.h"
#include "crypto/equihash.h"
#include "uint256.h"
#include "arith_uint256.h"
#include <utility>
#include "utility/logger.h"

#if defined (BEAM_USE_GPU)
#include "3rdparty/equihash_gpu.h"
#endif

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
		blake2b_update(&m_Blake, nonce.m_pData, nonce.nBytes);
	}

	bool TestDifficulty(const uint8_t* pSol, uint32_t nSol, Difficulty d) const
	{
		ECC::Hash::Value hv;
		ECC::Hash::Processor() << Blob(pSol, nSol) >> hv;

		return d.IsTargetReached(hv);
	}
};

#if defined (BEAM_USE_GPU)

    bool Block::PoW::SolveGPU(const void* pInput, uint32_t nSizeInput, const Cancel& fnCancel)
    {
        Helper hlp;
        static EquihashGpu gpu;


        auto fnValid = [this, &hlp, pInput, nSizeInput](const beam::ByteBuffer& solution, const beam::Block::PoW::NonceType& nonce)
            {

                if (!hlp.TestDifficulty(&solution.front(), (uint32_t)solution.size(), m_Difficulty))
                {
                   // LOG_DEBUG() << "===============================  Difficulty is not reachable nonce: " << nonce << " Diff = " << m_Difficulty.ToFloat();
                    return false;
                }
        		 
        	    assert(solution.size() == m_Indices.size());
                std::copy(solution.begin(), solution.end(), m_Indices.begin());
                return true;
            };


        std::function<bool()> fnCancelInternal = [fnCancel]() {
            return fnCancel(false);
        };

        if (!gpu.solve(pInput, nSizeInput, fnValid, fnCancelInternal))
            return false;

        if (fnCancel(true))
        	return false; // retry not allowed

        LOG_DEBUG() << "===============================  Solution found: " << m_Nonce;
        return true;
    }

#endif

bool Block::PoW::Solve(const void* pInput, uint32_t nSizeInput, const Cancel& fnCancel)
{
	Helper hlp;

	std::function<bool(const beam::ByteBuffer&)> fnValid = [this, &hlp](const beam::ByteBuffer& solution)
		{
			if (!hlp.TestDifficulty(&solution.front(), (uint32_t) solution.size(), m_Difficulty))
				return false;
			assert(solution.size() == m_Indices.size());
            std::copy(solution.begin(), solution.end(), m_Indices.begin());
            return true;
        };


    std::function<bool(EhSolverCancelCheck)> fnCancelInternal = [fnCancel](EhSolverCancelCheck pos) {
        return fnCancel(false);
    };

    while (true)
    {
		hlp.Reset(pInput, nSizeInput, m_Nonce);

		try {

			if (hlp.m_Eh.OptimisedSolve(hlp.m_Blake, fnValid, fnCancelInternal))
				break;

		} catch (const EhSolverCancelledException&) {
			return false;
		}

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
    return
		hlp.m_Eh.IsValidSolution(hlp.m_Blake, v) &&
		hlp.TestDifficulty(&m_Indices.front(), (uint32_t) m_Indices.size(), m_Difficulty);
}

} // namespace beam

