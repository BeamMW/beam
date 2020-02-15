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
#include "../ecc_native.h"
#include "../block_rw.h"
#include "../shielded.h"
#include "../treasury.h"
#include "../../utility/serialize.h"
#include "../serialization_adapters.h"
#include "../aes.h"
#include "../proto.h"
#include "../lelantus.h"
#include "../../utility/executor.h"

extern "C" {
#include "../ecc_min.h"
}

#if defined(__clang__) || defined(__GNUC__) || defined(__GNUG__)
#pragma GCC diagnostic ignored "-Wunused-result"
#endif

#if defined(__clang__) || defined(__GNUC__) || defined(__GNUG__)
#	pragma GCC diagnostic push
#	pragma GCC diagnostic ignored "-Wunused-function"
#else
#	pragma warning (push, 0) // suppress warnings from secp256k1
#	pragma warning (disable: 4706 4701)
#endif

#include "secp256k1-zkp/include/secp256k1_rangeproof.h" // For benchmark comparison with secp256k1
#include "secp256k1-zkp/src/group_impl.h"
#include "secp256k1-zkp/src/scalar_impl.h"
#include "secp256k1-zkp/src/field_impl.h"
#include "secp256k1-zkp/src/hash_impl.h"
#include "secp256k1-zkp/src/ecmult.h"
#include "secp256k1-zkp/src/ecmult_gen_impl.h"

#if defined(__clang__) || defined(__GNUC__) || defined(__GNUG__)
#	pragma GCC diagnostic pop
#else
#	pragma warning (default: 4706 4701)
#	pragma warning (pop)
#endif

// Needed for test
struct secp256k1_context_struct {
    secp256k1_ecmult_context ecmult_ctx;
    secp256k1_ecmult_gen_context ecmult_gen_ctx;
    secp256k1_callback illegal_callback;
    secp256k1_callback error_callback;
};

void secp256k1_ecmult_gen(const secp256k1_context* pCtx, secp256k1_gej *r, const secp256k1_scalar *a)
{
    secp256k1_ecmult_gen(&pCtx->ecmult_gen_ctx, r, a);
}

secp256k1_context* g_psecp256k1 = NULL;

int g_TestsFailed = 0;

const beam::Height g_hFork = 3; // whatever

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

inline void hex2bin(const char *hex_string, const size_t size_string, uint8_t *out_bytes)
{
  uint32_t buffer = 0;
  for (size_t i = 0; i < size_string / 2; i++)
  {
#ifdef WIN32
    sscanf_s(hex_string + 2 * i, "%2X", &buffer);
#else
    sscanf(hex_string + 2 * i, "%2X", &buffer);
#endif
    out_bytes[i] = (uint8_t)buffer;
  }
}

int IS_EQUAL_HEX(const char *hex_str, const uint8_t *bytes, size_t str_size)
{
  std::vector<uint8_t> tmp(str_size / 2);
  hex2bin(hex_str, str_size, &tmp[0]);
  return memcmp(&tmp[0], bytes, str_size / 2) == 0;
}

namespace ECC {

typedef beam::CoinID CoinID;

void Test_SetUintBig(uintBig& uintbig, int value)
{
	memset(uintbig.m_pData, value, uintbig.nBytes);
}

void GenerateRandom(void* p, uint32_t n)
{
	for (uint32_t i = 0; i < n; i++)
		((uint8_t*) p)[i] = (uint8_t) rand();
}

template <uint32_t nBytes>
void SetRandom(beam::uintBig_t<nBytes>& x)
{
	GenerateRandom(x.m_pData, x.nBytes);
}

void SetRandom(Scalar::Native& x)
{
	Scalar s;
	while (true)
	{
		SetRandom(s.m_Value);
		if (!x.Import(s))
			break;
	}
}

void SetRandom(Point::Native& value, uint8_t y = 0)
{
    Point p;

    SetRandom(p.m_X);
    p.m_Y = y;

    while (!value.Import(p))
    {
        verify_test(value == Zero);
        p.m_X.Inc();
    }
}

void SetRandom(Key::IKdf::Ptr& pPtr)
{
	uintBig hv;
	SetRandom(hv);
	HKdf::Create(pPtr, hv);
}


template <typename T>
void SetRandomOrd(T& x)
{
	GenerateRandom(&x, sizeof(x));
}

uint32_t get_LsBit(const uint8_t* pSrc, uint32_t nSrc, uint32_t iBit)
{
	uint32_t iByte = iBit >> 3;
	if (iByte >= nSrc)
		return 0;

	return 1 & (pSrc[nSrc - 1 - iByte] >> (7 & iBit));
}

void TestShifted2(const uint8_t* pSrc, uint32_t nSrc, const uint8_t* pDst, uint32_t nDst, int nShift)
{
	for (uint32_t iBitDst = 0; iBitDst < (nDst << 3); iBitDst++)
	{
		uint32_t a = get_LsBit(pSrc, nSrc, iBitDst - nShift);
		uint32_t b = get_LsBit(pDst, nDst, iBitDst);
		verify_test(a == b);
	}
}

template <uint32_t n0, uint32_t n1>
void TestShifted(const beam::uintBig_t<n0>& x0, const beam::uintBig_t<n1>& x1, int nShift)
{
	TestShifted2(x0.m_pData, x0.nBytes, x1.m_pData, x1.nBytes, nShift);
}

template <uint32_t n0, uint32_t n1>
void TestShifts(const beam::uintBig_t<n0>& src, beam::uintBig_t<n0>& src2, beam::uintBig_t<n1>& trg, int nShift)
{
	src2 = src;
	src2.ShiftLeft(nShift, trg);
	TestShifted(src, trg, nShift);
	src2 = src;
	src2.ShiftRight(nShift, trg);
	TestShifted(src, trg, -nShift);
}

void TestUintBig()
{
	for (int i = 0; i < 100; i++)
	{
		uint32_t a, b;
		SetRandomOrd(a);
		SetRandomOrd(b);

		uint64_t ab = a;
		ab *= b;

		uintBig v0, v1;
		v0 = a;
		v1 = b;
		v0 = v0 * v1;
		v1 = ab;

		verify_test(v0 == v1);

		ab = a;
		ab += b;

		v0 = a;
		v1 = b;

		v0 += v1;
		v1 = ab;

		verify_test(v0 == v1);
	}

	// test shifts, when src/dst types is smaller/bigger/equal
	for (int j = 0; j < 20; j++)
	{
		beam::uintBig_t<32> a;
		beam::uintBig_t<32 - 8> b;
		beam::uintBig_t<32 + 8> c;
		beam::uintBig_t<32> d;

		SetRandom(a);

		for (int i = 0; i < 512; i++)
		{
			TestShifts(a, a, b, i);
			TestShifts(a, a, c, i);
			TestShifts(a, a, d, i);
			TestShifts(a, d, d, i); // inplace shift
		}
	}
}

void TestHash()
{
	Oracle oracle;
	Hash::Value hv;
	oracle >> hv;

	for (int i = 0; i < 10; i++)
	{
		Hash::Value hv2 = hv;
		oracle >> hv;

		// hash values must change, even if no explicit input was fed.
		verify_test(!(hv == hv2));
	}
}

void TestScalars()
{
	Scalar::Native s0, s1, s2;
	s0 = 17U;

	// neg
	s1 = -s0;
	verify_test(!(s1 == Zero));
	s1 += s0;
	verify_test(s1 == Zero);

	// inv, mul
	s1.SetInv(s0);

	s2 = -s1;
	s2 += s0;
	verify_test(!(s2 == Zero));

	s1 *= s0;
	s2 = 1U;
	s2 = -s2;
	s2 += s1;
	verify_test(s2 == Zero);

	// import,export

	for (int i = 0; i < 1000; i++)
	{
		SetRandom(s0);

		Scalar s_(s0);
		s1 = s_;
		verify_test(s0 == s1);

		s1 = -s1;
		s1 += s0;
		verify_test(s1 == Zero);
	}

	// powers
	ScalarGenerator pwrGen, pwrGenInv;
	s0 = 7U; // looks like a good generator
	pwrGen.Initialize(s0);
	s0.Inv();
	pwrGenInv.Initialize(s0);


	for (int i = 0; i < 20; i++)
	{
		Scalar pwr;
		SetRandom(pwr.m_Value); // don't care if overflows, just doesn't matter

		pwrGen.Calculate(s1, pwr);
		pwrGenInv.Calculate(s2, pwr);

		s0.SetInv(s2);
		verify_test(s0 == s1);
	}
}

void TestPoints()
{
	Mode::Scope scope(Mode::Fast); // suppress assertion in point multiplication

	// generate, import, export
	Point::Native p0, p1;
	Point p_, p2_;

	p_.m_X = Zero; // should be zero-point
	p_.m_Y = 1;
	verify_test(!p0.Import(p_));
	verify_test(p0 == Zero);

	p_.m_Y = 0;
	verify_test(p0.Import(p_));
	verify_test(p0 == Zero);

	p2_ = p0;
	verify_test(p_ == p2_);

	for (int i = 0; i < 1000; i++)
	{
        SetRandom(p0, (1 & i));

		verify_test(!(p0 == Zero));
        p0.Export(p_);

		p1 = -p0;
		verify_test(!(p1 == Zero));

		p1 += p0;
		verify_test(p1 == Zero);

		p2_ = p0;
		verify_test(p_ == p2_);
	}

    // substraction
    {
        Point::Native pointNative;

        pointNative = Zero;

        verify_test(pointNative == Zero);

        SetRandom(pointNative);

        verify_test(pointNative != Zero);

        Point::Native pointNative2;
        pointNative2 = pointNative;

        pointNative -= pointNative2;

        verify_test(pointNative == Zero);
    }

    {
        Scalar::Native scalarNative;

        scalarNative = Zero;

        verify_test(scalarNative == Zero);

        SetRandom(scalarNative);

        verify_test(scalarNative != Zero);

        Scalar::Native scalarNative2;
        scalarNative2 = scalarNative;
        scalarNative -= scalarNative2;

        verify_test(scalarNative == Zero);
    }

	// multiplication
	Scalar::Native s0, s1;

	s0 = 1U;

	Point::Native g = Context::get().G * s0;
	verify_test(!(g == Zero));

	s0 = Zero;
	p0 = Context::get().G * s0;
	verify_test(p0 == Zero);

	p0 += g * s0;
	verify_test(p0 == Zero);

	for (int i = 0; i < 300; i++)
	{
		SetRandom(s0);

		p0 = Context::get().G * s0; // via generator

		s1 = -s0;
		p1 = p0;
		p1 += Context::get().G * s1; // inverse, also testing +=
		verify_test(p1 == Zero);

		p1 = p0;
		p1 += g * s1; // simple multiplication

		verify_test(p1 == Zero);
	}

	// H-gen
	Point::Native h = Context::get().H * 1U;
	verify_test(!(h == Zero));

	p0 = Context::get().H * 0U;
	verify_test(p0 == Zero);

	for (int i = 0; i < 300; i++)
	{
		Amount val;
		SetRandomOrd(val);

		p0 = Context::get().H * val; // via generator

		s0 = val;

		p1 = Zero;
		p1 += h * s0;
		p1 = -p1;
		p1 += p0;

		verify_test(p1 == Zero);
	}

	// doubling, all bits test
	s0 = 1U;
	s1 = 2U;
	p0 = g;

	for (int nBit = 1; nBit < 256; nBit++)
	{
		s0 *= s1;
		p1 = Context::get().G * s0;
		verify_test(!(p1 == Zero));

		p0 = p0 * Two;
		p0 = -p0;
		p0 += p1;
		verify_test(p0 == Zero);

		p0 = p1;
	}

	SetRandom(s1);
	p0 = g * s1;

	{
		Mode::Scope scope2(Mode::Secure);
		p1 = g * s1;
	}

	p1 = -p1;
	p1 += p0;
	verify_test(p1 == Zero);

	// Make sure we use the same G-generator as in secp256k1
	SetRandom(s0);
	secp256k1_ecmult_gen(g_psecp256k1, &p0.get_Raw(), &s0.get());
	p1 = Context::get().G * s0;

	p1 = -p1;
	p1 += p0;
	verify_test(p1 == Zero);

	// batch normalization
	const uint32_t nBatch = 10;
	Point::Native::BatchNormalizer_Arr_T<nBatch> bctx;

	Point::Native pPts[nBatch];
	SetRandom(pPts[0]);
	pPts[0] = pPts[0] * Two; // make sure it's not normalized

	for (uint32_t i = 1; i < nBatch; i++)
		pPts[i] = pPts[i-1] + pPts[0];

	memcpy(bctx.m_pPtsBuf, pPts, sizeof(pPts));

	bctx.Normalize();

	// test
	for (uint32_t i = 0; i < nBatch; i++)
	{
		p0 = -pPts[i];

		// treat the normalized point as affine
		secp256k1_ge ge;
		bctx.get_As(ge, bctx.m_pPts[i]);

		secp256k1_gej_add_ge(&p0.get_Raw(), &p0.get_Raw(), &ge);

		verify_test(p0 == Zero);
	}

	// bringing to the same denominator
	memcpy(bctx.m_pPtsBuf, pPts, sizeof(pPts));

	secp256k1_fe zDen;
	bctx.ToCommonDenominator(zDen);

	// test. Points brought to the same denominator can be added as if they were normalized, and only the final result should account for the denominator
	p0 = Zero;
	p1 = Zero;
	for (uint32_t i = 0; i < nBatch; i++)
	{
		p1 += pPts[i];

		// treat the normalized point as affine
		secp256k1_ge ge;
		bctx.get_As(ge, bctx.m_pPts[i]);

		secp256k1_gej_add_ge(&p0.get_Raw(), &p0.get_Raw(), &ge);
	}

	secp256k1_fe_mul(&p0.get_Raw().z, &p0.get_Raw().z, &zDen);

	p0 = -p0;
	p0 += p1;
	verify_test(p0 == Zero);
}

namespace ECC_Min
{
	template <unsigned int nMaxCount>
	struct MultiMac_WithBufs
	{
		ECC_Min_MultiMac_Prepared m_pPrepared[nMaxCount];
		ECC_Min_MultiMac_WNaf m_pWnaf[nMaxCount];
		ECC_Min_MultiMac_Scalar m_pS[nMaxCount];

