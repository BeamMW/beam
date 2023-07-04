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

#include "keccak.h"

namespace beam {

KeccakProcessorBase::KeccakProcessorBase()
{
	ZeroObject(*this);
}

void KeccakProcessorBase::WriteInternal(const uint8_t* pSrc, uint32_t nSrc, uint32_t nWordsBlock)
{
	if (m_LastWordBytes)
	{
		auto* pDst = m_pLastWordAsBytes + m_LastWordBytes;

		if (m_LastWordBytes + nSrc < nSizeWord)
		{
			memcpy(pDst, pSrc, nSrc);
			m_LastWordBytes += nSrc;
			return;
		}

		uint32_t nPortion = nSizeWord - m_LastWordBytes;

		memcpy(pDst, pSrc, nPortion);
		pSrc += nPortion;
		nSrc -= nPortion;

		m_LastWordBytes = 0;
		AddLastWordInternal(nWordsBlock);
	}

	while (true)
	{
		if (nSrc < nSizeWord)
		{
			memcpy(m_pLastWordAsBytes, pSrc, nSrc);
			m_LastWordBytes = nSrc;
			return;
		}

		memcpy(m_pLastWordAsBytes, pSrc, nSizeWord);
		pSrc += nSizeWord;
		nSrc -= nSizeWord;

		AddLastWordInternal(nWordsBlock);
	}
}

void KeccakProcessorBase::ReadInternal(uint8_t* pRes, uint32_t nWordsBlock, uint32_t nBytes)
{
	// pad and transform
	assert(m_LastWordBytes < nSizeWord);
	m_pLastWordAsBytes[m_LastWordBytes++] = 0x01;
	memset0(m_pLastWordAsBytes + m_LastWordBytes, nSizeWord - m_LastWordBytes);

	AddLastWordRawInternal();

	m_pState[nWordsBlock - 1] ^= 0x8000000000000000;

	ethash_keccakf1600(m_pState);

	for (uint32_t i = 0; i < (nBytes / nSizeWord); ++i)
		reinterpret_cast<uint64_t*>(pRes)[i] = ByteOrder::from_le(m_pState[i]);
}

void KeccakProcessorBase::AddLastWordRawInternal()
{
	assert(m_iWord < _countof(m_pState));
	m_pState[m_iWord] ^= ByteOrder::to_le(m_LastWord);
}

void KeccakProcessorBase::AddLastWordInternal(uint32_t nWordsBlock)
{
	AddLastWordRawInternal();

	if (++m_iWord == nWordsBlock)
	{
		ethash_keccakf1600(m_pState);
		m_iWord = 0;
	}
}


} // namespace beam
