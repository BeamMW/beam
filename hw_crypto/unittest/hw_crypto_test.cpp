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

#include <iostream>

extern "C" {
#include "multimac.h"
#include "oracle.h"
#include "noncegen.h"
#include "coinid.h"
#include "kdf.h"
#include "rangeproof.h"
#include "sign.h"
#include "keykeeper.h"
}

#include "../core/ecc_native.h"
#include "../core/block_crypt.h"

#include "../keykeeper/local_private_key_keeper.h"


using namespace beam;

extern "C"
{
	wallet::LocalPrivateKeyKeeperStd::State* g_pHwEmuNonces = nullptr;

	uint32_t BeamCrypto_KeyKeeper_getNumSlots() {
		return wallet::LocalPrivateKeyKeeperStd::s_Slots;
	}

	void BeamCrypto_KeyKeeper_ReadSlot(uint32_t iSlot, BeamCrypto_UintBig* p)
	{
		memcpy(p->m_pVal, g_pHwEmuNonces->get_AtReady(iSlot).m_pData, BeamCrypto_nBytes);
	}


	void BeamCrypto_KeyKeeper_RegenerateSlot(uint32_t iSlot)
	{
		g_pHwEmuNonces->Regenerate(iSlot);
	}
}


int g_TestsFailed = 0;

const Height g_hFork = 3; // whatever

void TestFailed(const char* szExpr, uint32_t nLine)
{
	printf("Test failed! Line=%u, Expression: %s\n", nLine, szExpr);
	g_TestsFailed++;
}