		unsigned int m_Count = 0;

		void Reset() {
			m_Count = 0;
		}

		secp256k1_scalar& Add()
		{
			assert(m_Count < nMaxCount);
			return m_pS[m_Count++].m_pK[0];
		}

		void Add(const secp256k1_scalar& k)
		{
			Add() = k;
		}

		void Calculate(secp256k1_gej& res)
		{
			ECC_Min_MultiMac_Context ctx;
			ctx.m_pRes = &res;
			ctx.m_Count = m_Count;
			ctx.m_pPrep = m_pPrepared;
			ctx.m_pS = m_pS;
			ctx.m_pWnaf = m_pWnaf;
			ECC_Min_MultiMac_Calculate(&ctx);
		}
	};
} // namespace ECC_Min

void TestMultiMac()
{
	Mode::Scope scope(Mode::Fast);

	uint32_t bb = sizeof(ECC_Min_MultiMac_Prepared);
	uint32_t cc = sizeof(ECC_Min_MultiMac_WNaf);
	bb; cc;

	const uint32_t nBatch = 8;

	ECC::MultiMac_WithBufs<1, nBatch> mm1;
	ECC_Min::MultiMac_WithBufs<nBatch> mm2;

	for (uint32_t iGen = 0; iGen < nBatch; iGen++)
	{
		const MultiMac::Prepared& p = ECC::Context::get().m_Ipp.m_pGen_[0][iGen];
		mm1.m_ppPrepared[iGen] = &p;

		ECC_Min_MultiMac_Prepared& trg = mm2.m_pPrepared[iGen];
		const ECC::MultiMac::Prepared::Fast& src = p.m_Fast;

		static_assert(_countof(trg.m_pPt) <= _countof(src.m_pPt));

		for (uint32_t j = 0; j < _countof(trg.m_pPt); j++)
			trg.m_pPt[j] = src.m_pPt[j];
	}

	for (int i = 0; i < 10; i++)
	{
		mm1.Reset();
		mm2.Reset();


		for (uint32_t iPt = 0; iPt < nBatch; iPt++)
		{
			Scalar::Native sk;
			SetRandom(sk);

			mm1.m_pKPrep[iPt] = sk;
			mm1.m_Prepared++;

			mm2.Add(sk.get());

		}

		Point::Native res1, res2;
		mm1.Calculate(res1);
		mm2.Calculate(res2.get_Raw());

		verify_test(res1 == res2);
	}
}

void TestSigning()
{
	for (int i = 0; i < 30; i++)
	{
		Scalar::Native sk; // private key
		SetRandom(sk);

		Point::Native pk; // public key
		pk = Context::get().G * sk;

		Signature mysig;

		uintBig msg;
		SetRandom(msg); // assumed message

		mysig.Sign(msg, sk);

		verify_test(mysig.IsValid(msg, pk));

		// tamper msg
		uintBig msg2 = msg;
		msg2.Inc();

		verify_test(!mysig.IsValid(msg2, pk));

		// try to sign with different key
		Scalar::Native sk2;
		SetRandom(sk2);

		Signature mysig2;
		mysig2.Sign(msg, sk2);
		verify_test(!mysig2.IsValid(msg, pk));

		// tamper signature
		mysig2 = mysig;
		mysig2.m_NoncePub.m_Y = !mysig2.m_NoncePub.m_Y;
		verify_test(!mysig2.IsValid(msg, pk));

		mysig2 = mysig;
		SetRandom(mysig2.m_k.m_Value);
		verify_test(!mysig2.IsValid(msg, pk));
	}
}

void TestCommitments()
{
	Scalar::Native kExcess(Zero);

	Amount vSum = 0;

	Point::Native commInp(Zero);

	// inputs
	for (uint32_t i = 0; i < 7; i++)
	{
		Amount v = (i+50) * 400;
		Scalar::Native sk;
		SetRandom(sk);

		commInp += Commitment(sk, v);

		kExcess += sk;
		vSum += v;
	}

	// single output
	Point::Native commOutp(Zero);
	{
		Scalar::Native sk;
		SetRandom(sk);

		commOutp += Commitment(sk, vSum);

		sk = -sk;
		kExcess += sk;
	}

	Point::Native sigma = Context::get().G * kExcess;
	sigma += commOutp;

	sigma = -sigma;
	sigma += commInp;

	verify_test(sigma == Zero);

	// switch commitment
	HKdf kdf;
	uintBig seed;
	SetRandom(seed);
	kdf.Generate(seed);

	for (uint8_t iCycle = 0; iCycle < 2; iCycle++)
	{
		uint8_t nScheme = iCycle ? CoinID::Scheme::V1 : CoinID::Scheme::V0;

		CoinID cid(100500, 15, Key::Type::Regular);
		cid.set_Subkey(7, nScheme);

		Scalar::Native sk;
		Point::Native comm;
		CoinID::Worker(cid).Create(sk, comm, kdf);

		sigma = Commitment(sk, cid.m_Value);
		sigma = -sigma;
		sigma += comm;
		verify_test(sigma == Zero);

		CoinID::Worker(cid).Recover(sigma, kdf);
		sigma = -sigma;
		sigma += comm;
		verify_test(sigma == Zero);
	}
}

template <typename T>
void WriteSizeSerialized(const char* sz, const T& t)
{
	beam::SerializerSizeCounter ssc;
	ssc & t;

	printf("%s size = %u\n", sz, (uint32_t) ssc.m_Counter.m_Value);
}

struct AssetTag
	:public CoinID::Generator
{
	using Generator::Generator;

	void Commit(Point::Native& out, const Scalar::Native& sk, Amount v)
	{
		out = Context::get().G * sk;
		AddValue(out, v);
	}
};

void TestRangeProof(bool bCustomTag)
{
	RangeProof::CreatorParams cp;

	beam::uintBig_t<sizeof(Key::ID::Packed)> up0, up1;
	SetRandom(up0);
	cp.m_Blob = up0;

	SetRandom(cp.m_Seed.V);
	cp.m_Value = 345000;

	beam::Asset::Base aib;
	aib.m_ID = bCustomTag ? 14 : 0;

	AssetTag tag(aib.m_ID);

	Scalar::Native sk;
	SetRandom(sk);

	RangeProof::Public rp;
	{
		Oracle oracle;
		rp.Create(sk, cp, oracle);
		verify_test(rp.m_Value == cp.m_Value);
	}

	Point::Native comm;
	tag.Commit(comm, sk, rp.m_Value);

	{
		Oracle oracle;
		verify_test(rp.IsValid(comm, oracle, &tag.m_hGen));
	}

	{
		RangeProof::CreatorParams cp2;
		cp2.m_Seed = cp.m_Seed;
		cp2.m_Blob = up1;

		verify_test(rp.Recover(cp2));
		verify_test(cp.m_Value == cp2.m_Value);
		verify_test(up0 == up1);

		// leave only data needed for recovery
		RangeProof::Public rp2;
		ZeroObject(rp2);
		rp2.m_Recovery = rp.m_Recovery;
		rp2.m_Value = rp.m_Value;

		verify_test(rp2.Recover(cp2));
		verify_test(cp.m_Value == cp2.m_Value);
		verify_test(up0 == up1);
	}

	// tamper value
	rp.m_Value++;
	{
		Oracle oracle;
		verify_test(!rp.IsValid(comm, oracle, &tag.m_hGen));
	}
	rp.m_Value--;

	// try with invalid key
	SetRandom(sk);

	tag.Commit(comm, sk, rp.m_Value);

	{
		Oracle oracle;
		verify_test(!rp.IsValid(comm, oracle, &tag.m_hGen));
	}

	Scalar::Native pA[InnerProduct::nDim];
	Scalar::Native pB[InnerProduct::nDim];

	for (size_t i = 0; i < _countof(pA); i++)
	{
		SetRandom(pA[i]);
		SetRandom(pB[i]);
	}

	Scalar::Native pwrMul, dot;

	InnerProduct::get_Dot(dot, pA, pB);

	InnerProduct::Modifier::Channel ch0, ch1;
	SetRandom(pwrMul);
	ch0.SetPwr(pwrMul);
	SetRandom(pwrMul);
	ch1.SetPwr(pwrMul);
	InnerProduct::Modifier mod;
	mod.m_ppC[0] = &ch0;
	mod.m_ppC[1] = &ch1;

	InnerProduct sig;
	sig.Create(comm, dot, pA, pB, mod);

	InnerProduct::get_Dot(dot, pA, pB);

	verify_test(sig.IsValid(comm, dot, mod));

	RangeProof::Confidential bp;
	cp.m_Value = 23110;

	beam::uintBig_t<sizeof(Scalar) - sizeof(Amount) - 1> uc0, uc1;
	SetRandom(uc0);
	cp.m_Blob = uc0;

	tag.Commit(comm, sk, cp.m_Value);

	{
		Oracle oracle;
		bp.Create(sk, cp, oracle, &tag.m_hGen);
	}
	{
		Oracle oracle;
		verify_test(bp.IsValid(comm, oracle, &tag.m_hGen));
	}
	{
		Oracle oracle;
		RangeProof::CreatorParams cp2;
		cp2.m_Seed = cp.m_Seed;
		cp2.m_Blob = uc1;

		verify_test(bp.Recover(oracle, cp2));
		verify_test(uc0 == uc1);

		// leave only data needed for recovery
		RangeProof::Confidential bp2 = bp;

		ZeroObject(bp2.m_Part3);
		ZeroObject(bp2.m_tDot);
		ZeroObject(bp2.m_P_Tag);

		oracle = Oracle();
		verify_test(bp2.Recover(oracle, cp2));
		verify_test(uc0 == uc1);
	}

	// Bulletproof with extra data embedded
	uintBig seedSk = 4432U;
	Scalar::Native pEx[2];
	SetRandom(pEx[0]);
	SetRandom(pEx[1]);
	{
		Oracle oracle;
		cp.m_pExtra = pEx;
		bp.CoSign(seedSk, sk, cp, oracle, RangeProof::Confidential::Phase::SinglePass, &tag.m_hGen);
	}
	{
		Oracle oracle;
		verify_test(bp.IsValid(comm, oracle, &tag.m_hGen));
	}
	{
		Oracle oracle;
		Scalar::Native sk2;
		Scalar::Native pExVer[2];

		RangeProof::CreatorParams cp2;
		cp2.m_Blob = uc1;
		cp2.m_Seed = cp.m_Seed;
		cp2.m_pSeedSk = &seedSk;
		cp2.m_pSk = &sk2;
		cp2.m_pExtra = pExVer;

		verify_test(bp.Recover(oracle, cp2));
		verify_test(uc0 == uc1);
		verify_test(sk == sk2);
		verify_test((pEx[0] == pExVer[0]) && (pEx[1] == pExVer[1]));
	}

	InnerProduct::BatchContextEx<2> bc;

	{
		Oracle oracle;
		verify_test(bp.IsValid(comm, oracle, bc, &tag.m_hGen)); // add to batch
	}

	SetRandom(sk);
	cp.m_Value = 7223110;
	SetRandom(cp.m_Seed.V); // another seed for this bulletproof
	tag.Commit(comm, sk, cp.m_Value);

	{
		Oracle oracle;
		bp.Create(sk, cp, oracle, &tag.m_hGen);
	}
	{
		Oracle oracle;
		verify_test(bp.IsValid(comm, oracle, bc, &tag.m_hGen)); // add to batch
	}

	verify_test(bc.Flush()); // verify at once


	WriteSizeSerialized("BulletProof", bp);

	{
		// multi-signed bulletproof
		const uint32_t nSigners = 5;

		Scalar::Native pSk[nSigners];
		uintBig pSeed[nSigners];

		// 1st cycle. peers produce Part2, aggregate commitment
		RangeProof::Confidential::Part2 p2;
		ZeroObject(p2);

		comm = Zero;
		Tag::AddValue(comm, &tag.m_hGen, cp.m_Value);

		RangeProof::Confidential::MultiSig msig;

		for (uint32_t i = 0; i < nSigners; i++)
		{
			SetRandom(pSk[i]);
			SetRandom(pSeed[i]);

			comm += Context::get().G * pSk[i];

			if (i + 1 < nSigners)
				verify_test(RangeProof::Confidential::MultiSig::CoSignPart(pSeed[i], p2)); // p2 aggregation
			else
			{
				Oracle oracle;
				bp.m_Part2 = p2;
				verify_test(bp.CoSign(pSeed[i], pSk[i], cp, oracle, RangeProof::Confidential::Phase::Step2, &tag.m_hGen)); // add last p2, produce msig
				p2 = bp.m_Part2;

				msig.m_Part1 = bp.m_Part1;
				msig.m_Part2 = bp.m_Part2;
			}
		}

		// 2nd cycle. Peers produce Part3
		RangeProof::Confidential::Part3 p3;
		ZeroObject(p3);

		for (uint32_t i = 0; i < nSigners; i++)
		{
			Oracle oracle;

			if (i + 1 < nSigners)
				msig.CoSignPart(pSeed[i], pSk[i], oracle, p3);
			else
			{
				bp.m_Part2 = p2;
				bp.m_Part3 = p3;
				verify_test(bp.CoSign(pSeed[i], pSk[i], cp, oracle, RangeProof::Confidential::Phase::Finalize, &tag.m_hGen));
			}
		}


		{
			// test
			Oracle oracle;
			verify_test(bp.IsValid(comm, oracle, &tag.m_hGen));
		}
	}

	HKdf kdf;
	uintBig seed;
	SetRandom(seed);
	kdf.Generate(seed);

	CoinID cid(20300, 1, Key::Type::Regular);
	cid.m_AssetID = aib.m_ID;

	{

		beam::Output outp;
		outp.m_Coinbase = true; // others may be disallowed
		outp.Create(g_hFork, sk, kdf, cid, kdf, true);
		verify_test(outp.IsValid(g_hFork, comm));
		WriteSizeSerialized("Out-UTXO-Public", outp);

		outp.m_RecoveryOnly = true;
		WriteSizeSerialized("Out-UTXO-Public-RecoveryOnly", outp);
	}
	{
		beam::Output outp;
		outp.Create(g_hFork, sk, kdf, cid, kdf);
		verify_test(outp.IsValid(g_hFork, comm));
		WriteSizeSerialized("Out-UTXO-Confidential", outp);

		outp.m_RecoveryOnly = true;
		WriteSizeSerialized("Out-UTXO-Confidential-RecoveryOnly", outp);

		CoinID cid2;
		verify_test(outp.Recover(g_hFork, kdf, cid2));
		verify_test(cid == cid2);
	}

	WriteSizeSerialized("In-Utxo", beam::Input());

	beam::TxKernelStd txk;
	txk.m_Fee = 50;
	WriteSizeSerialized("Kernel(simple)", txk);
}

void TestMultiSigOutput()
{
    Amount amount = 5000;

    beam::Key::IKdf::Ptr pKdf_A;
    beam::Key::IKdf::Ptr pKdf_B;
    SetRandom(pKdf_B);
    SetRandom(pKdf_A);

    // multi-signed bulletproof
    // blindingFactor = sk + sk1
    Scalar::Native blindingFactorA;
    Scalar::Native blindingFactorB;

	CoinID cid(amount, 0, Key::Type::Regular);
	CoinID::Worker wrk(cid);

    wrk.Create(blindingFactorA, *pKdf_A);
    wrk.Create(blindingFactorB, *pKdf_B);

    // seed from RangeProof::Confidential::Create
    uintBig seedA;
    uintBig seedB;
    {
        Oracle oracle;
        RangeProof::Confidential::GenerateSeed(seedA, blindingFactorA, amount, oracle);
        RangeProof::Confidential::GenerateSeed(seedB, blindingFactorB, amount, oracle);
    }
    Point::Native commitment(Zero);
    Tag::AddValue(commitment, nullptr, amount);
    commitment += Context::get().G * blindingFactorA;
    commitment += Context::get().G * blindingFactorB;

	beam::Output outp;
	outp.m_Commitment = commitment;

	Oracle o0; // context for creating the bulletproof.
	outp.Prepare(o0, g_hFork);

    // from Output::get_SeedKid    
	RangeProof::CreatorParams cp;
	cp.m_Value = cid.m_Value;
	beam::Output::GenerateSeedKid(cp.m_Seed.V, outp.m_Commitment, *pKdf_B);

    // 1st cycle. peers produce Part2
    RangeProof::Confidential::Part2 p2;
    ZeroObject(p2);

    // A part2
    verify_test(RangeProof::Confidential::MultiSig::CoSignPart(seedA, p2)); // p2 aggregation

    // B part2
	RangeProof::Confidential::MultiSig multiSig;
	{
        Oracle oracle(o0);
		RangeProof::Confidential bulletproof;
		bulletproof.m_Part2 = p2;

        verify_test(bulletproof.CoSign(seedB, blindingFactorB, cp, oracle, RangeProof::Confidential::Phase::Step2)); // add last p2, produce msig

		multiSig.m_Part1 = bulletproof.m_Part1;
		multiSig.m_Part2 = bulletproof.m_Part2;
        p2 = bulletproof.m_Part2;
    }

    // 2nd cycle. Peers produce Part3, commitment is aggregated too
    RangeProof::Confidential::Part3 p3;
    ZeroObject(p3);

    // A part3
	{
		Oracle oracle(o0);
		multiSig.CoSignPart(seedA, blindingFactorA, oracle, p3);
	}

    // B part3
    {
		outp.m_pConfidential = std::make_unique<RangeProof::Confidential>();
		outp.m_pConfidential->m_Part1 = multiSig.m_Part1;
		outp.m_pConfidential->m_Part2 = multiSig.m_Part2;
		outp.m_pConfidential->m_Part3 = p3;

		Oracle oracle(o0);
		verify_test(outp.m_pConfidential->CoSign(seedB, blindingFactorB, cp, oracle, RangeProof::Confidential::Phase::Finalize));
    }

    {
        // test
        Oracle oracle(o0);
        verify_test(outp.IsValid(g_hFork, commitment));
    }

    //==========================================================
    Scalar::Native offset;

    // create Input
    std::unique_ptr<beam::Input> pInput(new beam::Input);

    // create test coin
    SetRandomOrd(cid.m_Idx);
	cid.m_Type = Key::Type::Regular;
	cid.set_Subkey(0);
	cid.m_Value = amount;
    Scalar::Native k;
    CoinID::Worker(cid).Create(k, pInput->m_Commitment, *pKdf_A);
    offset = k;

    // output
    std::unique_ptr<beam::Output> pOutput(new beam::Output);
	*pOutput = outp;
    {
        Point::Native comm;
        verify_test(pOutput->IsValid(g_hFork, comm));
    }
    Scalar::Native outputBlindingFactor;
    outputBlindingFactor = blindingFactorA + blindingFactorB;
    outputBlindingFactor = -outputBlindingFactor;
    offset += outputBlindingFactor;

    // kernel
    Scalar::Native blindingExcessA;
    Scalar::Native blindingExcessB;
    SetRandom(blindingExcessA);
    SetRandom(blindingExcessB);
    offset += blindingExcessA;
    offset += blindingExcessB;

    blindingExcessA = -blindingExcessA;
    blindingExcessB = -blindingExcessB;

    Point::Native blindingExcessPublicA = Context::get().G * blindingExcessA;
    Point::Native blindingExcessPublicB = Context::get().G * blindingExcessB;

    Scalar::Native nonceA;
    Scalar::Native nonceB;
    SetRandom(nonceA);
    SetRandom(nonceB);
    Point::Native noncePublicA = Context::get().G * nonceA;
    Point::Native noncePublicB = Context::get().G * nonceB;
    Point::Native noncePublic = noncePublicA + noncePublicB;

    std::unique_ptr<beam::TxKernelStd> pKernel(new beam::TxKernelStd);
    pKernel->m_Fee = 0;
    pKernel->m_Height.m_Min = 100;
    pKernel->m_Height.m_Max = 220;
    pKernel->m_Commitment = blindingExcessPublicA + blindingExcessPublicB;
    pKernel->UpdateID();
	const Hash::Value& message = pKernel->m_Internal.m_ID;

	pKernel->m_Signature.m_NoncePub = noncePublic;

	pKernel->m_Signature.SignPartial(message, blindingExcessA, nonceA);
	verify_test(pKernel->m_Signature.IsValidPartial(message, noncePublicA, blindingExcessPublicA));
	Scalar::Native partialSignatureA = pKernel->m_Signature.m_k;

	pKernel->m_Signature.SignPartial(message, blindingExcessB, nonceB);
	verify_test(pKernel->m_Signature.IsValidPartial(message, noncePublicB, blindingExcessPublicB));
	Scalar::Native partialSignatureB = pKernel->m_Signature.m_k;

    pKernel->m_Signature.m_k = partialSignatureA + partialSignatureB;

    // create transaction
    beam::Transaction transaction;
    transaction.m_vKernels.push_back(move(pKernel));
    transaction.m_Offset = offset;
    transaction.m_vInputs.push_back(std::move(pInput));
    transaction.m_vOutputs.push_back(std::move(pOutput));
    transaction.Normalize();

    beam::TxBase::Context::Params pars;
    beam::TxBase::Context context(pars);
	context.m_Height.m_Min = g_hFork;
    verify_test(transaction.IsValid(context));
}

struct TransactionMaker
{
	beam::Transaction m_Trans;
	HKdf m_Kdf;

