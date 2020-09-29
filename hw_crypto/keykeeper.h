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

#pragma once
#include "kdf.h"
#include "coinid.h"
#include "sign.h"

typedef struct
{
	BeamCrypto_Kdf m_MasterKey;

	int m_AllowWeakInputs;

	// TODO: state, Slot management, etc.

} BeamCrypto_KeyKeeper;

void BeamCrypto_SecureEraseMem(void*, uint32_t);

uint32_t BeamCrypto_KeyKeeper_getNumSlots();
void BeamCrypto_KeyKeeper_ReadSlot(uint32_t, BeamCrypto_UintBig*);
void BeamCrypto_KeyKeeper_RegenerateSlot(uint32_t);

typedef struct
{
	BeamCrypto_UintBig m_Secret;
	BeamCrypto_CompactPoint m_CoFactorG;
	BeamCrypto_CompactPoint m_CoFactorJ;

} BeamCrypto_KdfPub;

void BeamCrypto_KeyKeeper_GetPKdf(const BeamCrypto_KeyKeeper*, BeamCrypto_KdfPub*, const uint32_t* pChild); // if pChild is NULL then the master kdfpub (owner key) is returned

typedef uint64_t BeamCrypto_Height;
typedef uint64_t BeamCrypto_WalletIdentity;

typedef struct
{
	BeamCrypto_Amount m_Fee;
	BeamCrypto_Height m_hMin;
	BeamCrypto_Height m_hMax;

	BeamCrypto_CompactPoint m_Commitment;
	BeamCrypto_Signature m_Signature;

} BeamCrypto_TxKernel;

void BeamCrypto_TxKernel_getID(const BeamCrypto_TxKernel*, BeamCrypto_UintBig* pMsg);
int BeamCrypto_TxKernel_IsValid(const BeamCrypto_TxKernel*);

typedef struct
{
	const BeamCrypto_CoinID* m_pIns;
	const BeamCrypto_CoinID* m_pOuts;
	unsigned int m_Ins;
	unsigned int m_Outs;

	BeamCrypto_TxKernel m_Krn;
	BeamCrypto_UintBig m_kOffset;

} BeamCrypto_TxCommon;

#define BeamCrypto_KeyKeeper_Status_Ok 0
#define BeamCrypto_KeyKeeper_Status_Unspecified 1
#define BeamCrypto_KeyKeeper_Status_UserAbort 2
#define BeamCrypto_KeyKeeper_Status_NotImpl 3

// Split tx, no value transfer. Only fee is spent (hence the user agreement is required)
int BeamCrypto_KeyKeeper_SignTx_Split(const BeamCrypto_KeyKeeper*, BeamCrypto_TxCommon*);

typedef struct
{
	BeamCrypto_UintBig m_Peer;
	BeamCrypto_WalletIdentity m_MyIDKey;
	BeamCrypto_Signature m_PaymentProofSignature;

} BeamCrypto_TxMutualInfo;

int BeamCrypto_KeyKeeper_SignTx_Receive(const BeamCrypto_KeyKeeper*, BeamCrypto_TxCommon*, BeamCrypto_TxMutualInfo*);

typedef struct
{
	uint32_t m_iSlot;
	BeamCrypto_UintBig m_UserAgreement; // set to Zero on 1st invocation

} BeamCrypto_TxSenderParams;

int BeamCrypto_KeyKeeper_SignTx_Send(const BeamCrypto_KeyKeeper*, BeamCrypto_TxCommon*, BeamCrypto_TxMutualInfo*, BeamCrypto_TxSenderParams*);

typedef struct
{
	// ticket
	BeamCrypto_CompactPoint m_SerialPub;
	BeamCrypto_CompactPoint m_NoncePub;
	BeamCrypto_UintBig m_pK[2];

	BeamCrypto_UintBig m_SharedSecret;
	BeamCrypto_Signature m_Signature;

} BeamCrypto_ShieldedVoucher;

int BeamCrypto_KeyKeeper_CreateVouchers(const BeamCrypto_KeyKeeper*, BeamCrypto_ShieldedVoucher*, uint32_t n, BeamCrypto_WalletIdentity nMyIDKey, BeamCrypto_UintBig* pNonce0);
