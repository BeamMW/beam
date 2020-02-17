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
}

#include "../core/ecc_native.h"
#include "../core/block_crypt.h"


using namespace beam;

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

BeamCrypto_Point& Ecc2BC(const ECC::Point& x)
{
	static_assert(sizeof(x) == sizeof(BeamCrypto_Point));
	return (BeamCrypto_Point&) x;
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

void BeamCrypto_InitFast(BeamCrypto_MultiMac_Fast& trg, const ECC::MultiMac::Prepared p)
{
	const ECC::MultiMac::Prepared::Fast& src = p.m_Fast;

	static_assert(_countof(trg.m_pPt) <= _countof(src.m_pPt));

	for (uint32_t j = 0; j < _countof(trg.m_pPt); j++)
		trg.m_pPt[j] = src.m_pPt[j];
}

void InitContext()
{
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

	BeamCrypto_CoinID_getSkComm(&kdf2, &cid2, &sk2.get_Raw(), &Ecc2BC(comm2));

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

int main()
{
	Rules::get().CA.Enabled = true;
	Rules::get().pForks[1].m_Height = g_hFork;
	Rules::get().pForks[2].m_Height = g_hFork;

	InitContext();

	TestMultiMac();
	TestNonceGen();
	TestOracle();
	TestKdf();
	TestCoins();

    return g_TestsFailed ? -1 : 0;
}