	TransactionMaker()
	{
		m_Trans.m_Offset.m_Value = Zero;
	}

	struct Peer
	{
		Scalar::Native m_k;

		Peer()
		{
			m_k = Zero;
		}

		void FinalizeExcess(Point::Native& kG, Scalar::Native& kOffset)
		{
			kOffset += m_k;

			SetRandom(m_k);
			kOffset += m_k;

			m_k = -m_k;
			kG += Context::get().G * m_k;
		}

		void AddInput(beam::Transaction& t, Amount val, Key::IKdf& kdf, beam::Asset::ID nAssetID = 0)
		{
			std::unique_ptr<beam::Input> pInp(new beam::Input);

			CoinID cid;
			SetRandomOrd(cid.m_Idx);
			cid.m_Type = Key::Type::Regular;
			cid.set_Subkey(0);
			cid.m_Value = val;
			cid.m_AssetID = nAssetID;

			Scalar::Native k;
			CoinID::Worker(cid).Create(k, pInp->m_Commitment, kdf);

			t.m_vInputs.push_back(std::move(pInp));
			m_k += k;
		}

		void AddOutput(beam::Transaction& t, Amount val, Key::IKdf& kdf, beam::Asset::ID nAssetID = 0)
		{
			std::unique_ptr<beam::Output> pOut(new beam::Output);

			Scalar::Native k;

			CoinID cid;
			SetRandomOrd(cid.m_Idx);
			cid.m_Type = Key::Type::Regular;
			cid.set_Subkey(0);
			cid.m_Value = val;
			cid.m_AssetID = nAssetID;

			pOut->Create(g_hFork, k, kdf, cid, kdf);

			// test recovery
			CoinID cid2;
			verify_test(pOut->Recover(g_hFork, kdf, cid2));
			verify_test(cid == cid2);

			t.m_vOutputs.push_back(std::move(pOut));

			k = -k;
			m_k += k;
		}

	};

