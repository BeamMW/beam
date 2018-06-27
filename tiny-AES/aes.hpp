#ifndef _AES_HPP_
#define _AES_HPP_

#ifndef __cplusplus
#error Do not include the hpp header in a c project!
#endif //__cplusplus

extern "C" {
#include "aes.h"
}

class AES_StreamCipher
{
	AES_ctx m_Ctx;

	uint8_t m_pBuf[AES_BLOCKLEN];
	uint8_t m_nBuf;

	void PerfXor(uint8_t* pBuf, uint32_t nSize);

public:

	void Init(const uint8_t* pKey, const uint8_t* pIV = NULL);
	void XCrypt(uint8_t* pBuf, uint32_t nSize);
};

#endif //_AES_HPP_
