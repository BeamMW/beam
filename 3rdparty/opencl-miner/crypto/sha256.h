/* Sha256.h -- SHA-256 Hash
2016-11-04 : Marc Bevand : A few changes to make it more self-contained
2010-06-11 : Igor Pavlov : Public domain */

#ifndef __CRYPTO_SHA256_H
#define __CRYPTO_SHA256_H

#define SHA256_DIGEST_SIZE 32

typedef struct
{
  uint32_t state[8];
  uint64_t count;
  uint8_t buffer[64];
} CSha256;

void Sha256_Init(CSha256 *p);
void Sha256_Update(CSha256 *p, const uint8_t *data, size_t size);
void Sha256_Final(CSha256 *p, uint8_t *digest);
void Sha256_Onestep(const uint8_t *data, size_t size, uint8_t *digest);

#endif