	Peer m_pPeers[2]; // actually can be more

	void CoSignKernel(beam::TxKernelStd& krn)
	{
		// 1st pass. Public excesses and Nonces are summed.
		Scalar::Native pX[_countof(m_pPeers)];
		Scalar::Native offset(m_Trans.m_Offset);

		Point::Native xG(Zero), kG(Zero);

		for (size_t i = 0; i < _countof(m_pPeers); i++)
		{
			Peer& p = m_pPeers[i];
			p.FinalizeExcess(kG, offset);

			SetRandom(pX[i]);
			xG += Context::get().G * pX[i];
		}

		m_Trans.m_Offset = offset;

		krn.m_Commitment = kG;
		krn.m_Signature.m_NoncePub = xG;

		krn.UpdateID();
		const Hash::Value& msg = krn.m_Internal.m_ID;

		// 2nd pass. Signing. Total excess is the signature public key.
		Scalar::Native kSig = Zero;

		for (size_t i = 0; i < _countof(m_pPeers); i++)
		{
			Peer& p = m_pPeers[i];

			krn.m_Signature.SignPartial(msg, p.m_k, pX[i]);
			kSig += krn.m_Signature.m_k;

			p.m_k = Zero; // signed, prepare for next tx
		}

		krn.m_Signature.m_k = kSig;
	}

