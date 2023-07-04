#pragma once
#include "uintBig.h"
#include "../utility/byteorder.h"
#include "ethash/include/ethash/keccak.h"

namespace beam
{
	// Wrapper for keccak hash algorithm. In contrast to original implementation it allows partial write in several calls

	struct KeccakProcessorBase
	{
		static const uint32_t nSizeWord = sizeof(uint64_t);

	protected:

		KeccakProcessorBase();

		void WriteInternal(const uint8_t* pSrc, uint32_t nSrc, uint32_t nWordsBlock);
		void ReadInternal(uint8_t* pRes, uint32_t nWordsBlock, uint32_t nBytes);

		uint64_t m_pState[25];

		union {
			uint64_t m_LastWord;
			uint8_t m_pLastWordAsBytes[nSizeWord];
		};

		uint32_t m_iWord;
		uint32_t m_LastWordBytes;

		void AddLastWordRawInternal();
		void AddLastWordInternal(uint32_t nWordsBlock);
	};

	template <uint32_t nBits_>
	struct KeccakProcessor
		:public KeccakProcessorBase
	{
		static const uint32_t nBits = nBits_;
		static const uint32_t nBytes = nBits / 8;

		static const uint32_t nSizeBlock = (1600 - nBits * 2) / 8;
		static const uint32_t nWordsBlock = nSizeBlock / nSizeWord;

		KeccakProcessor()
		{
			static_assert(nWordsBlock <= _countof(m_pState), "");
		}

		void Write(const uint8_t* pSrc, uint32_t nSrc)
		{
			WriteInternal(pSrc, nSrc, nWordsBlock);
		}

		void Write(const void* pSrc, uint32_t nSrc)
		{
			Write(reinterpret_cast<const uint8_t*>(pSrc), nSrc);
		}

		template <uint32_t nBytes_>
		void Write(const beam::uintBig_t<nBytes_>& x) { Write(x.m_pData, x.nBytes); }

		template <typename T>
		KeccakProcessor& operator << (const T& t) { Write(t); return *this; }

		void Read(uint8_t* pRes)
		{
			ReadInternal(pRes, nWordsBlock, nBytes);
		}

		void operator >> (uintBig_t<nBytes>& hv)
		{
			Read(hv.m_pData);
		}

	};

} // namespace beam
