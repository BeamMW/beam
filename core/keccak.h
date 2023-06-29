#pragma once
#include "uintBig.h"
#include "../utility/byteorder.h"
#include "ethash/include/ethash/keccak.h"

namespace beam
{
	// Wrapper for keccak hash algorithm. In contrast to original implementation it allows partial write in several calls
	template <uint32_t nBits_>
	struct KeccakProcessor
	{
		static const uint32_t nBits = nBits_;
		static const uint32_t nBytes = nBits / 8;

		static const uint32_t nSizeWord = sizeof(uint64_t);
		static const uint32_t nSizeBlock = (1600 - nBits * 2) / 8;
		static const uint32_t nWordsBlock = nSizeBlock / nSizeWord;

		KeccakProcessor()
			:m_LastWordBytes(0) // workaround, otherwise compiler with optimizations produces an out-of-bounds error/warning
		{
			ZeroObject(*this);
			
		}

		void Write(const uint8_t* pSrc, uint32_t nSrc)
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
				AddLastWord();
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

				AddLastWord();
			}
		}

		void Write(const void* pSrc, uint32_t nSrc)
		{
			Write(reinterpret_cast<const uint8_t*>(pSrc), nSrc);
		}

		template <uint32_t nBytes_>
		void Write(const beam::uintBig_t<nBytes_>& x) { Write(x.m_pData, x.nBytes); }

		void Read(uint8_t* pRes)
		{
			// pad and transform
			m_pLastWordAsBytes[m_LastWordBytes++] = 0x01;
			memset0(m_pLastWordAsBytes + m_LastWordBytes, nSizeWord - m_LastWordBytes);

			AddLastWordRaw();

			m_pState[nWordsBlock - 1] ^= 0x8000000000000000;

			ethash_keccakf1600(m_pState);

			for (uint32_t i = 0; i < (nBytes / nSizeWord); ++i)
				reinterpret_cast<uint64_t*>(pRes)[i] = ByteOrder::from_le(m_pState[i]);
		}

		void operator >> (uintBig_t<nBytes>& hv)
		{
			Read(hv.m_pData);
		}

		template <typename T>
		KeccakProcessor& operator << (const T& t) { Write(t); return *this; }


	private:

		uint64_t m_pState[25];

		union {
			uint64_t m_LastWord;
			uint8_t m_pLastWordAsBytes[nSizeWord];
		};

		uint32_t m_iWord;
		uint32_t m_LastWordBytes;


		void AddLastWordRaw()
		{
			assert(m_iWord < nWordsBlock);
			m_pState[m_iWord] ^= ByteOrder::to_le(m_LastWord);
		}

		void AddLastWord()
		{
			AddLastWordRaw();

			if (++m_iWord == nWordsBlock)
			{
				ethash_keccakf1600(m_pState);
				m_iWord = 0;
			}
		}
	};

} // namespace beam