	void CreateTxKernel(std::vector<beam::TxKernel::Ptr>& lstTrg, Amount fee, std::vector<beam::TxKernel::Ptr>& lstNested, bool bEmitCustomTag, bool bNested)
	{
		std::unique_ptr<beam::TxKernelStd> pKrn(new beam::TxKernelStd);
		pKrn->m_Fee = fee;
		pKrn->m_Height.m_Min = g_hFork;
		pKrn->m_CanEmbed = bNested;
		pKrn->m_vNested.swap(lstNested);

		// hashlock
		pKrn->m_pHashLock.reset(new beam::TxKernelStd::HashLock);
		//pKrn->m_pHashLock.release();

		beam::TxKernelStd::HashLock hl;
		SetRandom(hl.m_Value);

		pKrn->m_pHashLock->m_IsImage = true;
		pKrn->m_pHashLock->m_Value = hl.get_Image(pKrn->m_pHashLock->m_Value);

		if (bEmitCustomTag)
		{
			// emit some asset
			beam::Asset::Metadata md;
			md.m_Value.assign({ 1, 2, 3, 4, 5 });
			md.UpdateHash();

			Scalar::Native sk;
			beam::Asset::ID nAssetID = 17;
			Amount valAsset = 4431;
			
			SetRandom(sk); // excess

			m_pPeers[0].AddOutput(m_Trans, valAsset, m_Kdf, nAssetID); // output UTXO to consume the created asset

			beam::TxKernelAssetEmit::Ptr pKrnEmission(new beam::TxKernelAssetEmit);
			pKrnEmission->m_Height.m_Min = g_hFork;
			pKrnEmission->m_CanEmbed = bNested;
			pKrnEmission->m_AssetID = nAssetID;
			pKrnEmission->m_Value = valAsset;

			pKrnEmission->Sign(sk, m_Kdf, md);

			lstTrg.push_back(std::move(pKrnEmission));

			sk = -sk;
			m_pPeers[0].m_k += sk;
		}

		CoSignKernel(*pKrn);

		Point::Native exc;
		pKrn->m_pHashLock->m_IsImage = false;
		pKrn->UpdateID();
		verify_test(!pKrn->IsValid(g_hFork, exc)); // should not pass validation unless correct hash preimage is specified

		// finish HL: add hash preimage
		pKrn->m_pHashLock->m_Value = hl.m_Value;
		pKrn->UpdateID();
		verify_test(pKrn->IsValid(g_hFork, exc));

		lstTrg.push_back(std::move(pKrn));
	}

	void AddInput(int i, Amount val)
	{
		m_pPeers[i].AddInput(m_Trans, val, m_Kdf);
	}

