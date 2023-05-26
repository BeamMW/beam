#pragma once
#include <stdint.h>

namespace AES2
{
	struct Coder
	{
		static const uint32_t Nb = 4;
		static const uint32_t Nr_Max = 14;

		unsigned int m_Nk = 0;
		unsigned int m_Nr = 0;
		uint8_t m_pK[4 * Nb * (Nr_Max + 1)]; // round keys

		void Init256(const uint8_t* pKey);
		void Init192(const uint8_t* pKey);
		void Init128(const uint8_t* pKey);

		void EncodeBlock(uint8_t* pDst, const uint8_t* pSrc) const;
		void DecodeBlock(uint8_t* pDst, const uint8_t* pSrc) const;

	private:
		void InitInternal(const uint8_t* pKey);
	};

} // namespace AES2
