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

//////////////////////////
// External functions, implemented by the platform-specific code
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
} BeamCrypto_TxKernelUser;

typedef struct
{
	BeamCrypto_CompactPoint m_Commitment;
	BeamCrypto_Signature m_Signature;

} BeamCrypto_TxKernelData;

void BeamCrypto_TxKernel_getID(const BeamCrypto_TxKernelUser*, const BeamCrypto_TxKernelData*, BeamCrypto_UintBig* pMsg);
int BeamCrypto_TxKernel_IsValid(const BeamCrypto_TxKernelUser*, const BeamCrypto_TxKernelData*);

typedef struct
{
	BeamCrypto_CompactPoint m_Commitment;
	BeamCrypto_CompactPoint m_NoncePub;

} BeamCrypto_TxKernelCommitments;

typedef struct
{
	BeamCrypto_UintBig m_Sender;
	BeamCrypto_UintBig m_pMessage[2];

} BeamCrypto_ShieldedTxoUser;

typedef struct
{
	// ticket source params
	BeamCrypto_UintBig m_kSerG;
	uint32_t m_nViewerIdx;
	uint8_t m_IsCreatedByViewer;

	// sender params
	BeamCrypto_ShieldedTxoUser m_User;
	BeamCrypto_Amount m_Amount;
	BeamCrypto_AssetID m_AssetID;

} BeamCrypto_ShieldedTxoID;

typedef struct
{
	BeamCrypto_ShieldedTxoID m_TxoID;
	BeamCrypto_Amount m_Fee;

} BeamCrypto_ShieldedInput;

typedef struct
{
	BeamCrypto_TxKernelUser m_Krn;

	uint32_t m_Ins;
	uint32_t m_Outs;
	uint32_t m_InsShielded;

} BeamCrypto_TxCommonIn;

typedef struct
{
	BeamCrypto_TxKernelData m_Krn;
	BeamCrypto_UintBig m_kOffset;

} BeamCrypto_TxCommonOut;

typedef struct
{
	BeamCrypto_UintBig m_Peer;
	BeamCrypto_WalletIdentity m_MyIDKey;

} BeamCrypto_TxMutualIn;



#define BeamCrypto_KeyKeeper_Status_Ok 0
#define BeamCrypto_KeyKeeper_Status_Unspecified 1
#define BeamCrypto_KeyKeeper_Status_UserAbort 2
#define BeamCrypto_KeyKeeper_Status_NotImpl 3

#define BeamCrypto_KeyKeeper_Status_ProtoError 10


typedef struct
{
	// ticket
	BeamCrypto_CompactPoint m_SerialPub;
	BeamCrypto_CompactPoint m_NoncePub;
	BeamCrypto_UintBig m_pK[2];

	BeamCrypto_UintBig m_SharedSecret;
	BeamCrypto_Signature m_Signature;

} BeamCrypto_ShieldedVoucher;

#pragma pack (push, 1)
typedef struct
{
	// packed into 674 bytes, serialized the same way
	BeamCrypto_UintBig m_Ax;
	BeamCrypto_UintBig m_Sx;
	BeamCrypto_UintBig m_T1x;
	BeamCrypto_UintBig m_T2x;
	BeamCrypto_UintBig m_Taux;
	BeamCrypto_UintBig m_Mu;
	BeamCrypto_UintBig m_tDot;
	BeamCrypto_UintBig m_pLRx[6][2];
	BeamCrypto_UintBig m_pCondensed[2];
	uint8_t m_pYs[2];

} BeamCrypto_RangeProof_Packed;
#pragma pack (pop)

//////////////////
// Protocol
#define BeamCrypto_CurrentProtoVer 1

#define BeamCrypto_ProtoRequest_Version(macro)
#define BeamCrypto_ProtoResponse_Version(macro) \
	macro(1, uint32_t, Value)

#define BeamCrypto_ProtoRequest_GetNumSlots(macro)
#define BeamCrypto_ProtoResponse_GetNumSlots(macro) \
	macro(1, uint32_t, Value)

#define BeamCrypto_ProtoRequest_GetPKdf(macro) \
	macro(0, uint8_t, Root) \
	macro(1, uint32_t, iChild)

#define BeamCrypto_ProtoResponse_GetPKdf(macro) \
	macro(0, BeamCrypto_KdfPub, Value)

#define BeamCrypto_ProtoRequest_CreateOutput(macro) \
	macro(1, BeamCrypto_CoinID, Cid) \
	macro(0, BeamCrypto_UintBig, pKExtra[2]) \
	macro(0, BeamCrypto_CompactPoint, pT[2]) \

#define BeamCrypto_ProtoResponse_CreateOutput(macro) \
	macro(0, BeamCrypto_CompactPoint, pT[2]) \
	macro(0, BeamCrypto_UintBig, TauX) \

#define BeamCrypto_ProtoRequest_CreateShieldedInput(macro) \
	macro(1, BeamCrypto_ShieldedInput, Inp) \
	macro(1, BeamCrypto_Height, hMin) \
	macro(1, BeamCrypto_Height, hMax) \
	macro(1, uint64_t, WindowEnd) \
	macro(1, uint32_t, Sigma_M) \
	macro(1, uint32_t, Sigma_n) \
	macro(0, BeamCrypto_UintBig, AssetSk) /* negated blinding for asset generator (H` = H - assetSk*G) */ \
	macro(0, BeamCrypto_UintBig, OutpSk) /* The overall blinding factor of the shielded Txo (not secret) */ \
	macro(0, BeamCrypto_CompactPoint, pABCD[4]) \
	/* followed by BeamCrypto_CompactPoint* pG[] */