	void AddOutput(int i, Amount val)
	{
		m_pPeers[i].AddOutput(m_Trans, val, m_Kdf);
	}
};

void TestTransaction()
{
	TransactionMaker tm;
	tm.AddInput(0, 3000);
	tm.AddInput(0, 2000);
	tm.AddOutput(0, 500);

	tm.AddInput(1, 1000);
	tm.AddOutput(1, 5400);

	std::vector<beam::TxKernel::Ptr> lstNested, lstDummy;

	Amount fee1 = 100, fee2 = 2;

	tm.CreateTxKernel(lstNested, fee1, lstDummy, false, true);

	tm.AddOutput(0, 738);
	tm.AddInput(1, 740);
	tm.CreateTxKernel(tm.m_Trans.m_vKernels, fee2, lstNested, true, false);

	tm.m_Trans.Normalize();

	beam::TxBase::Context::Params pars;
	beam::TxBase::Context ctx(pars);
	ctx.m_Height.m_Min = g_hFork;
	verify_test(tm.m_Trans.IsValid(ctx));
	verify_test(ctx.m_Stats.m_Fee == beam::AmountBig::Type(fee1 + fee2));
}

void TestCutThrough()
{
	TransactionMaker tm;
	tm.AddOutput(0, 3000);
	tm.AddOutput(0, 2000);

	tm.m_Trans.Normalize();

	beam::TxBase::Context::Params pars;
	beam::TxBase::Context ctx(pars);
	ctx.m_Height.m_Min = g_hFork;
	verify_test(ctx.ValidateAndSummarize(tm.m_Trans, tm.m_Trans.get_Reader()));

	beam::Input::Ptr pInp(new beam::Input);
	pInp->m_Commitment = tm.m_Trans.m_vOutputs.front()->m_Commitment;
	tm.m_Trans.m_vInputs.push_back(std::move(pInp));

	ctx.Reset();
	ctx.m_Height = g_hFork;
	verify_test(!ctx.ValidateAndSummarize(tm.m_Trans, tm.m_Trans.get_Reader())); // redundant outputs must be banned!

	verify_test(tm.m_Trans.Normalize() == 1);

	ctx.Reset();
	ctx.m_Height = g_hFork;
	verify_test(ctx.ValidateAndSummarize(tm.m_Trans, tm.m_Trans.get_Reader()));
}

void TestAES()
{
	// AES in ECB mode (simplest): https://csrc.nist.gov/CSRC/media/Projects/Cryptographic-Standards-and-Guidelines/documents/examples/AES_Core256.pdf

	uint8_t pKey[AES::s_KeyBytes] = {
		0x60,0x3D,0xEB,0x10,0x15,0xCA,0x71,0xBE,0x2B,0x73,0xAE,0xF0,0x85,0x7D,0x77,0x81,
		0x1F,0x35,0x2C,0x07,0x3B,0x61,0x08,0xD7,0x2D,0x98,0x10,0xA3,0x09,0x14,0xDF,0xF4
	};

	const uint8_t pPlaintext[AES::s_BlockSize] = {
		0x6B,0xC1,0xBE,0xE2,0x2E,0x40,0x9F,0x96,0xE9,0x3D,0x7E,0x11,0x73,0x93,0x17,0x2A
	};

	const uint8_t pCiphertext[AES::s_BlockSize] = {
		0xF3,0xEE,0xD1,0xBD,0xB5,0xD2,0xA0,0x3C,0x06,0x4B,0x5A,0x7E,0x3D,0xB1,0x81,0xF8
	};

	struct {
		uint32_t zero0 = 0;
		AES::Encoder enc;
		uint32_t zero1 = 0;
	} se;

	se.enc.Init(pKey);
	verify_test(!se.zero0 && !se.zero1);

	uint8_t pBuf[sizeof(pPlaintext)];
	memcpy(pBuf, pPlaintext, sizeof(pBuf));

	se.enc.Proceed(pBuf, pBuf); // inplace encode
	verify_test(!memcmp(pBuf, pCiphertext, sizeof(pBuf)));

	struct {
		uint32_t zero0 = 0;
		AES::Decoder dec;
		uint32_t zero1 = 0;
	} sd;

	sd.dec.Init(se.enc);
	verify_test(!sd.zero0 && !sd.zero1);

	sd.dec.Proceed(pBuf, pBuf); // inplace decode
	verify_test(!memcmp(pBuf, pPlaintext, sizeof(pPlaintext)));
}

void TestKdfPair(Key::IKdf& skdf, Key::IPKdf& pkdf)
{
	for (uint32_t i = 0; i < 10; i++)
	{
		Hash::Value hv;
		Hash::Processor() << "test_kdf" << i >> hv;

		Scalar::Native sk0, sk1;
		skdf.DerivePKey(sk0, hv);
		pkdf.DerivePKey(sk1, hv);
		verify_test(Scalar(sk0) == Scalar(sk1));

		skdf.DeriveKey(sk0, hv);
		verify_test(Scalar(sk0) != Scalar(sk1));

		Point::Native pk0, pk1;
		skdf.DerivePKeyG(pk0, hv);
		pkdf.DerivePKeyG(pk1, hv);
		pk1 = -pk1;
		pk0 += pk1;
		verify_test(pk0 == Zero);

		skdf.DerivePKeyJ(pk0, hv);
		pkdf.DerivePKeyJ(pk1, hv);
		pk1 = -pk1;
		pk0 += pk1;
		verify_test(pk0 == Zero);
	}
}

void TestKdf()
{
	HKdf skdf;
	HKdfPub pkdf;

	uintBig seed;
	SetRandom(seed);

	skdf.Generate(seed);
	pkdf.GenerateFrom(skdf);

	TestKdfPair(skdf, pkdf);

	const std::string sPass("test password");

	beam::KeyString ks1;
	ks1.SetPassword(sPass);
	ks1.m_sMeta = "hello, World!";

	ks1.ExportS(skdf);
	HKdf skdf2;
	ks1.m_sMeta.clear();
	ks1.SetPassword(sPass);
	verify_test(ks1.Import(skdf2));

	verify_test(skdf2.IsSame(skdf));

	ks1.ExportP(pkdf);
	HKdfPub pkdf2;
	verify_test(ks1.Import(pkdf2));
	verify_test(pkdf2.IsSame(pkdf));

	seed.Inc();
	skdf2.Generate(seed);
	verify_test(!skdf2.IsSame(skdf));

	// parallel key generation
	SetRandom(seed);

	skdf2.GenerateChildParallel(skdf, seed);

	pkdf.GenerateFrom(skdf);
	pkdf2.GenerateChildParallel(pkdf, seed);

	TestKdfPair(skdf2, pkdf2);
}

void TestBbs()
{
	Scalar::Native privateAddr, nonce;
	beam::PeerID publicAddr;

	SetRandom(privateAddr);
	publicAddr.FromSk(privateAddr);

	const char szMsg[] = "Hello, World!";

	SetRandom(nonce);
	beam::ByteBuffer buf;
	verify_test(beam::proto::Bbs::Encrypt(buf, publicAddr, nonce, szMsg, sizeof(szMsg)));

	uint8_t* p = &buf.at(0);
	uint32_t n = (uint32_t) buf.size();

	verify_test(beam::proto::Bbs::Decrypt(p, n, privateAddr));
	verify_test(n == sizeof(szMsg));
	verify_test(!memcmp(p, szMsg, n));

	SetRandom(privateAddr);
	p = &buf.at(0);
	n = (uint32_t) buf.size();

	verify_test(!beam::proto::Bbs::Decrypt(p, n, privateAddr));
}

void TestRatio(const beam::Difficulty& d0, const beam::Difficulty& d1, double k)
{
	const double tol = 1.000001;
	double k_ = d0.ToFloat() / d1.ToFloat();
	verify_test((k_ < k * tol) && (k < k_ * tol));
}

void TestDifficulty()
{
	using namespace beam;

	Difficulty::Raw r1, r2;
	Difficulty(Difficulty::s_Inf).Unpack(r1);
	Difficulty(Difficulty::s_Inf - 1).Unpack(r2);
	verify_test(r1 > r2);

	uintBig val(Zero);

	verify_test(Difficulty(Difficulty::s_Inf).IsTargetReached(val));

	val.m_pData[0] = 0x80; // msb set

	verify_test(Difficulty(0).IsTargetReached(val));
	verify_test(Difficulty(1).IsTargetReached(val));
	verify_test(Difficulty(0xffffff).IsTargetReached(val)); // difficulty almost 2
	verify_test(!Difficulty(0x1000000).IsTargetReached(val)); // difficulty == 2

	val.m_pData[0] = 0x7f;
	verify_test(Difficulty(0x1000000).IsTargetReached(val));

	// Adjustments
	Difficulty d, d2;
	d.m_Packed = 3 << Difficulty::s_MantissaBits;

	Difficulty::Raw raw, wrk;
	d.Unpack(raw);
	uint32_t dh = 1440;
	wrk.AssignMul(raw, uintBigFrom(dh));

	d2.Calculate(wrk, dh, 100500, 100500);
	TestRatio(d2, d, 1.);

	// slight increase
	d2.Calculate(wrk, dh, 100500, 100000);
	TestRatio(d2, d, 1.005);

	// strong increase
	d2.Calculate(wrk, dh, 180000, 100000);
	TestRatio(d2, d, 1.8);

	// huge increase
	d2.Calculate(wrk, dh, 7380000, 100000);
	TestRatio(d2, d, 73.8);

	// insane increase (1.7 billions). Still must fit
	d2.Calculate(wrk, dh, 1794380000, 1);
	TestRatio(d2, d, 1794380000);

	// slight decrease
	d2.Calculate(wrk, dh, 100000, 100500);
	TestRatio(d, d2, 1.005);

	// strong decrease
	d2.Calculate(wrk, dh, 100000, 180000);
	TestRatio(d, d2, 1.8);

	// insane decrease, out-of-bound
	d2.Calculate(wrk, dh, 100000, 7380000);
	verify_test(!d2.m_Packed);

	for (uint32_t i = 0; i < 200; i++)
	{
		GenerateRandom(&d, sizeof(d));

		uintBig trg;
		if (!d.get_Target(trg))
		{
			verify_test(d.m_Packed >= Difficulty::s_Inf);
			continue;
		}

		verify_test(d.IsTargetReached(trg));

		trg.Inc();
		if (!(trg == Zero)) // overflow?
			verify_test(!d.IsTargetReached(trg));
	}
}

void TestRandom()
{
	uintBig pV[2];
	ZeroObject(pV);

	for (uint32_t i = 0; i < 10; i++)
	{
		uintBig& a = pV[1 & i];
		uintBig& b = pV[1 & (i + 1)];

		a = Zero;
		GenRandom(a);
		verify_test(!(a == Zero));
		verify_test(!(a == b));
	}
}

bool IsOkFourCC(const char* szRes, const char* szSrc)
{
	// the formatted FourCC always consists of 4 characters. If source is shorter - spaced are appended
	size_t n = strlen(szSrc);
	for (size_t i = 0; i < 4; i++)
	{
		char c = (i < n) ? szSrc[i] : ' ';
		if (szRes[i] != c)
			return false;
	}

	return !szRes[4];

}

void TestFourCC()
{
#define TEST_FOURCC(name) \
	{ \
		uint32_t nFourCC = FOURCC_FROM(name); \
		beam::FourCC::Text txt(nFourCC); \
		verify_test(IsOkFourCC(txt, #name)); \
	}

	// compile-time FourCC should support shorter strings
	TEST_FOURCC(help)
	TEST_FOURCC(hel)
	TEST_FOURCC(he)
	TEST_FOURCC(h)
}

void TestTreasury()
{
	beam::Treasury::Parameters pars;
	pars.m_Bursts = 12;
	pars.m_MaturityStep = 1440 * 30 * 4;

	beam::Treasury tres;

	const uint32_t nPeers = 3;
	HKdf pKdfs[nPeers];

	for (uint32_t i = 0; i < nPeers; i++)
	{
		// 1. target wallet is initialized, generates its PeerID
		uintBig seed;
		SetRandom(seed);
		pKdfs[i].Generate(seed);

		beam::PeerID pid;
		Scalar::Native sk;
		beam::Treasury::get_ID(pKdfs[i], pid, sk);

		// 2. Plan is created (2%, 3%, 4% of the total emission)
		beam::Treasury::Entry* pE = tres.CreatePlan(pid, beam::Rules::get().Emission.Value0 * (i + 2)/100, pars);
		verify_test(pE->m_Request.m_WalletID == pid);

		// test Request serialization
		beam::Serializer ser0;
		ser0 & pE->m_Request;

		beam::Deserializer der0;
		der0.reset(ser0.buffer().first, ser0.buffer().second);

		beam::Treasury::Request req;
		der0 & req;

		// 3. Plan is appvoved by the wallet, response is generated
		pE->m_pResponse.reset(new beam::Treasury::Response);
		uint64_t nIndex = 1;
		verify_test(pE->m_pResponse->Create(req, pKdfs[i], nIndex));
		verify_test(pE->m_pResponse->m_WalletID == pid);

		// 4. Reponse is verified
		verify_test(pE->m_pResponse->IsValid(pE->m_Request));
	}

	// test serialization
	beam::Serializer ser1;
	ser1 & tres;

	tres.m_Entries.clear();

	beam::Deserializer der1;
	der1.reset(ser1.buffer().first, ser1.buffer().second);
	der1 & tres;

	verify_test(tres.m_Entries.size() == nPeers);

	std::string msg = "cool treasury";
	beam::Treasury::Data data;
	data.m_sCustomMsg = msg;
	tres.Build(data);
	verify_test(!data.m_vGroups.empty());

	std::vector<beam::Treasury::Data::Burst> vBursts = data.get_Bursts();

	// test serialization
	beam::ByteBuffer bb;
	ser1.swap_buf(bb);
	ser1 & data;

	data.m_vGroups.clear();
	data.m_sCustomMsg.clear();

	der1.reset(ser1.buffer().first, ser1.buffer().second);
	der1 & data;

	verify_test(!data.m_vGroups.empty());
	verify_test(data.m_sCustomMsg == msg);
	verify_test(data.IsValid());

	for (uint32_t i = 0; i < nPeers; i++)
	{
		std::vector<beam::Treasury::Data::Coin> vCoins;
		data.Recover(pKdfs[i], vCoins);
		verify_test(vCoins.size() == pars.m_Bursts);
	}
}

void TestLelantus(bool bWithAsset)
{
	beam::Lelantus::Cfg cfg; // default

	if (bWithAsset)
	{
		// set other parameters. Test small set (make it run faster)
		cfg.n = 3;
		cfg.M = 4; // 3^4 = 81
	}

	const uint32_t N = cfg.get_N();
	if (!bWithAsset)
		printf("Lelantus [n, M, N] = [%u, %u, %u]\n", cfg.n, cfg.M, N);

	beam::Lelantus::CmListVec lst;
	lst.m_vec.resize(N);

	Point::Native hGen;
	if (bWithAsset)
		beam::Asset::Base(35).get_Generator(hGen);

	Point::Native rnd;
	SetRandom(rnd);

	for (size_t i = 0; i < lst.m_vec.size(); i++, rnd += rnd)
		rnd.Export(lst.m_vec[i]);

	beam::Lelantus::Proof proof;
	proof.m_Cfg = cfg;
	beam::Lelantus::Prover p(lst, proof);

	beam::Sigma::Prover::UserData ud1, ud2;
	for (size_t i = 0; i < _countof(ud1.m_pS); i++)
	{
		do
			SetRandom(ud1.m_pS[i].m_Value);
		while (!ud1.m_pS[i].IsValid());
	}
	p.m_pUserData = &ud1;

	p.m_Witness.V.m_V = 100500;
	p.m_Witness.V.m_R = 4U;
	p.m_Witness.V.m_R_Output = 756U;
	p.m_Witness.V.m_L = 333 % N;
	SetRandom(p.m_Witness.V.m_SpendSk);

	Point::Native pt = Context::get().G * p.m_Witness.V.m_SpendSk;
	Point pt_ = pt;
	Scalar::Native ser;
	beam::Lelantus::SpendKey::ToSerial(ser, pt_);

	pt = Context::get().G * p.m_Witness.V.m_R;
	Tag::AddValue(pt, &hGen, p.m_Witness.V.m_V);
	pt += Context::get().J * ser;
	pt.Export(lst.m_vec[p.m_Witness.V.m_L]);

	if (bWithAsset)
	{
		// add blinding to the asset
		Scalar::Native skGen = 77345U;
		hGen = hGen + Context::get().G * skGen;

		skGen *= p.m_Witness.V.m_V;

		p.m_Witness.V.m_R_Adj = p.m_Witness.V.m_R_Output;
		p.m_Witness.V.m_R_Adj += -skGen;
	}

	std::vector<Point> vG;

	for (uint32_t iCycle = 0; iCycle < 3; iCycle++)
	{
		struct MyExec
			:public beam::ExecutorMT
		{
			uint32_t m_Threads;

			virtual uint32_t get_Threads() override { return m_Threads; }

			virtual void RunThread(uint32_t iThread) override
			{
				ExecutorMT::Context ctx;
				ctx.m_iThread = iThread;
				RunThreadCtx(ctx);
			}
		} ex;

		ex.m_Threads = 1 << iCycle;

		beam::Executor::Scope scope(ex);

		uint32_t t = beam::GetTime_ms();

		Oracle oracle;
		p.Generate(Zero, oracle, &hGen);

		if (!bWithAsset)
			printf("\tProof time = %u ms, Threads=%u\n", beam::GetTime_ms() - t, ex.m_Threads);

		if (iCycle)
		{
			// verify the result is the same (doesn't depend on thread num)
			verify_test(proof.m_Part1.m_vG == vG);
		}
		else
		{
			vG.swap(proof.m_Part1.m_vG);
		}
	}


	{
		Oracle o2;
		Hash::Value hv0;
		proof.m_Cfg.Expose(o2);
		proof.Expose0(o2, hv0);
		ud2.Recover(o2, proof, Zero);

		for (size_t i = 0; i < _countof(ud1.m_pS); i++)
			verify_test(ud1.m_pS[i] == ud2.m_pS[i]);
	}

	typedef InnerProduct::BatchContextEx<4> MyBatch;

	{
		// serialization
		beam::Serializer ser_;
		ser_ & proof;

		proof.m_Part1.m_vG.clear();
		proof.m_Part2.m_vF.clear();

		beam::Deserializer der_;
		der_.reset(ser_.buffer().first, ser_.buffer().second);
		der_ & proof;

		if (!bWithAsset)
			printf("\tProof size = %u\n", (uint32_t)ser_.buffer().second);
	}

	MyBatch bc;

	std::vector<Scalar::Native> vKs;
	vKs.resize(N);

	bool bSuccess = true;

	for (int j = 0; j < 2; j++)
	{
		const uint32_t nCycles = j ? 1 : 11;

		memset0(&vKs.front(), sizeof(Scalar::Native) * vKs.size());

		uint32_t t = beam::GetTime_ms();

		for (uint32_t i = 0; i < nCycles; i++)
		{
			Oracle o2;
			if (!proof.IsValid(bc, o2, &vKs.front(), &hGen))
				bSuccess = false;
		}

		lst.Calculate(bc.m_Sum, 0, N, &vKs.front());

		if (!bc.Flush())
			bSuccess = false;

		if (!bWithAsset)
			printf("\tVerify time %u overlapping proofs = %u ms\n", nCycles, beam::GetTime_ms() - t);
	}

	verify_test(bSuccess);
}

void TestLelantusKeys()
{
	// Test encoding and recognition
	Key::IKdf::Ptr pMaster;
	SetRandom(pMaster);

	Key::IKdf::IPKdf& keyOwner = *pMaster;

	beam::ShieldedTxo::Viewer viewer;
	viewer.FromOwner(keyOwner);

	Key::IKdf::Ptr pPrivateSpendGen;
	viewer.GenerateSerPrivate(pPrivateSpendGen, *pMaster);
	verify_test(viewer.m_pSer->IsSame(*pPrivateSpendGen));

	beam::ShieldedTxo::PublicGen gen;
	gen.FromViewer(viewer);

	beam::ShieldedTxo::Data::SerialParams sprs, sprs2;
	beam::ShieldedTxo txo;

	Point::Native pt;

	sprs.Generate(txo.m_Serial, gen, 115U);
	verify_test(txo.m_Serial.IsValid(pt));
	verify_test(sprs2.Recover(txo.m_Serial, viewer));
	verify_test(!sprs2.m_IsCreatedByViewer);

	sprs.Generate(txo.m_Serial, viewer, 115U);
	verify_test(txo.m_Serial.IsValid(pt));
	verify_test(sprs2.Recover(txo.m_Serial, viewer));
	verify_test(sprs2.m_IsCreatedByViewer);
	verify_test(sprs2.m_SharedSecret == sprs.m_SharedSecret);

	// make sure we get the appropriate private spend key
	Scalar::Native kSpend;
	pPrivateSpendGen->DeriveKey(kSpend, sprs2.m_SerialPreimage);
	Point::Native ptSpend = Context::get().G * kSpend;
	verify_test(ptSpend == sprs2.m_SpendPk);

	beam::ShieldedTxo::Data::OutputParams oprs, oprs2;
	oprs.m_Sender = 1U;
	oprs.m_Value = 3002U;
	oprs.m_Message = Scalar::s_Order;
	oprs.m_AssetID = 18;
	{
		Oracle oracle;
		oprs.Generate(txo, sprs.m_SharedSecret, oracle, gen);
	}
	{
		Oracle oracle;
		verify_test(txo.IsValid(oracle, pt, pt));
	}
	{
		Oracle oracle;
		verify_test(oprs2.Recover(txo, sprs.m_SharedSecret, oracle, viewer));
		verify_test(oprs.m_AssetID == oprs2.m_AssetID);
		verify_test(oprs.m_Sender == oprs2.m_Sender);
		verify_test(oprs.m_Message == oprs2.m_Message);
	}

	oprs.m_Sender.Negate(); // won't fit ECC::Scalar, special handling should be done
	oprs.m_Message.Negate();
	oprs.m_Message.Inc();
	oprs.m_Message.Negate(); // should be 1 less than the order
	oprs.m_AssetID = 0;

	{
		Oracle oracle;
		oprs.Generate(txo, sprs.m_SharedSecret, oracle, gen);
	}
	{
		Oracle oracle;
		verify_test(oprs2.Recover(txo, sprs.m_SharedSecret, oracle, viewer));
		verify_test(oprs.m_Sender == oprs2.m_Sender);
		verify_test(oprs.m_Message == oprs2.m_Message);
		verify_test(oprs.m_Message == oprs2.m_Message);
	}

	{
		Oracle oracle;
		verify_test(txo.IsValid(oracle, pt, pt));
	}
}

void TestAssetProof()
{
	Scalar::Native sk;
	SetRandom(sk);

	Amount val = 400;

	Point::Native genBlinded;

	beam::Asset::Proof proof;
	proof.Create(genBlinded, sk, val, 100500);
	verify_test(proof.IsValid(genBlinded));

	proof.Create(genBlinded, sk, val, 1);
	verify_test(proof.IsValid(genBlinded));

	proof.Create(genBlinded, sk, val, 0);
	verify_test(proof.IsValid(genBlinded));
}

void TestAssetEmission()
{
	const beam::Height hScheme = g_hFork;

	CoinID cidInpBeam (170, 12, beam::Key::Type::Regular);
	CoinID cidInpAsset(53,  15, beam::Key::Type::Regular);
	CoinID cidOutBeam (70,  25, beam::Key::Type::Regular);
	const beam::Amount fee = 100;

	beam::Key::IKdf::Ptr pKdf;
	HKdf::Create(pKdf, Zero);

	Scalar::Native sk, kOffset(Zero);

	beam::Asset::ID nAssetID = 24;
	beam::Asset::Metadata md;
	md.m_Value.assign({ 'a', 'b', '2' });
	md.UpdateHash();

	cidInpAsset.m_AssetID = nAssetID;

	beam::Transaction tx;

	beam::Input::Ptr pInp(new beam::Input);
	CoinID::Worker(cidInpBeam).Create(sk, pInp->m_Commitment, *pKdf);
	tx.m_vInputs.push_back(std::move(pInp));
	kOffset += sk;

	beam::Input::Ptr pInpAsset(new beam::Input);
	CoinID::Worker(cidInpAsset).Create(sk, pInpAsset->m_Commitment, *pKdf);
	tx.m_vInputs.push_back(std::move(pInpAsset));
	kOffset += sk;

	beam::Output::Ptr pOutp(new beam::Output);
	pOutp->Create(hScheme, sk, *pKdf, cidOutBeam, *pKdf);
	tx.m_vOutputs.push_back(std::move(pOutp));
	kOffset += -sk;

	beam::TxKernelStd::Ptr pKrn(new beam::TxKernelStd);
	pKdf->DeriveKey(sk, beam::Key::ID(23123, beam::Key::Type::Kernel));
	pKrn->m_Commitment = Context::get().G * sk;
	pKrn->m_Fee = fee;
	pKrn->m_Height.m_Min = hScheme;
	pKrn->Sign(sk);
	tx.m_vKernels.push_back(std::move(pKrn));
	kOffset += -sk;

	beam::TxKernelAssetEmit::Ptr pKrnAsset(new beam::TxKernelAssetEmit);
	pKdf->DeriveKey(sk, beam::Key::ID(73123, beam::Key::Type::Kernel));
	pKrnAsset->m_AssetID = nAssetID;
	pKrnAsset->m_Value = -static_cast<beam::AmountSigned>(cidInpAsset.m_Value);
	pKrnAsset->m_Height.m_Min = hScheme;
	pKrnAsset->Sign(sk, *pKdf, md);
	tx.m_vKernels.push_back(std::move(pKrnAsset));
	kOffset += -sk;

	tx.m_Offset = kOffset;

	tx.Normalize();

	beam::Transaction::Context::Params pars;
	beam::Transaction::Context ctx(pars);
	ctx.m_Height.m_Min = hScheme;
	bool bIsValid = tx.IsValid(ctx);

	verify_test(bIsValid);
}

void TestAll()
{
	TestUintBig();
	TestHash();
	TestScalars();
	TestPoints();
	TestMultiMac();
	TestSigning();
	TestCommitments();
	TestRangeProof(false);
	TestRangeProof(true);
	TestTransaction();
	TestMultiSigOutput();
	TestCutThrough();
	TestAES();
	TestKdf();
	TestBbs();
	TestDifficulty();
	TestRandom();
	TestFourCC();
	TestTreasury();
	TestAssetProof();
	TestAssetEmission();
	TestLelantus(false);
	TestLelantus(true);
	TestLelantusKeys();
}


struct BenchmarkMeter
{
	const char* m_sz;

	uint64_t m_Start;
	uint64_t m_Cycles;

	uint32_t N;

#ifdef WIN32

	uint64_t m_Freq;

	static uint64_t get_Time()
	{
		uint64_t n;
		QueryPerformanceCounter((LARGE_INTEGER*) &n);
		return n;
	}

#else // WIN32

	static const uint64_t m_Freq = 1000000000;
	static uint64_t get_Time()
	{
		timespec tp;
		verify_test(!clock_gettime(CLOCK_MONOTONIC, &tp));
		return uint64_t(tp.tv_sec) * m_Freq + tp.tv_nsec;
	}

#endif // WIN32


	BenchmarkMeter(const char* sz)
		:m_sz(sz)
		,m_Cycles(0)
		,N(1000)
	{
#ifdef WIN32
		QueryPerformanceFrequency((LARGE_INTEGER*) &m_Freq);
#endif // WIN32

		m_Start = get_Time();
	}

	bool ShouldContinue()
	{
		m_Cycles += N;

		double dt_s = double(get_Time() - m_Start) / double(m_Freq);
		if (dt_s >= 1.)
		{
			printf("%-24s: %.2f us\n", m_sz, dt_s * 1e6 / double(m_Cycles));
			return false;
		}

		if (dt_s < 0.5)
			N <<= 1;

		return true;
	}
};

void RunBenchmark()
{
	Scalar::Native k1, k2;
	SetRandom(k1);
	SetRandom(k2);

/*	{
		BenchmarkMeter bm("scalar.Add");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				k1.Add(k2);

		} while (bm.ShouldContinue());
	}

	{
		BenchmarkMeter bm("scalar.Multiply");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				k1.Mul(k2);

		} while (bm.ShouldContinue());
	}
*/

	{
		BenchmarkMeter bm("scalar.Inverse");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				k1.Inv();

		} while (bm.ShouldContinue());
	}

	Scalar k_;
/*
	{
		BenchmarkMeter bm("scalar.Export");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				k1.Export(k_);

		} while (bm.ShouldContinue());
	}

	{
		BenchmarkMeter bm("scalar.Import");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				k1.Import(k_);

		} while (bm.ShouldContinue());
	}
*/

	{
		ScalarGenerator pwrGen;
		pwrGen.Initialize(7U);

		BenchmarkMeter bm("scalar.7-Pwr");
		SetRandom(k_.m_Value);
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				pwrGen.Calculate(k1, k_);

		} while (bm.ShouldContinue());
	}

	Point::Native p0, p1;

    SetRandom(p0);
    SetRandom(p1);

/*	{
		BenchmarkMeter bm("point.Negate");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				p0 = -p0;

		} while (bm.ShouldContinue());
	}
*/
	{
		BenchmarkMeter bm("point.Double");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				p0 = p0 * Two;

		} while (bm.ShouldContinue());
	}

