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
	Kdf m_MasterKey;

	// context information
	union {

		struct {

			int64_t m_RcvBeam;
			int64_t m_RcvAsset; // up to 1 more asset supported in a tx
			Amount m_ImplicitFee; // shielded input fees. Their effect is already accounted for in m_RcvBeam
			AssetID m_Aid;
			secp256k1_scalar m_sk; // net blinding factor, sum(outputs) - sum(inputs)

		} m_TxBalance;

	} u;

	uint8_t m_State;

} KeyKeeper;

#define c_KeyKeeper_State_TxBalance 1


//////////////////////////
// External functions, implemented by the platform-specific code
void SecureEraseMem(void*, uint32_t);
uint32_t KeyKeeper_getNumSlots(KeyKeeper*);
void KeyKeeper_ReadSlot(KeyKeeper*, uint32_t, UintBig*);
void KeyKeeper_RegenerateSlot(KeyKeeper*, uint32_t);
int KeyKeeper_AllowWeakInputs(KeyKeeper*);
Amount KeyKeeper_get_MaxShieldedFee(KeyKeeper*);

typedef struct
{
	UintBig m_Secret;
	CompactPoint m_CoFactorG;
	CompactPoint m_CoFactorJ;

} KdfPub;

void KeyKeeper_GetPKdf(const KeyKeeper*, KdfPub*, const uint32_t* pChild); // if pChild is NULL then the master kdfpub (owner key) is returned

typedef uint64_t Height;
typedef uint64_t WalletIdentity;

typedef struct
{
	Amount m_Fee;
	Height m_hMin;
	Height m_hMax;
} TxKernelUser;

typedef struct
{
	CompactPoint m_Commitment;
	Signature m_Signature;

} TxKernelData;

void TxKernel_getID(const TxKernelUser*, const TxKernelData*, UintBig* pMsg);
int TxKernel_IsValid(const TxKernelUser*, const TxKernelData*);

typedef struct
{
	CompactPoint m_Commitment;
	CompactPoint m_NoncePub;

} TxKernelCommitments;

typedef struct
{
	UintBig m_Sender;
	UintBig m_pMessage[2];

} ShieldedTxoUser;

typedef struct
{
	// ticket source params
	UintBig m_kSerG;
	uint32_t m_nViewerIdx;
	uint8_t m_IsCreatedByViewer;

	// sender params
	ShieldedTxoUser m_User;
	Amount m_Amount;
	AssetID m_AssetID;

} ShieldedTxoID;

typedef struct
{
	ShieldedTxoID m_TxoID;
	Amount m_Fee;

} ShieldedInput;

typedef struct
{
	TxKernelUser m_Krn;

} TxCommonIn;

typedef struct
{
	TxKernelData m_Krn;
	UintBig m_kOffset;

} TxCommonOut;

typedef struct
{
	UintBig m_Peer;
	WalletIdentity m_MyIDKey;

} TxMutualIn;



#define c_KeyKeeper_Status_Ok 0
#define c_KeyKeeper_Status_Unspecified 1
#define c_KeyKeeper_Status_UserAbort 2
#define c_KeyKeeper_Status_NotImpl 3

#define c_KeyKeeper_Status_ProtoError 10


typedef struct
{
	// ticket
	CompactPoint m_SerialPub;
	CompactPoint m_NoncePub;
	UintBig m_pK[2];

	UintBig m_SharedSecret;
	Signature m_Signature;

} ShieldedVoucher;

#define c_ShieldedInput_ChildKdf ((uint32_t) -2)

#pragma pack (push, 1)
typedef struct
{
	// packed into 674 bytes, serialized the same way
	UintBig m_Ax;
	UintBig m_Sx;
	UintBig m_T1x;
	UintBig m_T2x;
	UintBig m_Taux;
	UintBig m_Mu;
	UintBig m_tDot;
	UintBig m_pLRx[6][2];
	UintBig m_pCondensed[2];
	uint8_t m_pYs[2];

} RangeProof_Packed;
#pragma pack (pop)

//////////////////
// Protocol
#define BeamCrypto_CurrentProtoVer 2

#define BeamCrypto_ProtoRequest_Version(macro)
#define BeamCrypto_ProtoResponse_Version(macro) \
	macro(1, uint32_t, Value)

#define BeamCrypto_ProtoRequest_GetNumSlots(macro)
#define BeamCrypto_ProtoResponse_GetNumSlots(macro) \
	macro(1, uint32_t, Value)

#define BeamCrypto_ProtoRequest_GetPKdf(macro) \
	macro(0, uint8_t, Kind) \

#define BeamCrypto_ProtoResponse_GetPKdf(macro) \
	macro(0, KdfPub, Value)

#define BeamCrypto_ProtoRequest_CreateOutput(macro) \
	macro(1, CoinID, Cid) \
	macro(0, CompactPoint, ptAssetGen) \
	macro(0, UintBig, pKExtra[2]) \
	macro(0, CompactPoint, pT[2]) \

#define BeamCrypto_ProtoResponse_CreateOutput(macro) \
	macro(0, CompactPoint, pT[2]) \
	macro(0, UintBig, TauX) \

#define BeamCrypto_ProtoRequest_TxAddCoins(macro) \
	macro(0, uint8_t, Reset) \
	macro(0, uint8_t, Ins) \
	macro(0, uint8_t, Outs) \
	macro(0, uint8_t, InsShielded)
	/* followed by in/outs */


#define BeamCrypto_ProtoResponse_TxAddCoins(macro) \
	macro(0, uint8_t, Dummy) // no response needed actually