#define verify_test(x) \
	do { \
		if (!(x)) \
			TestFailed(#x, __LINE__); \
	} while (false)

void GenerateRandom(void* p, uint32_t n)
{
	for (uint32_t i = 0; i < n; i++)
		((uint8_t*) p)[i] = (uint8_t) rand();
}

template <uint32_t nBytes>
void SetRandom(uintBig_t<nBytes>& x)
{
	GenerateRandom(x.m_pData, x.nBytes);
}

void SetRandom(ECC::Scalar::Native& x)
{
	ECC::Scalar s;
	while (true)
	{
		SetRandom(s.m_Value);
		if (!x.Import(s))
			break;
	}
}

void SetRandom(ECC::Point::Native& value, uint8_t y = 0)
{
    ECC::Point p;

    SetRandom(p.m_X);
    p.m_Y = y;

    while (!value.Import(p))
    {
        verify_test(value == Zero);
        p.m_X.Inc();
    }
}

template <typename T>
void SetRandomOrd(T& x)
{
	GenerateRandom(&x, sizeof(x));
}

BeamCrypto_UintBig& Ecc2BC(const ECC::uintBig& x)
{
	static_assert(sizeof(x) == sizeof(BeamCrypto_UintBig));
	return (BeamCrypto_UintBig&) x;
}

BeamCrypto_CompactPoint& Ecc2BC(const ECC::Point& x)
{
	static_assert(sizeof(x) == sizeof(BeamCrypto_CompactPoint));
	return (BeamCrypto_CompactPoint&) x;
}

void BeamCrypto_InitGenSecure(BeamCrypto_MultiMac_Secure& x, const ECC::Point::Native& ptVal, const ECC::Point::Native& nums)
{
	ECC::Point::Compact::Converter cpc;
	ECC::Point::Native pt = nums;

	for (unsigned int i = 0; ; pt += ptVal)
	{
		assert(!(pt == Zero));
		cpc.set_Deferred(Cast::Up<ECC::Point::Compact>(x.m_pPt[i]), pt);
		if (++i == BeamCrypto_MultiMac_Secure_nCount)
			break;
	}

	pt = Zero;
	for (unsigned int iBit = BeamCrypto_nBits; iBit--; )
	{
		pt = pt * ECC::Two;

		if (!(iBit % BeamCrypto_MultiMac_Secure_nBits))
			pt += nums;
	}

	pt = -pt;
	cpc.set_Deferred(Cast::Up<ECC::Point::Compact>(x.m_pPt[BeamCrypto_MultiMac_Secure_nCount]), pt);
	cpc.Flush();
}

void BeamCrypto_InitFast(BeamCrypto_MultiMac_Fast& trg, const ECC::MultiMac::Prepared& p)
{
	const ECC::MultiMac::Prepared::Fast& src = p.m_Fast;

	static_assert(_countof(trg.m_pPt) <= _countof(src.m_pPt));

	for (uint32_t j = 0; j < _countof(trg.m_pPt); j++)
		trg.m_pPt[j] = src.m_pPt[j];
}

char Dig2Hex(uint8_t x)
{
	if (x < 0xa)
		return '0' + x;

	return x + 'a' - 0xa;
}

void BufferToHexTxt(std::string& s, const uint8_t* p, size_t n)
{
	std::ostringstream os;

	for (size_t i = 0; i < n; i++)
	{
		if (!(i & 15))
			os << "\n ";
		os
			<< "0x"
			<< Dig2Hex(p[i] >> 4)
			<< Dig2Hex(p[i] & 0xf)
			<< ',';
	}

	s = os.str();
}

void InitContext()
{
	printf("Init context...\n");

	BeamCrypto_Context* pCtx = BeamCrypto_Context_get();
	assert(pCtx);

	const ECC::Context& ctx = ECC::Context::get();

	ECC::Point::Native nums, pt;
	ctx.m_Ipp.m_GenDot_.m_Fast.m_pPt[0].Assign(nums, true); // whatever point, doesn't matter actually

	ctx.m_Ipp.G_.m_Fast.m_pPt[0].Assign(pt, true);
	BeamCrypto_InitGenSecure(pCtx->m_GenG, pt, nums);

	ctx.m_Ipp.J_.m_Fast.m_pPt[0].Assign(pt, true);
	BeamCrypto_InitGenSecure(pCtx->m_GenJ, pt, nums);

	static_assert(ECC::InnerProduct::nDim * 2 == BeamCrypto_MultiMac_Fast_Idx_H);

	for (uint32_t iGen = 0; iGen < ECC::InnerProduct::nDim * 2; iGen++)
		BeamCrypto_InitFast(pCtx->m_pGenFast[iGen], ECC::Context::get().m_Ipp.m_pGen_[0][iGen]);

	BeamCrypto_InitFast(pCtx->m_pGenFast[BeamCrypto_MultiMac_Fast_Idx_H], ECC::Context::get().m_Ipp.H_);
}


void TestMultiMac()
{
	printf("MultiMac...\n");

	ECC::Mode::Scope scope(ECC::Mode::Fast);

	uint32_t aa = sizeof(BeamCrypto_MultiMac_Secure);
	uint32_t bb = sizeof(BeamCrypto_MultiMac_Fast);
	uint32_t cc = sizeof(BeamCrypto_MultiMac_WNaf);
	aa;  bb; cc;

	const uint32_t nFast = 8;
	const uint32_t nSecure = 2;

	const uint32_t nBatch = nFast + nSecure;

	BeamCrypto_MultiMac_WNaf pWnaf[nFast];
	BeamCrypto_MultiMac_Scalar pFastS[nFast];

	BeamCrypto_MultiMac_Secure pGenSecure[nSecure];
	secp256k1_scalar pSecureS[nSecure];

	BeamCrypto_MultiMac_Context mmCtx;
	mmCtx.m_pZDenom = nullptr;
	mmCtx.m_Fast = nFast;
	mmCtx.m_Secure = nSecure;
	mmCtx.m_pGenFast = BeamCrypto_Context_get()->m_pGenFast;
	mmCtx.m_pS = pFastS;
	mmCtx.m_pWnaf = pWnaf;
	mmCtx.m_pGenSecure = pGenSecure;
	mmCtx.m_pSecureK = pSecureS;

	ECC::MultiMac_WithBufs<1, nBatch> mm1;

	for (uint32_t iGen = 0; iGen < nFast; iGen++)
	{
		const ECC::MultiMac::Prepared& p = ECC::Context::get().m_Ipp.m_pGen_[0][iGen];
		mm1.m_ppPrepared[iGen] = &p;
	}

	ECC::Point::Native ptVal, nums;
	ECC::Context::get().m_Ipp.m_GenDot_.m_Fast.m_pPt[0].Assign(nums, true); // whatever point, doesn't matter actually

	for (uint32_t iGen = 0; iGen < nSecure; iGen++)
	{
		const ECC::MultiMac::Prepared& p = ECC::Context::get().m_Ipp.m_pGen_[0][nFast + iGen];
		mm1.m_ppPrepared[nFast + iGen] = &p;

		p.m_Fast.m_pPt[0].Assign(ptVal, true);

		BeamCrypto_InitGenSecure(pGenSecure[iGen], ptVal, nums);
	}


	for (int i = 0; i < 10; i++)
	{
		mm1.Reset();

		for (uint32_t iPt = 0; iPt < nBatch; iPt++)
		{
			ECC::Scalar::Native sk;
			SetRandom(sk);

			mm1.m_pKPrep[iPt] = sk;
			mm1.m_Prepared++;

			if (iPt < nFast)
				pFastS[iPt].m_pK[0] = sk.get();
			else
				pSecureS[iPt - nFast] = sk.get();
		}

		ECC::Point::Native res1, res2;
		mm1.Calculate(res1);

		mmCtx.m_pRes = &res2.get_Raw();
		BeamCrypto_MultiMac_Calculate(&mmCtx);

		verify_test(res1 == res2);
	}
}

void TestNonceGen()
{
	printf("NonceGen...\n");

	static const char szSalt[] = "my_salt";

	for (int i = 0; i < 3; i++)
	{
		ECC::Hash::Value seed;
		SetRandom(seed);

		ECC::NonceGenerator ng1(szSalt);
		ng1 << seed;

		BeamCrypto_NonceGenerator ng2;
		BeamCrypto_NonceGenerator_Init(&ng2, szSalt, sizeof(szSalt), &Ecc2BC(seed));

		for (int j = 0; j < 10; j++)
		{
			ECC::Scalar::Native sk1, sk2;
			ng1 >> sk1;
			BeamCrypto_NonceGenerator_NextScalar(&ng2, &sk2.get_Raw());

			verify_test(sk1 == sk2);
		}
	}
}

void TestOracle()
{
	printf("Oracle...\n");

	for (int i = 0; i < 3; i++)
	{
		ECC::Oracle o1;
		BeamCrypto_Oracle o2;
		BeamCrypto_Oracle_Init(&o2);

		for (int j = 0; j < 4; j++)
		{
			for (int k = 0; k < 3; k++)
			{
				ECC::Scalar::Native sk1, sk2;
				o1 >> sk1;
				BeamCrypto_Oracle_NextScalar(&o2, &sk2.get_Raw());

				verify_test(sk1 == sk2);
			}

			ECC::Hash::Value val;
			SetRandom(val);

			o1 << val;
			BeamCrypto_Oracle_Expose(&o2, val.m_pData, val.nBytes);
		}
	}
}

void TestCoin(const CoinID& cid, Key::IKdf& kdf, const BeamCrypto_Kdf& kdf2)
{
	ECC::Hash::Value hv1, hv2;
	cid.get_Hash(hv1);

	BeamCrypto_CoinID cid2;
	cid2.m_Idx = cid.m_Idx;
	cid2.m_Type = cid.m_Type;
	cid2.m_SubIdx = cid.m_SubIdx;
	cid2.m_Amount = cid.m_Value;
	cid2.m_AssetID = cid.m_AssetID;

	BeamCrypto_CoinID_getHash(&cid2, &Ecc2BC(hv2));

	verify_test(hv1 == hv2);

	uint8_t nScheme;
	uint32_t nSubKey;
	bool bChildKdf2 = !!BeamCrypto_CoinID_getSchemeAndSubkey(&cid2, &nScheme, &nSubKey);

	verify_test(cid.get_Scheme() == nScheme);

	uint32_t iChild;
	bool bChildKdf = cid.get_ChildKdfIndex(iChild);
	verify_test(bChildKdf == bChildKdf2);

	if (bChildKdf) {
		verify_test(nSubKey == iChild);
	}

	// keys and commitment
	ECC::Scalar::Native sk1, sk2;
	ECC::Point comm1, comm2;

	ECC::Key::IKdf* pChildKdf = &kdf;
	ECC::HKdf hkdfC;

	if (bChildKdf)
	{
		hkdfC.GenerateChild(kdf, iChild);
		pChildKdf = &hkdfC;
	}

	CoinID::Worker(cid).Create(sk1, comm1, *pChildKdf);

	BeamCrypto_FlexPoint fp;
	BeamCrypto_CoinID_getSkComm(&kdf2, &cid2, &sk2.get_Raw(), &fp);

	BeamCrypto_FlexPoint_MakeCompact(&fp);
	Ecc2BC(comm2) = fp.m_Compact;

	verify_test(sk1 == sk2);
	verify_test(comm1 == comm2);

	if (CoinID::Scheme::V1 != nScheme)
		return;

	// Generate multi-party output

	Output outp;
	outp.m_Commitment = comm1;

	ECC::HKdf kdfDummy;
	ECC::Scalar::Native skDummy;
	outp.Create(g_hFork, skDummy, kdfDummy, cid, kdf, Output::OpCode::Mpc_1); // Phase 1
	assert(outp.m_pConfidential);

	BeamCrypto_RangeProof rp;
	rp.m_pKdf = &kdf2;
	rp.m_Cid = cid2;
	rp.m_pT[0] = Ecc2BC(outp.m_pConfidential->m_Part2.m_T1);
	rp.m_pT[1] = Ecc2BC(outp.m_pConfidential->m_Part2.m_T2);
	rp.m_pKExtra = nullptr;
	ZeroObject(rp.m_TauX);

	verify_test(BeamCrypto_RangeProof_Calculate(&rp)); // Phase 2

	Ecc2BC(outp.m_pConfidential->m_Part2.m_T1) = rp.m_pT[0];
	Ecc2BC(outp.m_pConfidential->m_Part2.m_T2) = rp.m_pT[1];

	ECC::Scalar::Native tauX;
	tauX.get_Raw() = rp.m_TauX;
	outp.m_pConfidential->m_Part3.m_TauX = tauX;

	outp.Create(g_hFork, skDummy, kdfDummy, cid, kdf, Output::OpCode::Mpc_2); // Phase 3

	ECC::Point::Native comm;
	verify_test(outp.IsValid(g_hFork, comm));

	CoinID cid3;
	verify_test(outp.Recover(g_hFork, kdf, cid3));

	verify_test(cid == cid3);

}

void TestCoins()
{
	printf("Utxo key derivation and rangeproof...\n");

	ECC::HKdf hkdf;
	BeamCrypto_Kdf kdf2;

	ECC::Hash::Value hv;
	SetRandom(hv);

	hkdf.Generate(hv);
	BeamCrypto_Kdf_Init(&kdf2, &Ecc2BC(hv));

	for (int i = 0; i < 3; i++)
	{
		CoinID cid;
		SetRandomOrd(cid.m_Idx);
		SetRandomOrd(cid.m_Type);
		SetRandomOrd(cid.m_Value);

		for (int iAsset = 0; iAsset < 2; iAsset++)
		{
			if (iAsset)
				SetRandomOrd(cid.m_AssetID);
			else
				cid.m_AssetID = 0;

			for (int iCh = 0; iCh < 2; iCh++)
			{
				uint32_t iChild;
				if (iCh)
				{
					SetRandomOrd(iChild);
					iChild &= (1U << 24) - 1;
				}
				else
					iChild = 0;

				cid.set_Subkey(iChild);
				TestCoin(cid, hkdf, kdf2);

				cid.set_Subkey(iChild, CoinID::Scheme::V0);
				TestCoin(cid, hkdf, kdf2);

				cid.set_Subkey(iChild, CoinID::Scheme::BB21);
				TestCoin(cid, hkdf, kdf2);
			}
		}
	}
}

void TestKdf()
{
	printf("Key derivation...\n");

	ECC::HKdf hkdf;
	BeamCrypto_Kdf kdf2;

	for (int i = 0; i < 3; i++)
	{
		ECC::Hash::Value hv;
		SetRandom(hv);

		if (i)
		{
			uint32_t iChild;
			SetRandomOrd(iChild);

			hkdf.GenerateChild(hkdf, iChild);
			BeamCrypto_Kdf_getChild(&kdf2, iChild, &kdf2);
		}
		else
		{
			hkdf.Generate(hv);
			BeamCrypto_Kdf_Init(&kdf2, &Ecc2BC(hv));
		}

		for (int j = 0; j < 5; j++)
		{
			SetRandom(hv);

			ECC::Scalar::Native sk1, sk2;

			hkdf.DerivePKey(sk1, hv);
			BeamCrypto_Kdf_Derive_PKey(&kdf2, &Ecc2BC(hv), &sk2.get_Raw());
			verify_test(sk1 == sk2);

			hkdf.DeriveKey(sk1, hv);
			BeamCrypto_Kdf_Derive_SKey(&kdf2, &Ecc2BC(hv), &sk2.get_Raw());
			verify_test(sk1 == sk2);
		}
	}
}

void TestSignature()
{
	printf("Signatures...\n");

	for (int i = 0; i < 5; i++)
	{
		ECC::Hash::Value msg;
		ECC::Scalar::Native sk;
		SetRandom(msg);
		SetRandom(sk);

		ECC::Point::Native pkN = ECC::Context::get().G * sk;
		ECC::Point pk = pkN;

		BeamCrypto_Signature sig2;
		BeamCrypto_Signature_Sign(&sig2, &Ecc2BC(msg), &sk.get_Raw());

		BeamCrypto_FlexPoint fp;
		fp.m_Compact = Ecc2BC(pk);
		fp.m_Flags = BeamCrypto_FlexPoint_Compact;

		verify_test(BeamCrypto_Signature_IsValid(&sig2, &Ecc2BC(msg), &fp));

		ECC::Signature sig1;
		Ecc2BC(sig1.m_NoncePub) = sig2.m_NoncePub;
		Ecc2BC(sig1.m_k.m_Value) = sig2.m_k;

		verify_test(sig1.IsValid(msg, pkN));

		// tamper with sig
		sig2.m_k.m_pVal[0] ^= 12;
		verify_test(!BeamCrypto_Signature_IsValid(&sig2, &Ecc2BC(msg), &fp));
	}
}

void TestKrn()
{
	printf("Tx Kernels...\n");

	for (int i = 0; i < 3; i++)
	{
		TxKernelStd krn1;
		SetRandomOrd(krn1.m_Fee);
		SetRandomOrd(krn1.m_Height.m_Min);
		SetRandomOrd(krn1.m_Height.m_Max);
		std::setmax(krn1.m_Height.m_Max, krn1.m_Height.m_Min);

		ECC::Scalar::Native sk;
		SetRandom(sk);
		krn1.Sign(sk);

		BeamCrypto_TxKernel krn2;
		krn2.m_Fee = krn1.m_Fee;
		krn2.m_hMin = krn1.m_Height.m_Min;
		krn2.m_hMax = krn1.m_Height.m_Max;
		krn2.m_Commitment = Ecc2BC(krn1.m_Commitment);
		krn2.m_Signature.m_k = Ecc2BC(krn1.m_Signature.m_k.m_Value);
		krn2.m_Signature.m_NoncePub = Ecc2BC(krn1.m_Signature.m_NoncePub);

		verify_test(BeamCrypto_TxKernel_IsValid(&krn2));

		ECC::Hash::Value msg;
		BeamCrypto_TxKernel_getID(&krn2, &Ecc2BC(msg));
		verify_test(msg == krn1.m_Internal.m_ID);

		// tamper
		krn2.m_Fee++;
		verify_test(!BeamCrypto_TxKernel_IsValid(&krn2));
	}
}

void TestPKdfExport()
{
	printf("PKdf export...\n");

	for (int i = 0; i < 3; i++)
	{
		ECC::Hash::Value hv;
		SetRandom(hv);

		ECC::HKdf hkdf;
		hkdf.Generate(hv);

		BeamCrypto_KeyKeeper kk;
		BeamCrypto_Kdf_Init(&kk.m_MasterKey, &Ecc2BC(hv));

		for (int j = 0; j < 3; j++)
		{
			ECC::HKdf hkdfChild;
			ECC::HKdf* pKdf1 = &hkdfChild;

			BeamCrypto_KdfPub pkdf2;

			if (j)
			{
				uint32_t iChild;
				SetRandomOrd(iChild);

				BeamCrypto_KeyKeeper_GetPKdf(&kk, &pkdf2, &iChild);
				hkdfChild.GenerateChild(hkdf, iChild);
			}
			else
			{
				pKdf1 = &hkdf;
				BeamCrypto_KeyKeeper_GetPKdf(&kk, &pkdf2, nullptr);
			}

			ECC::HKdfPub::Packed p;
			Ecc2BC(p.m_Secret) = pkdf2.m_Secret;
			Ecc2BC(p.m_PkG) = pkdf2.m_CoFactorG;
			Ecc2BC(p.m_PkJ) = pkdf2.m_CoFactorJ;

			ECC::HKdfPub hkdf2;
			verify_test(hkdf2.Import(p));

			// Check the match between directly derived pKdf1 and imported hkdf2
			// Key::IPKdf::IsSame() is not a comprehensive test, it won't check point images (too expensive)

			ECC::Point::Native pt1, pt2;
			pKdf1->DerivePKeyG(pt1, hv);
			hkdf2.DerivePKeyG(pt2, hv);
			verify_test(pt1 == pt2);

			pKdf1->DerivePKeyJ(pt1, hv);
			hkdf2.DerivePKeyJ(pt2, hv);
			verify_test(pt1 == pt2);
		}
	}
}


struct KeyKeeperHwEmu
	:public wallet::PrivateKeyKeeper_AsyncNotify
{
	BeamCrypto_KeyKeeper m_Ctx;
	Key::IPKdf::Ptr m_pOwnerKey; // cached

	bool get_PKdf(Key::IPKdf::Ptr&, const uint32_t*);
	bool get_OwnerKey();

	static void CidCvt(BeamCrypto_CoinID&, const CoinID&);

	static void TxImport(BeamCrypto_TxCommon&, const Method::TxCommon&, std::vector<BeamCrypto_CoinID>&);
	static void TxExport(Method::TxCommon&, const BeamCrypto_TxCommon&);


#define THE_MACRO(method) \
        virtual Status::Type InvokeSync(Method::method& m) override;

	KEY_KEEPER_METHODS(THE_MACRO)
#undef THE_MACRO
};

bool KeyKeeperHwEmu::get_PKdf(Key::IPKdf::Ptr& pRes, const uint32_t* pChild)
{
	BeamCrypto_KdfPub pkdf;
	BeamCrypto_KeyKeeper_GetPKdf(&m_Ctx, &pkdf, pChild);

	ECC::HKdfPub::Packed p;
	Ecc2BC(p.m_Secret) = pkdf.m_Secret;
	Ecc2BC(p.m_PkG) = pkdf.m_CoFactorG;
	Ecc2BC(p.m_PkJ) = pkdf.m_CoFactorJ;

	pRes = std::make_unique<ECC::HKdfPub>();

	if (Cast::Up<ECC::HKdfPub>(*pRes).Import(p))
		return true;

	pRes.reset();
	return false;
}

bool KeyKeeperHwEmu::get_OwnerKey()
{
	return m_pOwnerKey || get_PKdf(m_pOwnerKey, nullptr);
}

KeyKeeperHwEmu::Status::Type KeyKeeperHwEmu::InvokeSync(Method::get_Kdf& m)
{
	if (m.m_Root)
	{
		get_OwnerKey();
		m.m_pPKdf = m_pOwnerKey;
	}
	else
		get_PKdf(m.m_pPKdf, &m.m_iChild);

	return m.m_pPKdf ? Status::Success : Status::Unspecified;
}

KeyKeeperHwEmu::Status::Type KeyKeeperHwEmu::InvokeSync(Method::get_NumSlots& m)
{
	m.m_Count = 64;
	return Status::Success;
}

void KeyKeeperHwEmu::CidCvt(BeamCrypto_CoinID& cid2, const CoinID& cid)
{
	cid2.m_Idx = cid.m_Idx;
	cid2.m_SubIdx = cid.m_SubIdx;
	cid2.m_Type = cid.m_Type;
	cid2.m_AssetID = cid.m_AssetID;
	cid2.m_Amount = cid.m_Value;
}

KeyKeeperHwEmu::Status::Type KeyKeeperHwEmu::InvokeSync(Method::CreateOutput& m)
{
	if (m.m_hScheme < Rules::get().pForks[1].m_Height)
		return Status::NotImplemented;

	if (!get_OwnerKey())
		return Status::Unspecified;

	Output::Ptr pOutp = std::make_unique<Output>();

	ECC::Point::Native comm;
	get_Commitment(comm, m.m_Cid);
	pOutp->m_Commitment = comm;

	// rangeproof
	ECC::Scalar::Native skDummy;
	ECC::HKdf kdfDummy;

	pOutp->Create(g_hFork, skDummy, kdfDummy, m.m_Cid, *m_pOwnerKey, Output::OpCode::Mpc_1);
	assert(pOutp->m_pConfidential);

	BeamCrypto_RangeProof rp;
	rp.m_pKdf = &m_Ctx.m_MasterKey;
	CidCvt(rp.m_Cid, m.m_Cid);

	rp.m_pT[0] = Ecc2BC(pOutp->m_pConfidential->m_Part2.m_T1);
	rp.m_pT[1] = Ecc2BC(pOutp->m_pConfidential->m_Part2.m_T2);
	rp.m_pKExtra = nullptr;
	ZeroObject(rp.m_TauX);

	if (!BeamCrypto_RangeProof_Calculate(&rp)) // Phase 2
		return Status::Unspecified;

	Ecc2BC(pOutp->m_pConfidential->m_Part2.m_T1) = rp.m_pT[0];
	Ecc2BC(pOutp->m_pConfidential->m_Part2.m_T2) = rp.m_pT[1];

	ECC::Scalar::Native tauX;
	tauX.get_Raw() = rp.m_TauX;
	pOutp->m_pConfidential->m_Part3.m_TauX = tauX;

	pOutp->Create(g_hFork, skDummy, kdfDummy, m.m_Cid, *m_pOwnerKey, Output::OpCode::Mpc_2); // Phase 3

	m.m_pResult.swap(pOutp);

	return Status::Success;
}

KeyKeeperHwEmu::Status::Type KeyKeeperHwEmu::InvokeSync(Method::SignReceiver& m)
{
	std::vector<BeamCrypto_CoinID> vCvt;
	BeamCrypto_TxCommon tx2;
	TxImport(tx2, m, vCvt);

	BeamCrypto_TxMutualInfo txMut;
	txMut.m_MyIDKey = m.m_MyIDKey;
	txMut.m_Peer = Ecc2BC(m.m_Peer);

	int nRet = BeamCrypto_KeyKeeper_SignTx_Receive(&m_Ctx, &tx2, &txMut);

	if (BeamCrypto_KeyKeeper_Status_Ok == nRet)
	{
		TxExport(m, tx2);

		Ecc2BC(m.m_PaymentProofSignature.m_NoncePub) = txMut.m_PaymentProofSignature.m_NoncePub;
		Ecc2BC(m.m_PaymentProofSignature.m_k.m_Value) = txMut.m_PaymentProofSignature.m_k;
	}

	return static_cast<Status::Type>(nRet);
}

KeyKeeperHwEmu::Status::Type KeyKeeperHwEmu::InvokeSync(Method::SignSender& m)
{
	std::vector<BeamCrypto_CoinID> vCvt;
	BeamCrypto_TxCommon tx2;
	TxImport(tx2, m, vCvt);

	BeamCrypto_TxMutualInfo txMut;
	txMut.m_MyIDKey = m.m_MyIDKey;
	txMut.m_Peer = Ecc2BC(m.m_Peer);
	txMut.m_PaymentProofSignature.m_NoncePub = Ecc2BC(m.m_PaymentProofSignature.m_NoncePub);
	txMut.m_PaymentProofSignature.m_k = Ecc2BC(m.m_PaymentProofSignature.m_k.m_Value);

	BeamCrypto_TxSenderParams txSnd;
	txSnd.m_iSlot = m.m_Slot;
	txSnd.m_UserAgreement = Ecc2BC(m.m_UserAgreement);

	int nRet = BeamCrypto_KeyKeeper_SignTx_Send(&m_Ctx, &tx2, &txMut, &txSnd);

	if (BeamCrypto_KeyKeeper_Status_Ok == nRet)
	{
		TxExport(m, tx2);
		Ecc2BC(m.m_UserAgreement) = txSnd.m_UserAgreement;
	}

	return static_cast<Status::Type>(nRet);
}

KeyKeeperHwEmu::Status::Type KeyKeeperHwEmu::InvokeSync(Method::SignSplit& m)
{
	std::vector<BeamCrypto_CoinID> vCvt;
	BeamCrypto_TxCommon tx2;
	TxImport(tx2, m, vCvt);

	int nRet = BeamCrypto_KeyKeeper_SignTx_Split(&m_Ctx, &tx2);

	if (BeamCrypto_KeyKeeper_Status_Ok == nRet)
		TxExport(m, tx2);

	return static_cast<Status::Type>(nRet);
}

void KeyKeeperHwEmu::TxImport(BeamCrypto_TxCommon& tx2, const Method::TxCommon& m, std::vector<BeamCrypto_CoinID>& v)
{
	tx2.m_Ins = static_cast<unsigned int>(m.m_vInputs.size());
	tx2.m_Outs = static_cast<unsigned int>(m.m_vOutputs.size());

	v.resize(tx2.m_Ins + tx2.m_Outs);
	if (!v.empty())
	{
		for (size_t i = 0; i < v.size(); i++)
			CidCvt(v[i], (i < tx2.m_Ins) ? m.m_vInputs[i] : m.m_vOutputs[i - tx2.m_Ins]);

		tx2.m_pIns = &v.front();
		tx2.m_pOuts = &v.front() + tx2.m_Ins;
	}

	// kernel
	assert(m.m_pKernel);
	tx2.m_Krn.m_Fee = m.m_pKernel->m_Fee;
	tx2.m_Krn.m_hMin = m.m_pKernel->m_Height.m_Min;
	tx2.m_Krn.m_hMax = m.m_pKernel->m_Height.m_Max;

	tx2.m_Krn.m_Commitment = Ecc2BC(m.m_pKernel->m_Commitment);
	tx2.m_Krn.m_Signature.m_NoncePub = Ecc2BC(m.m_pKernel->m_Signature.m_NoncePub);
	tx2.m_Krn.m_Signature.m_k = Ecc2BC(m.m_pKernel->m_Signature.m_k.m_Value);

	// offset
	ECC::Scalar kOffs(m.m_kOffset);
	tx2.m_kOffset = Ecc2BC(kOffs.m_Value);
}

void KeyKeeperHwEmu::TxExport(Method::TxCommon& m, const BeamCrypto_TxCommon& tx2)
{
	// kernel
	assert(m.m_pKernel);
	m.m_pKernel->m_Fee = tx2.m_Krn.m_Fee;
	m.m_pKernel->m_Height.m_Min = tx2.m_Krn.m_hMin;
	m.m_pKernel->m_Height.m_Max = tx2.m_Krn.m_hMax;

	Ecc2BC(m.m_pKernel->m_Commitment) = tx2.m_Krn.m_Commitment;
	Ecc2BC(m.m_pKernel->m_Signature.m_NoncePub) = tx2.m_Krn.m_Signature.m_NoncePub;
	Ecc2BC(m.m_pKernel->m_Signature.m_k.m_Value) = tx2.m_Krn.m_Signature.m_k;

	m.m_pKernel->UpdateID();

	// offset
	ECC::Scalar kOffs;
	Ecc2BC(kOffs.m_Value) = tx2.m_kOffset;
	m.m_kOffset = kOffs;
}


struct KeyKeeperStd
	:public wallet::LocalPrivateKeyKeeperStd
{
	bool m_Trustless = true;

	using LocalPrivateKeyKeeperStd::LocalPrivateKeyKeeperStd;
	virtual bool IsTrustless() override { return m_Trustless; }
};



struct KeyKeeperWrap
{
	KeyKeeperHwEmu m_kkEmu;
	KeyKeeperStd m_kkStd; // for comparison

	wallet::LocalPrivateKeyKeeperStd::State m_Nonces;

	static Key::IKdf::Ptr get_KdfFromSeed(const ECC::Hash::Value& hv)
	{
		Key::IKdf::Ptr pKdf;
		ECC::HKdf::Create(pKdf, hv);
		return pKdf;
	}

	KeyKeeperWrap(const ECC::Hash::Value& hv)
		:m_kkStd(get_KdfFromSeed(hv))
	{
		SetRandom(m_kkStd.m_State.m_hvLast);

		m_kkEmu.m_Ctx.m_AllowWeakInputs = 0;
		BeamCrypto_Kdf_Init(&m_kkEmu.m_Ctx.m_MasterKey, &Ecc2BC(hv));

		m_Nonces.m_hvLast = m_kkStd.m_State.m_hvLast;
	}

	static CoinID& Add(std::vector<CoinID>& vec, Amount val = 0);

	void ExportTx(Transaction& tx, const wallet::IPrivateKeyKeeper2::Method::TxCommon& tx2);
	void TestTx(const wallet::IPrivateKeyKeeper2::Method::TxCommon& tx2);

	void TestSplit();
	void TestRcv();
	void TestSend1();

	void get_WalletID(PeerID& pid, wallet::WalletIDKey nKeyID)
	{
		ECC::Hash::Value hv;
		Key::ID(nKeyID, Key::Type::WalletID).get_Hash(hv);

		ECC::Point::Native ptN;
		verify_test(m_kkEmu.get_OwnerKey());
		m_kkEmu.m_pOwnerKey->DerivePKeyG(ptN, hv);

		pid = ECC::Point(ptN).m_X;
	}

	static void GetPaymentConfirmationStatsVec(wallet::PaymentConfirmation& pc, const std::vector<CoinID>& vec)
	{
		for (size_t i = 0; i < vec.size(); i++)
		{
			const CoinID& cid = vec[i];
			if (pc.m_AssetID != cid.m_AssetID)
			{
				pc.m_AssetID = cid.m_AssetID;
				pc.m_Value = 0;
			}
			pc.m_Value += cid.m_Value;
		}
	}

	static void NegateUns(Amount& v)
	{
		// v = -v
		v = (v ^ static_cast<Amount>(-1)) + 1;
	}

	void GetPaymentConfirmationStats(wallet::PaymentConfirmation& pc, const wallet::IPrivateKeyKeeper2::Method::TxCommon& tx, bool bSending)
	{
		// Reconstruct the value and asset of the actual tx. This is (roughly) a duplication of logic within the KeyKeeper

		pc.m_Value = 0;
		pc.m_AssetID = 0;

		GetPaymentConfirmationStatsVec(pc, tx.m_vOutputs);
		NegateUns(pc.m_Value);
		GetPaymentConfirmationStatsVec(pc, tx.m_vInputs);

		if (!bSending)
			NegateUns(pc.m_Value);
	}

	void UpdateMethod(KeyKeeperHwEmu::Method::TxCommon& dst, KeyKeeperHwEmu::Method::TxCommon& src, int nPhase)
	{
		switch (nPhase)
		{
		case 0: // save
			{
				dst.m_kOffset = src.m_kOffset;
				TxKernel::Ptr pKrn;
				src.m_pKernel->Clone(pKrn);
				dst.m_pKernel.reset(Cast::Up<TxKernelStd>(pKrn.release()));
			}
			break;

		case 1: // swap
			std::swap(dst.m_kOffset, src.m_kOffset);
			dst.m_pKernel.swap(src.m_pKernel);
			break;

		default: // compare
			verify_test(dst.m_kOffset == src.m_kOffset);
			verify_test(dst.m_pKernel->m_Internal.m_ID == src.m_pKernel->m_Internal.m_ID);
			verify_test(dst.m_pKernel->m_Signature == src.m_pKernel->m_Signature);
		}
	}

	void UpdateMethod(KeyKeeperHwEmu::Method::SignReceiver& dst, KeyKeeperHwEmu::Method::SignReceiver& src, int nPhase)
	{
		UpdateMethod(Cast::Down<KeyKeeperHwEmu::Method::TxCommon>(dst), Cast::Down<KeyKeeperHwEmu::Method::TxCommon>(src), nPhase);

		switch (nPhase)
		{
		case 0: // save
			// ignore, initially it's just garbage
			break;

		case 1: // swap
			dst.m_PaymentProofSignature = src.m_PaymentProofSignature;
			break;

		default: // compare signatures
			// Right now we can't mem-compare them, because standard signature on host uses system random (in addition to NonceGen)
			// So we'll only compare that both signatures are good for the expected pubkey
			{
				PeerID pid;
				get_WalletID(pid, src.m_MyIDKey);

				wallet::PaymentConfirmation pc;
				pc.m_KernelID = src.m_pKernel->m_Internal.m_ID;
				pc.m_Sender = src.m_Peer;
				GetPaymentConfirmationStats(pc, src, false);

				pc.m_Signature = dst.m_PaymentProofSignature;
				verify_test(pc.IsValid(pid));

				pc.m_Signature = src.m_PaymentProofSignature;
				verify_test(pc.IsValid(pid));
			}
			break;
		}
	}

	void UpdateMethod(KeyKeeperHwEmu::Method::SignSender& dst, KeyKeeperHwEmu::Method::SignSender& src, int nPhase)
	{
		switch (nPhase)
		{
		case 0: // save
			dst.m_UserAgreement = src.m_UserAgreement;
			break;

		case 1: // swap
			std::swap(dst.m_UserAgreement, src.m_UserAgreement);
			break;

		default: // compare
			verify_test(dst.m_UserAgreement == src.m_UserAgreement);
			// kernel is not necessarily updated (it's not finalized on the 1st invocation
			dst.m_pKernel->UpdateID();
			src.m_pKernel->UpdateID();
		}

		UpdateMethod(Cast::Down<KeyKeeperHwEmu::Method::TxCommon>(dst), Cast::Down<KeyKeeperHwEmu::Method::TxCommon>(src), nPhase);
	}

	template <typename TMethod>
	int InvokeOnBoth(TMethod& m)
	{
		TMethod m2;
		UpdateMethod(m2, m, 0);

		g_pHwEmuNonces = &m_Nonces;
		int n1 = m_kkEmu.InvokeSync(m);
		g_pHwEmuNonces = nullptr;

		UpdateMethod(m2, m, 1);

		int n2 = m_kkStd.InvokeSync(m);

		verify_test(n1 == n2);

		if (KeyKeeperHwEmu::Status::Success == n1)
			UpdateMethod(m2, m, 2);

		return n1;
	}
};

CoinID& KeyKeeperWrap::Add(std::vector<CoinID>& vec, Amount val)
{
	CoinID& ret = vec.emplace_back();

	uint64_t nIdx;
	SetRandomOrd(nIdx);

	ret = CoinID(val, nIdx, Key::Type::Regular);
	return ret;
}

void KeyKeeperWrap::ExportTx(Transaction& tx, const wallet::IPrivateKeyKeeper2::Method::TxCommon& tx2)
{
	tx.m_vInputs.reserve(tx.m_vInputs.size() + tx2.m_vInputs.size());

	ECC::Point::Native pt;

	for (unsigned int i = 0; i < tx2.m_vInputs.size(); i++)
	{
		Input::Ptr& pInp = tx.m_vInputs.emplace_back();
		pInp = std::make_unique<Input>();

		m_kkEmu.get_Commitment(pt, tx2.m_vInputs[i]);
		pInp->m_Commitment = pt;
	}

	tx.m_vOutputs.reserve(tx.m_vOutputs.size() + tx2.m_vOutputs.size());

	for (unsigned int i = 0; i < tx2.m_vOutputs.size(); i++)
	{
		KeyKeeperHwEmu::Method::CreateOutput m;
		m.m_hScheme = g_hFork;
		m.m_Cid = tx2.m_vOutputs[i];
		
		verify_test(m_kkEmu.InvokeSync(m) == KeyKeeperHwEmu::Status::Success);
		assert(m.m_pResult);

		tx.m_vOutputs.emplace_back().swap(m.m_pResult);
	}


	// kernel
	if (tx2.m_pKernel)
		tx2.m_pKernel->Clone(tx.m_vKernels.emplace_back());

	// offset
	tx.m_Offset = tx2.m_kOffset;

	tx.Normalize();
}

void KeyKeeperWrap::TestTx(const wallet::IPrivateKeyKeeper2::Method::TxCommon& tx2)
{
	Transaction tx;
	ExportTx(tx, tx2);

	Transaction::Context::Params pars;
	Transaction::Context ctx(pars);
	ctx.m_Height.m_Min = g_hFork;
	verify_test(tx.IsValid(ctx));
}

void KeyKeeperWrap::TestSplit()
{
	wallet::IPrivateKeyKeeper2::Method::SignSplit m;

	Add(m.m_vInputs, 55);
	Add(m.m_vInputs, 16);

	Add(m.m_vOutputs, 12);
	Add(m.m_vOutputs, 13);
	Add(m.m_vOutputs, 14);

	m.m_pKernel = std::make_unique<TxKernelStd>();
	m.m_pKernel->m_Height.m_Min = g_hFork;
	m.m_pKernel->m_Height.m_Max = g_hFork + 40;
	m.m_pKernel->m_Fee = 30; // Incorrect balance (funds missing)

	verify_test(InvokeOnBoth(m) != KeyKeeperHwEmu::Status::Success);

	m.m_pKernel->m_Fee = 32; // ok
	
	m.m_vOutputs[0].set_Subkey(0, CoinID::Scheme::V0); // weak output scheme
	verify_test(InvokeOnBoth(m) != KeyKeeperHwEmu::Status::Success);

	m.m_vOutputs[0].set_Subkey(0, CoinID::Scheme::BB21); // weak output scheme
	verify_test(InvokeOnBoth(m) != KeyKeeperHwEmu::Status::Success);

	m.m_vOutputs[0].set_Subkey(12); // outputs to a child key
	verify_test(InvokeOnBoth(m) != KeyKeeperHwEmu::Status::Success);

	m.m_vOutputs[0].set_Subkey(0); // ok

	m.m_vInputs[0].set_Subkey(14, CoinID::Scheme::V0); // weak input scheme
	verify_test(InvokeOnBoth(m) != KeyKeeperHwEmu::Status::Success);

	m_kkEmu.m_Ctx.m_AllowWeakInputs = 1;
	m_kkStd.m_Trustless = false; // no explicit flag for weak inputs, just switch to trusted mode
	verify_test(InvokeOnBoth(m) == KeyKeeperHwEmu::Status::Success); // should work now

	TestTx(m);

	// add asset
	Add(m.m_vInputs, 16).m_AssetID = 12;
	Add(m.m_vOutputs, 16).m_AssetID = 13;

	verify_test(InvokeOnBoth(m) != KeyKeeperHwEmu::Status::Success); // different assets mixed (not allowed)

	m.m_vOutputs.back().m_AssetID = 12;
	m.m_vOutputs.back().m_Value = 15;
	verify_test(InvokeOnBoth(m) != KeyKeeperHwEmu::Status::Success); // asset balance mismatch

	m.m_vOutputs.back().m_Value = 16;
	m.m_kOffset = Zero; // m_kkStd assumes it's 0-initialized
	verify_test(InvokeOnBoth(m) == KeyKeeperHwEmu::Status::Success); // ok

	TestTx(m);

	m_kkEmu.m_Ctx.m_AllowWeakInputs = 0;
	m_kkStd.m_Trustless = true;
}

void KeyKeeperWrap::TestRcv()
{
	wallet::IPrivateKeyKeeper2::Method::SignReceiver m;

	Add(m.m_vInputs, 20);
	Add(m.m_vInputs, 30);

	Add(m.m_vOutputs, 40);

	m.m_pKernel = std::make_unique<TxKernelStd>();
	m.m_pKernel->m_Height.m_Min = g_hFork;
	m.m_pKernel->m_Height.m_Max = g_hFork + 40;
	m.m_pKernel->m_Fee = 20;

	SetRandom(m.m_Peer);
	m.m_MyIDKey = 325;

	// make the kernel look like the sender already did its part
	ECC::Point::Native pt = ECC::Context::get().G * ECC::Scalar::Native(115U);
	m.m_pKernel->m_Commitment = pt;
	m.m_pKernel->m_Signature.m_NoncePub = pt;
	m.m_pKernel->m_Signature.m_k = Zero;

	verify_test(InvokeOnBoth(m) != KeyKeeperHwEmu::Status::Success); // not receiving (though with fee it looks like the balance is to our side)

	Add(m.m_vOutputs, 100);

	verify_test(InvokeOnBoth(m) == KeyKeeperHwEmu::Status::Success); // should work now
}

void KeyKeeperWrap::TestSend1()
{
	wallet::IPrivateKeyKeeper2::Method::SignSender m;
	m.m_MyIDKey = 18;
	m.m_Peer = 567U;
	m.m_Slot = 6;
	m.m_UserAgreement = Zero;

	Add(m.m_vInputs, 45);

	Add(m.m_vOutputs, 25);

	m.m_pKernel = std::make_unique<TxKernelStd>();
	m.m_pKernel->m_Height.m_Min = g_hFork;
	m.m_pKernel->m_Height.m_Max = g_hFork + 40;
	m.m_pKernel->m_Fee = 20;

	verify_test(InvokeOnBoth(m) != KeyKeeperHwEmu::Status::Success); // not sending, everything is consumed by the fee

	Add(m.m_vInputs, 50);
	Add(m.m_vInputs, 15).m_AssetID = 4;

	verify_test(InvokeOnBoth(m) != KeyKeeperHwEmu::Status::Success); // mixed

	Add(m.m_vOutputs, 50);
	verify_test(InvokeOnBoth(m) == KeyKeeperHwEmu::Status::Success); // should work now
}

void TestKeyKeeperTxs()
{
	ECC::Hash::Value hv;
	SetRandom(hv);

	KeyKeeperWrap kkw(hv);

	printf("Split tx...\n");
	kkw.TestSplit();
	printf("Receiver tx...\n");
	kkw.TestRcv();
	printf("Send tx...\n");
	kkw.TestSend1();

	printf("Mutual Snd-Rcv-Snd tx...\n");

	// try a full with tx from both sides, each belongs to a different wallet
	SetRandom(hv);
	KeyKeeperWrap kkw2(hv); // the receiver

	wallet::IPrivateKeyKeeper2::Method::SignSender mS;
	mS.m_MyIDKey = 18;
	mS.m_Slot = 8;
	mS.m_UserAgreement = Zero;

	wallet::IPrivateKeyKeeper2::Method::SignReceiver mR;
	mR.m_MyIDKey = 23;

	kkw.get_WalletID(mR.m_Peer, mS.m_MyIDKey);
	kkw2.get_WalletID(mS.m_Peer, mR.m_MyIDKey);

	KeyKeeperWrap::Add(mS.m_vInputs, 50);
	KeyKeeperWrap::Add(mS.m_vOutputs, 25);

	mS.m_pKernel = std::make_unique<TxKernelStd>();
	mS.m_pKernel->m_Height.m_Min = g_hFork;
	mS.m_pKernel->m_Height.m_Max = g_hFork + 40;
	mS.m_pKernel->m_Fee = 20; // net value transfer is 5 groth

	verify_test(kkw.InvokeOnBoth(mS) == KeyKeeperHwEmu::Status::Success); // Sender Phase1
	verify_test(mS.m_UserAgreement != Zero);

	KeyKeeperWrap::Add(mR.m_vInputs, 6);
	KeyKeeperWrap::Add(mR.m_vOutputs, 11);

	mR.m_pKernel.swap(mS.m_pKernel);
	verify_test(kkw2.InvokeOnBoth(mR) == KeyKeeperHwEmu::Status::Success); // Receiver

	mS.m_pKernel.swap(mR.m_pKernel);
	mS.m_PaymentProofSignature = mR.m_PaymentProofSignature;
	mS.m_kOffset = mR.m_kOffset;

	Transaction tx;
	kkw2.ExportTx(tx, mR); // the receiver part

	mS.m_UserAgreement.Negate(); // tamper with user approval token
	verify_test(kkw.InvokeOnBoth(mS) != KeyKeeperHwEmu::Status::Success);

	mS.m_UserAgreement.Negate();
	mS.m_PaymentProofSignature.m_NoncePub.m_Y ^= 1; // tamper with payment proof signature
	verify_test(kkw.InvokeOnBoth(mS) != KeyKeeperHwEmu::Status::Success);

	mS.m_PaymentProofSignature.m_NoncePub.m_Y ^= 1;
	verify_test(kkw.InvokeOnBoth(mS) == KeyKeeperHwEmu::Status::Success); // Send Phase2

	kkw.ExportTx(tx, mS); // the sender part

	Transaction::Context::Params pars;
	Transaction::Context ctx(pars);
	ctx.m_Height.m_Min = mS.m_pKernel->m_Height.m_Min;
	verify_test(tx.IsValid(ctx));

	verify_test(kkw.InvokeOnBoth(mS) != KeyKeeperHwEmu::Status::Success); // Sender Phase2 can be called only once, the slot must have been invalidated
}

int main()
{
	Rules::get().CA.Enabled = true;
	Rules::get().pForks[1].m_Height = g_hFork;
	Rules::get().pForks[2].m_Height = g_hFork;

	//InitContext();

	TestMultiMac();
	TestNonceGen();
	TestOracle();
	TestKdf();
	TestCoins();
	TestSignature();
	TestKrn();
	TestPKdfExport();
	TestKeyKeeperTxs();

	printf("All done\n");

    return g_TestsFailed ? -1 : 0;
}