	{
		BenchmarkMeter bm("point.Add");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				p0 += p1;

		} while (bm.ShouldContinue());
	}

	{
		Mode::Scope scope(Mode::Fast);
		k1 = Zero;

		BenchmarkMeter bm("point.Multiply.Min");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				p0 = p1 * k1;

		} while (bm.ShouldContinue());
	}

	{
		Mode::Scope scope(Mode::Fast);

		BenchmarkMeter bm("point.Multiply.Avg");
		do
		{
			SetRandom(k1);
			for (uint32_t i = 0; i < bm.N; i++)
				p0 = p1 * k1;

		} while (bm.ShouldContinue());
	}

	{
		Mode::Scope scope(Mode::Secure);

		BenchmarkMeter bm("point.Multiply.Sec");
		do
		{
			SetRandom(k1);
			for (uint32_t i = 0; i < bm.N; i++)
				p0 = p1 * k1;

		} while (bm.ShouldContinue());
	}

	{
		// result should be close to prev (i.e. constant-time)
		Mode::Scope scope(Mode::Secure);
		BenchmarkMeter bm("point.Multiply.Sec2");
		do
		{
			k1 = Zero;
			for (uint32_t i = 0; i < bm.N; i++)
				p0 = p1 * k1;

		} while (bm.ShouldContinue());

		p0 = p1;
	}

    Point p_;
    p_.m_Y = 0;

	{
		BenchmarkMeter bm("point.Export");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				p0.Export(p_);

		} while (bm.ShouldContinue());
	}

	{
		BenchmarkMeter bm("point.Import");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				p0.Import(p_);

		} while (bm.ShouldContinue());
	}

	{
		BenchmarkMeter bm("H.Multiply");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				p0 = Context::get().H * uint64_t(-1);

		} while (bm.ShouldContinue());
	}

	{
		k1 = uint64_t(-1);

		Point p2;
		p2.m_X = Zero;
		p2.m_Y = 0;

		while (!p0.Import(p2))
			p2.m_X.Inc();

		BenchmarkMeter bm("G.Multiply");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				p0 = Context::get().G * k1;

		} while (bm.ShouldContinue());
	}

	{
		BenchmarkMeter bm("Commit");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				p0 = Commitment(k1, 275);

		} while (bm.ShouldContinue());
	}

	Hash::Value hv;

	{
		uint8_t pBuf[0x400];
		GenerateRandom(pBuf, sizeof(pBuf));

		BenchmarkMeter bm("Hash.Init.1K.Out");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
			{
				Hash::Processor()
					<< beam::Blob(pBuf, sizeof(pBuf))
					>> hv;
			}

		} while (bm.ShouldContinue());
	}

	Hash::Processor() << "abcd" >> hv;

	Signature sig;
	{
		BenchmarkMeter bm("signature.Sign");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				sig.Sign(hv, k1);

		} while (bm.ShouldContinue());
	}

	p1 = Context::get().G * k1;
	{
		BenchmarkMeter bm("signature.Verify");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				sig.IsValid(hv, p1);

		} while (bm.ShouldContinue());
	}

	Scalar::Native pA[InnerProduct::nDim];
	Scalar::Native pB[InnerProduct::nDim];

	for (size_t i = 0; i < _countof(pA); i++)
	{
		SetRandom(pA[i]);
		SetRandom(pB[i]);
	}

	InnerProduct sig2;

	Point::Native commAB;
	Scalar::Native dot;
	InnerProduct::get_Dot(dot, pA, pB);

	{
		BenchmarkMeter bm("InnerProduct.Sign");
		bm.N = 10;
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				sig2.Create(commAB, dot, pA, pB);

		} while (bm.ShouldContinue());
	}

	{
		BenchmarkMeter bm("InnerProduct.Verify");
		bm.N = 10;
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				sig2.IsValid(commAB, dot);

		} while (bm.ShouldContinue());
	}

	RangeProof::Confidential bp;
	RangeProof::CreatorParams cp;
	SetRandom(cp.m_Seed.V);
	cp.m_Value = 23110;

	{
		BenchmarkMeter bm("BulletProof.Sign");
		bm.N = 10;
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
			{
				Oracle oracle;
				bp.Create(k1, cp, oracle);
			}

		} while (bm.ShouldContinue());
	}

	Point::Native comm = Commitment(k1, cp.m_Value);

	{
		BenchmarkMeter bm("BulletProof.Verify");
		bm.N = 10;
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
			{
				Oracle oracle;
				bp.IsValid(comm, oracle);
			}

		} while (bm.ShouldContinue());
	}

	{
		BenchmarkMeter bm("BulletProof.Verify x100");

		const uint32_t nBatch = 100;
		bm.N = 10 * nBatch;

		typedef InnerProduct::BatchContextEx<4> MyBatch;
		std::unique_ptr<MyBatch> p(new MyBatch);

		InnerProduct::BatchContext::Scope scope(*p);

		do
		{
			for (uint32_t i = 0; i < bm.N; i += nBatch)
			{
				for (uint32_t n = 0; n < nBatch; n++)
				{
					Oracle oracle;
					bp.IsValid(comm, oracle);
				}

				verify_test(p->Flush());
			}

		} while (bm.ShouldContinue());
	}

	{
		AES::Encoder enc;
		enc.Init(hv.m_pData);
		AES::StreamCipher asc;
		asc.Reset();

		uint8_t pBuf[0x400];

		BenchmarkMeter bm("AES.XCrypt-1MB");
		bm.N = 10;
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
			{
				for (size_t nSize = 0; nSize < 0x100000; nSize += sizeof(pBuf))
					asc.XCrypt(enc, pBuf, sizeof(pBuf));
			}

		} while (bm.ShouldContinue());
	}

	{
		uint8_t pBuf[0x400];

		BenchmarkMeter bm("Random-1K");
		bm.N = 10;
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				GenRandom(pBuf, sizeof(pBuf));

		} while (bm.ShouldContinue());
	}


	{
		secp256k1_pedersen_commitment comm2;

		BenchmarkMeter bm("secp256k1.Commit");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				(void) secp256k1_pedersen_commit(g_psecp256k1, &comm2, k_.m_Value.m_pData, 78945, secp256k1_generator_h);

		} while (bm.ShouldContinue());
	}

	{
		BenchmarkMeter bm("secp256k1.G.Multiply");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				secp256k1_ecmult_gen(g_psecp256k1, &p0.get_Raw(), &k1.get());

		} while (bm.ShouldContinue());
	}

}


} // namespace ECC

int main()
{
	g_psecp256k1 = secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);

	beam::Rules::get().CA.Enabled = true;
	beam::Rules::get().pForks[1].m_Height = g_hFork;
	beam::Rules::get().pForks[2].m_Height = g_hFork;
	ECC::TestAll();
	ECC::RunBenchmark();

	secp256k1_context_destroy(g_psecp256k1);

    return g_TestsFailed ? -1 : 0;
}