#define BeamCrypto_ProtoResponse_CreateShieldedInput(macro) \
	macro(0, BeamCrypto_CompactPoint, G0) \
	macro(0, BeamCrypto_CompactPoint, NoncePub) \
	macro(0, BeamCrypto_UintBig, pSig[2]) \
	macro(0, BeamCrypto_UintBig, zR)

#define BeamCrypto_ProtoRequest_CreateShieldedVouchers(macro) \
	macro(1, uint32_t, Count) \
	macro(1, BeamCrypto_WalletIdentity, nMyIDKey) \
	macro(0, BeamCrypto_UintBig, Nonce0) \

#define BeamCrypto_ProtoResponse_CreateShieldedVouchers(macro) \
	macro(1, uint32_t, Count) \
	/* followed by BeamCrypto_ShieldedVoucher[] */

#define BeamCrypto_ProtoRequest_TxSplit(macro) \
	macro(1, BeamCrypto_TxCommonIn, Tx) \
	/* followed by in/outs */

#define BeamCrypto_ProtoResponse_TxSplit(macro) \
	macro(0, BeamCrypto_TxCommonOut, Tx) \

#define BeamCrypto_ProtoRequest_TxReceive(macro) \
	macro(1, BeamCrypto_TxCommonIn, Tx) \
	macro(1, BeamCrypto_TxMutualIn, Mut) \
	macro(0, BeamCrypto_TxKernelData, Krn) \
	/* followed by in/outs */

#define BeamCrypto_ProtoResponse_TxReceive(macro) \
	macro(0, BeamCrypto_TxCommonOut, Tx) \
	macro(0, BeamCrypto_Signature, PaymentProof) \

#define BeamCrypto_ProtoRequest_TxSend1(macro) \
	macro(1, BeamCrypto_TxCommonIn, Tx) \
	macro(1, BeamCrypto_TxMutualIn, Mut) \
	macro(1, uint32_t, iSlot) \
	/* followed by in/outs */

#define BeamCrypto_ProtoResponse_TxSend1(macro) \
	macro(0, BeamCrypto_TxKernelCommitments, HalfKrn) \
	macro(0, BeamCrypto_UintBig, UserAgreement) \

#define BeamCrypto_ProtoRequest_TxSend2(macro) \
	BeamCrypto_ProtoRequest_TxSend1(macro) \
	macro(0, BeamCrypto_TxKernelCommitments, HalfKrn) \
	macro(0, BeamCrypto_Signature, PaymentProof) \
	macro(0, BeamCrypto_UintBig, UserAgreement) \
	/* followed by in/outs */

#define BeamCrypto_ProtoResponse_TxSend2(macro) \
	macro(0, BeamCrypto_UintBig, kSig) \
	macro(0, BeamCrypto_UintBig, kOffset) \

#define BeamCrypto_ProtoRequest_TxSendShielded(macro) \
	macro(1, BeamCrypto_TxCommonIn, Tx) \
	macro(1, BeamCrypto_TxMutualIn, Mut) \
	macro(0, BeamCrypto_ShieldedVoucher, Voucher) \
	macro(0, BeamCrypto_ShieldedTxoUser, User) \
	macro(0, BeamCrypto_RangeProof_Packed, RangeProof) \
	macro(0, uint8_t, HideAssetAlways) /* important to specify, this affects expected blinding factor recovery */ \
	/* followed by in/outs */

#define BeamCrypto_ProtoResponse_TxSendShielded(macro) \
	macro(0, BeamCrypto_TxCommonOut, Tx) \


#define BeamCrypto_ProtoMethods(macro) \
	macro(0x01, Version) \
	macro(0x02, GetNumSlots) \
	macro(0x03, GetPKdf) \
	macro(0x10, CreateOutput) \
	macro(0x21, CreateShieldedInput) \
	macro(0x22, CreateShieldedVouchers) \
	macro(0x30, TxSplit) \
	macro(0x31, TxReceive) \
	macro(0x32, TxSend1) \
	macro(0x33, TxSend2) \
	macro(0x36, TxSendShielded) \

int BeamCrypto_KeyKeeper_Invoke(const BeamCrypto_KeyKeeper*, uint8_t* pIn, uint32_t nIn, uint8_t* pOut, uint32_t nOut);

//////////////////////////
// KeyKeeper - request user approval for spend
//
// pPeerID is NULL, if it's a Split tx (i.e. funds are transferred back to you, only the fee is spent).
// pKrnID and pData are NULL, if this is a 'preliminary' confirmation (SendTx 1st invocation)
// pUser contains fee and min/max height (may be shown to the user)
// pData (if specified) has commitments.
int BeamCrypto_KeyKeeper_ConfirmSpend(
	BeamCrypto_Amount val, BeamCrypto_AssetID aid, const BeamCrypto_UintBig* pPeerID,
	const BeamCrypto_TxKernelUser* pUser, const BeamCrypto_TxKernelData* pData, const BeamCrypto_UintBig* pKrnID);
