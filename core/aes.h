#pragma once
#include "uintBig.h"

struct AES
{
	static const int s_KeyBits = 256;
	static const int s_KeyBytes = s_KeyBits >> 3;
	static const int Nr = 14; // num-rounds
	static const int s_BlockSize = 16;

	struct Encoder
	{
		uint32_t m_erk[64]; // encryption round keys. Actually needed 60, but during init extra space is used
		void Init(const uint8_t* pKey);
		void Proceed(uint8_t* pDst, const uint8_t* pSrc) const;
	};

	struct Decoder
	{
		uint32_t m_drk[60]; // decryption round keys
		void Init(const Encoder&);
		void Proceed(uint8_t* pDst, const uint8_t* pSrc) const;
	};

	struct StreamCipher
	{
		beam::uintBig_t<s_BlockSize> m_Counter; // CTR mode

		// generated cipherstream
		uint8_t m_pBuf[s_BlockSize];
		uint8_t m_nBuf;

		void PerfXor(uint8_t* pBuf, uint32_t nSize);

		void Reset();
		void XCrypt(const Encoder&, uint8_t* pBuf, uint32_t nSize);
	};

};
