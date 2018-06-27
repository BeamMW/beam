//#include <stdint.h>
//#include <string.h> // CBC mode, for memset
#include "aes.hpp"

void AES_StreamCipher::PerfXor(uint8_t* pBuf, uint32_t nSize)
{
	assert(m_nBuf >= nSize);
	const uint8_t* pXor = m_pBuf + _countof(m_pBuf) - m_nBuf;
	m_nBuf -= nSize;

	for (uint32_t i = 0; i < nSize; i++)
		pBuf[i] ^= pXor[i];
}

void AES_StreamCipher::Init(const uint8_t* pKey, const uint8_t* pIV /* = NULL */)
{
	AES_init_ctx(&m_Ctx, pKey);
	if (pIV)
		AES_ctx_set_iv(&m_Ctx, pIV);
	else
		ZeroObject(m_Ctx.Iv);

	m_nBuf = 0;
}

void AES_StreamCipher::XCrypt(uint8_t* pBuf, uint32_t nSize)
{
	while (true)
	{
		if (!m_nBuf)
		{
			AES_CTR_GenerateXorComplement(&m_Ctx, m_pBuf);
			m_nBuf = _countof(m_pBuf);
		}

		if (m_nBuf >= nSize)
		{
			PerfXor(pBuf, nSize);
			break;
		}

		uint8_t n = m_nBuf;
		PerfXor(pBuf, m_nBuf);

		pBuf += n;
		nSize -= n;
	}
}
