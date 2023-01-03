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
#include "oracle.h"


typedef struct
{
	UintBig m_Secret;
	CompactPoint m_CoFactorG;
	CompactPoint m_CoFactorJ;

} KdfPub;

typedef uint64_t Height;
typedef uint64_t AddrID;

typedef struct
{
	Amount m_Fee;
	Height m_hMin;
	Height m_hMax;
} TxKernelUser;

typedef struct
{
	CompactPoint m_Commitment;
	CompactPoint m_NoncePub;

} TxKernelCommitments;

void TxKernel_getID(const TxKernelUser*, const TxKernelCommitments*, UintBig* pMsg);
int TxKernel_IsValid(const TxKernelUser*, const TxKernelCommitments*, const UintBig* pSig);

typedef struct
{
	UintBig m_Sender;
	UintBig m_pMessage[2];

} ShieldedTxoUser;

#pragma pack (push, 1)

typedef struct
{
	UintBig m_kSerG;
	ShieldedTxoUser m_User;
	uint8_t m_IsCreatedByViewer;

} ShieldedInput_Blob;

typedef struct
{
	Amount m_Amount;
	Amount m_Fee;
	AssetID m_AssetID;
	uint32_t m_nViewerIdx;
	// alignment should be ok
} ShieldedInput_Fmt;

#pragma pack (pop)

typedef struct
{
	TxKernelUser m_Krn;

} TxCommonIn;

typedef struct
{
	UintBig m_kSig;
	UintBig m_kOffset;

} TxSig;

typedef struct
{
	TxKernelCommitments m_Comms;
	TxSig m_TxSig;

} TxCommonOut;