#define BeamCrypto_ProtoRequest_GetImage(macro) \
	macro(0, UintBig, hvSrc) \
	macro(1, uint32_t, iChild) \
	macro(0, uint8_t, bG) \
	macro(0, uint8_t, bJ) \

#define BeamCrypto_ProtoResponse_GetImage(macro) \
	macro(0, CompactPoint, ptImageG) \
	macro(0, CompactPoint, ptImageJ) \

#define BeamCrypto_ProtoRequest_CreateShieldedInput(macro) \
	macro(1, ShieldedInput, Inp) \
	macro(1, Height, hMin) \
	macro(1, Height, hMax) \
	macro(1, uint64_t, WindowEnd) \
	macro(1, uint32_t, Sigma_M) \
	macro(1, uint32_t, Sigma_n) \
	macro(0, UintBig, ShieldedState) \
	macro(0, UintBig, AssetSk) /* negated blinding for asset generator (H` = H - assetSk*G) */ \
	macro(0, UintBig, OutpSk) /* The overall blinding factor of the shielded Txo (not secret) */ \
	macro(0, CompactPoint, ptAssetGen) \
	macro(0, CompactPoint, pABCD[4]) \
	/* followed by CompactPoint* pG[] */

#define BeamCrypto_ProtoResponse_CreateShieldedInput(macro) \
	macro(0, CompactPoint, G0) \
	macro(0, CompactPoint, NoncePub) \
	macro(0, UintBig, pSig[2]) \
	macro(0, UintBig, zR)

#define BeamCrypto_ProtoRequest_CreateShieldedVouchers(macro) \
	macro(1, uint32_t, Count) \
	macro(1, WalletIdentity, nMyIDKey) \
	macro(0, UintBig, Nonce0) \

#define BeamCrypto_ProtoResponse_CreateShieldedVouchers(macro) \
	macro(1, uint32_t, Count) \
	/* followed by ShieldedVoucher[] */

#define BeamCrypto_ProtoRequest_TxSplit(macro) \
	macro(1, TxCommonIn, Tx) \

#define BeamCrypto_ProtoResponse_TxSplit(macro) \
	macro(0, TxCommonOut, Tx) \

#define BeamCrypto_ProtoRequest_TxReceive(macro) \
	macro(1, TxCommonIn, Tx) \
	macro(1, TxMutualIn, Mut) \
	macro(0, TxKernelData, Krn) \

#define BeamCrypto_ProtoResponse_TxReceive(macro) \
	macro(0, TxCommonOut, Tx) \
	macro(0, Signature, PaymentProof) \

#define BeamCrypto_ProtoRequest_TxSend1(macro) \
	macro(1, TxCommonIn, Tx) \
	macro(1, TxMutualIn, Mut) \
	macro(1, uint32_t, iSlot) \

#define BeamCrypto_ProtoResponse_TxSend1(macro) \
	macro(0, TxKernelCommitments, HalfKrn) \
	macro(0, UintBig, UserAgreement) \

#define BeamCrypto_ProtoRequest_TxSend2(macro) \
	BeamCrypto_ProtoRequest_TxSend1(macro) \
	macro(0, TxKernelCommitments, HalfKrn) \
	macro(0, Signature, PaymentProof) \
	macro(0, UintBig, UserAgreement) \

#define BeamCrypto_ProtoResponse_TxSend2(macro) \
	macro(0, UintBig, kSig) \
	macro(0, UintBig, kOffset) \

#define BeamCrypto_ProtoRequest_TxSendShielded(macro) \
	macro(1, TxCommonIn, Tx) \
	macro(1, TxMutualIn, Mut) \
	macro(0, ShieldedVoucher, Voucher) \
	macro(0, ShieldedTxoUser, User) \
	macro(0, RangeProof_Packed, RangeProof) \
	macro(0, CompactPoint, ptAssetGen) \
	macro(0, uint8_t, HideAssetAlways) /* important to specify, this affects expected blinding factor recovery */ \

#define BeamCrypto_ProtoResponse_TxSendShielded(macro) \
	macro(0, TxCommonOut, Tx) \


#define BeamCrypto_ProtoMethods(macro) \
	macro(0x01, Version) \
	macro(0x02, GetNumSlots) \
	macro(0x03, GetPKdf) \
	macro(0x04, GetImage) \
	macro(0x10, CreateOutput) \
	macro(0x18, TxAddCoins) \
	macro(0x21, CreateShieldedInput) \
	macro(0x22, CreateShieldedVouchers) \
	macro(0x30, TxSplit) \
	macro(0x31, TxReceive) \
	macro(0x32, TxSend1) \
	macro(0x33, TxSend2) \
	macro(0x36, TxSendShielded) \

int KeyKeeper_Invoke(KeyKeeper*, uint8_t* pInOut, uint32_t nIn, uint32_t nOut);

//////////////////////////
// KeyKeeper - request user approval for spend
//
// pPeerID is NULL, if it's a Split tx (i.e. funds are transferred back to you, only the fee is spent).
// pKrnID and pData are NULL, if this is a 'preliminary' confirmation (SendTx 1st invocation)
// pUser contains fee and min/max height (may be shown to the user)
// pData (if specified) has commitments.
int KeyKeeper_ConfirmSpend(KeyKeeper*,
	Amount val, AssetID aid, const UintBig* pPeerID,
	const TxKernelUser* pUser, const TxKernelData* pData, const UintBig* pKrnID);