typedef struct
{
	UintBig m_Peer;
	AddrID m_AddrID;

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

typedef struct
{
	RangeProof_Packed m_RangeProof;
	ShieldedVoucher m_Voucher;
} ShieldedOutParams;

typedef struct
{
	Height m_hMin;
	Height m_hMax;
	uint64_t m_WindowEnd;
	uint32_t m_Sigma_M;
	uint32_t m_Sigma_n;
} ShieldedInput_SpendParams;

#pragma pack (pop)


typedef struct
{
	Kdf m_MasterKey;

	// context information
	uint8_t m_State;

	union {

		struct {

			int64_t m_RcvBeam;
			int64_t m_RcvAsset; // up to 1 more asset supported in a tx
			Amount m_ImplicitFee; // shielded input fees. Their effect is already accounted for in m_RcvBeam
			AssetID m_Aid;
			secp256k1_scalar m_sk; // net blinding factor, sum(outputs) - sum(inputs)

			// 64 bytes so far

			// The following is used for shielded send tx
			ShieldedOutParams m_Sh; // 901 byte
			uint16_t m_SizeSh;

		} m_TxBalance;

		struct
		{
			secp256k1_scalar m_skOutp;
			secp256k1_scalar m_skSpend;
			Oracle m_Oracle; // 100 bytes
			uint32_t m_Sigma_M;
			uint32_t m_Remaining;
		} m_Ins;

	} u;

} KeyKeeper;

#define c_KeyKeeper_State_TxBalance 1
#define c_KeyKeeper_State_CreateShielded_1 11
#define c_KeyKeeper_State_CreateShielded_2 12

void KeyKeeper_GetPKdf(const KeyKeeper*, KdfPub*, const uint32_t* pChild); // if pChild is NULL then the master kdfpub (owner key) is returned


//////////////////
// Protocol
#define BeamCrypto_CurrentSignature "BeamHW.ver.02"

#define BeamCrypto_ProtoRequest_Version(macro)
#define BeamCrypto_ProtoResponse_Version(macro) \
	macro(char, Signature[sizeof(BeamCrypto_CurrentSignature) - 1])

#define BeamCrypto_ProtoRequest_GetNumSlots(macro)
#define BeamCrypto_ProtoResponse_GetNumSlots(macro) \
	macro(uint32_t, Value)

#define BeamCrypto_ProtoRequest_GetPKdf(macro) \
	macro(uint8_t, Kind) \

#define BeamCrypto_ProtoResponse_GetPKdf(macro) \
	macro(KdfPub, Value)

#define BeamCrypto_ProtoRequest_CreateOutput(macro) \
	macro(CoinID, Cid) \
	macro(CompactPoint, ptAssetGen) \
	macro(UintBig, pKExtra[2]) \
	macro(CompactPoint, pT[2]) \

#define BeamCrypto_ProtoResponse_CreateOutput(macro) \
	macro(CompactPoint, pT[2]) \
	macro(UintBig, TauX) \

#define BeamCrypto_ProtoRequest_TxAddCoins(macro) \
	macro(uint8_t, Reset) \
	macro(uint8_t, Ins) \
	macro(uint8_t, Outs) \
	macro(uint8_t, InsShielded)
	/* followed by in/outs */


#define BeamCrypto_ProtoResponse_TxAddCoins(macro)

#define BeamCrypto_ProtoRequest_GetImage(macro) \
	macro(UintBig, hvSrc) \
	macro(uint32_t, iChild) \
	macro(uint8_t, bG) \
	macro(uint8_t, bJ) \

#define BeamCrypto_ProtoResponse_GetImage(macro) \
	macro(CompactPoint, ptImageG) \
	macro(CompactPoint, ptImageJ) \

#define BeamCrypto_ProtoRequest_DisplayAddress(macro) \
	macro(AddrID, AddrID) \

#define BeamCrypto_ProtoResponse_DisplayAddress(macro)

#define BeamCrypto_ProtoRequest_CreateShieldedInput_1(macro) \
	macro(ShieldedInput_Blob, InpBlob) /* 129 bytes */ \
	macro(ShieldedInput_Fmt, InpFmt) /* 24 bytes */ \
	macro(ShieldedInput_SpendParams, SpendParams) /* 32 bytes */ \
	macro(UintBig, ShieldedState) \
	macro(CompactPoint, ptAssetGen) \

#define BeamCrypto_ProtoResponse_CreateShieldedInput_1(macro)

#define BeamCrypto_ProtoRequest_CreateShieldedInput_2(macro) \
	macro(CompactPoint, pABCD[4]) \
	macro(CompactPoint, NoncePub) \

#define BeamCrypto_ProtoResponse_CreateShieldedInput_2(macro) \
	macro(CompactPoint, NoncePub) \
	macro(UintBig, SigG) \

#define BeamCrypto_ProtoRequest_CreateShieldedInput_3(macro) \
	macro(uint8_t, NumPoints) \
	/* followed by CompactPoint* pG[] */

#define BeamCrypto_ProtoResponse_CreateShieldedInput_3(macro)

#define BeamCrypto_ProtoRequest_CreateShieldedInput_4(macro) \
	/* followed by CompactPoint* pG[] */

#define BeamCrypto_ProtoResponse_CreateShieldedInput_4(macro) \
	macro(CompactPoint, G_Last) \
	macro(UintBig, zR)

#define BeamCrypto_ProtoRequest_CreateShieldedVouchers(macro) \
	macro(uint32_t, Count) \
	macro(AddrID, AddrID) \
	macro(UintBig, Nonce0) \

#define BeamCrypto_ProtoResponse_CreateShieldedVouchers(macro) \
	macro(uint32_t, Count) \
	/* followed by ShieldedVoucher[] */

#define BeamCrypto_ProtoRequest_TxSplit(macro) \
	macro(TxCommonIn, Tx) \

#define BeamCrypto_ProtoResponse_TxSplit(macro) \
	macro(TxCommonOut, Tx) \

#define BeamCrypto_ProtoRequest_TxReceive(macro) \
	macro(TxCommonIn, Tx) \
	macro(TxMutualIn, Mut) \
	macro(TxKernelCommitments, Comms) \

#define BeamCrypto_ProtoResponse_TxReceive(macro) \
	macro(TxCommonOut, Tx) \
	macro(Signature, PaymentProof) \

#define BeamCrypto_ProtoRequest_TxSend1(macro) \
	macro(TxCommonIn, Tx) \
	macro(TxMutualIn, Mut) \
	macro(uint32_t, iSlot) \

#define BeamCrypto_ProtoResponse_TxSend1(macro) \
	macro(TxKernelCommitments, Comms) \
	macro(UintBig, UserAgreement) \

#define BeamCrypto_ProtoRequest_TxSend2(macro) \
	BeamCrypto_ProtoRequest_TxSend1(macro) \
	macro(TxKernelCommitments, Comms) \
	macro(UintBig, UserAgreement) \
	macro(Signature, PaymentProof) \

#define BeamCrypto_ProtoResponse_TxSend2(macro) \
	macro(TxSig, TxSig) \

#define BeamCrypto_ProtoRequest_TxPrepareShielded(macro) \
	macro(uint8_t, Size) \
	/* followed by blob */

#define BeamCrypto_ProtoResponse_TxPrepareShielded(macro)

#define BeamCrypto_ProtoRequest_TxSendShielded(macro) \
	macro(TxCommonIn, Tx) \
	macro(TxMutualIn, Mut) \
	macro(ShieldedTxoUser, User) \
	macro(CompactPoint, ptAssetGen) \
	macro(uint8_t, HideAssetAlways) /* important to specify, this affects expected blinding factor recovery */ \

#define BeamCrypto_ProtoResponse_TxSendShielded(macro) \
	macro(TxCommonOut, Tx) \

#define BeamCrypto_ProtoMethods(macro) \
	macro(0x01, Version) \
	macro(0x02, GetNumSlots) \
	macro(0x03, GetPKdf) \
	macro(0x04, GetImage) \
	macro(0x05, DisplayAddress) \
	macro(0x10, CreateOutput) \
	macro(0x18, TxAddCoins) \
	macro(0x1a, CreateShieldedInput_1) \
	macro(0x1b, CreateShieldedInput_2) \
	macro(0x1c, CreateShieldedInput_3) \
	macro(0x1d, CreateShieldedInput_4) \
	macro(0x22, CreateShieldedVouchers) \
	macro(0x30, TxSplit) \
	macro(0x31, TxReceive) \
	macro(0x32, TxSend1) \
	macro(0x33, TxSend2) \
	macro(0x35, TxPrepareShielded) \
	macro(0x36, TxSendShielded) \

// pIn/pOut don't have to be distinct! There may be overlap
// Alignment isn't guaranteed either
//
uint16_t KeyKeeper_Invoke(KeyKeeper*, uint8_t* pIn, uint32_t nIn, uint8_t* pOut, uint32_t* pOutSize);

//////////////////////////
// External functions, implemented by the platform-specific code
void SecureEraseMem(void*, uint32_t);
uint32_t KeyKeeper_getNumSlots(KeyKeeper*);
void KeyKeeper_ReadSlot(KeyKeeper*, uint32_t, UintBig*);
void KeyKeeper_RegenerateSlot(KeyKeeper*, uint32_t);
int KeyKeeper_AllowWeakInputs(KeyKeeper*);
Amount KeyKeeper_get_MaxShieldedFee(KeyKeeper*);

void KeyKeeper_DisplayAddress(KeyKeeper*, AddrID addrID, const UintBig* pPeerID);

//////////////////////////
// KeyKeeper - request user approval for spend
//
// pPeerID is NULL, if it's a Split tx (i.e. funds are transferred back to you, only the fee is spent).
// pKrnID is NULL, if this is a 'preliminary' confirmation (SendTx 1st invocation)
// pUser contains fee and min/max height (may be shown to the user)
uint16_t KeyKeeper_ConfirmSpend(KeyKeeper*, Amount val, AssetID aid, const UintBig* pPeerID, const TxKernelUser* pUser, const UintBig* pKrnID);
