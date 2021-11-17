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
#include "../../utility/byteorder.h"
#include "../../utility/executor.h"

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

#include <ethash/keccak.hpp>

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


namespace ECC {

typedef beam::CoinID CoinID;

template <uint32_t nBytes>
void SetRandom(beam::uintBig_t<nBytes>& x)
{
	GenRandom(x);
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
	GenRandom(&x, sizeof(x));
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

template <typename T>
void TestByteOrderT()
{
	uint8_t pBytesBE[sizeof(T)];

	T val = 0;
	for (uint8_t i = 0; i < sizeof(T); i++)
	{
		pBytesBE[i] = (i + 1) * 0x11;
		val <<= 8;
		val |= pBytesBE[i];
	}

	union U {
		T val2;
		uint8_t pBytes[sizeof(T)];
	} u;
	static_assert(sizeof(u) == sizeof(T));

	u.val2 = beam::ByteOrder::to_be(val);
	verify_test(beam::ByteOrder::from_be(u.val2) == val);

	verify_test(!memcmp(pBytesBE, u.pBytes, sizeof(T)));

	u.val2 = beam::ByteOrder::to_le(val);
	verify_test(beam::ByteOrder::from_le(u.val2) == val);

	for (unsigned int i = 0; i < sizeof(T); i++)
	{
		verify_test(pBytesBE[i] == u.pBytes[sizeof(T) - 1 - i]);
	}
}

void TestByteOrder()
{
	TestByteOrderT<uint16_t>();
	TestByteOrderT<uint32_t>();
	TestByteOrderT<uint64_t>();

	// for uint8_t the byte ordering is irrelevant, there's nothing to reorder. Check that the value isn't distorted
	uint8_t x = 0xAB;
	verify_test(beam::ByteOrder::to_be(x) == x);
	verify_test(beam::ByteOrder::from_be(x) == x);
	verify_test(beam::ByteOrder::to_le(x) == x);
	verify_test(beam::ByteOrder::from_le(x) == x);
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

	memcpy(reinterpret_cast<void*>(bctx.m_pPtsBuf), pPts, sizeof(pPts));

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
	memcpy(reinterpret_cast<void*>(bctx.m_pPtsBuf), pPts, sizeof(pPts));

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

void PrintSizeSerialized(const char* sz, const beam::SerializerSizeCounter& ssc)
{
	printf("%s size = %u\n", sz, (uint32_t)ssc.m_Counter.m_Value);
}

template <typename T>
void WriteSizeSerialized(const char* sz, const T& t)
{
	beam::SerializerSizeCounter ssc;
	ssc & t;

	PrintSizeSerialized(sz, ssc);
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

	if (!bCustomTag) // coinbase with asset isn't allowed
	{
		beam::Output outp;
		outp.m_Coinbase = true; // others may be disallowed
		outp.Create(g_hFork, sk, kdf, cid, kdf, beam::Output::OpCode::Public);
		verify_test(outp.IsValid(g_hFork, comm));
		WriteSizeSerialized("Out-UTXO-Public", outp);

		beam::SerializerSizeCounter ssc;
		yas::detail::saveRecovery(ssc, outp);
		PrintSizeSerialized("Out-UTXO-Public-RecoveryOnly", ssc);
	}
	{
		beam::Output::User user, user2;
		for (size_t i = 0; i < _countof(user.m_pExtra); i++)
		{
			ECC::Scalar::Native s;
			SetRandom(s);
			user.m_pExtra[i] = s;
		}
		beam::Output outp;
		outp.Create(g_hFork, sk, kdf, cid, kdf, beam::Output::OpCode::Standard, &user);
		verify_test(outp.IsValid(g_hFork, comm));
		WriteSizeSerialized("Out-UTXO-Confidential", outp);

		beam::SerializerSizeCounter ssc;
		yas::detail::saveRecovery(ssc, outp);
		PrintSizeSerialized("Out-UTXO-Confidential-RecoveryOnly", ssc);

		CoinID cid2;
		verify_test(outp.Recover(g_hFork, kdf, cid2, &user2));
		verify_test(cid == cid2);

		for (size_t i = 0; i < _countof(user.m_pExtra); i++)
			verify_test(user.m_pExtra[i] == user2.m_pExtra[i]);
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
    pKernel->m_Height.m_Min = g_hFork;
    pKernel->m_Height.m_Max = g_hFork + 150;
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
		GenRandom(&d, sizeof(d));

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

void TestProtoVer()
{
	using namespace beam;

	const uint32_t nMskFlags = ~proto::LoginFlags::Extension::Msk;

	for (uint32_t nVer = 0; nVer < 200; nVer++)
	{
		uint32_t nFlags = 0;
		proto::LoginFlags::Extension::set(nFlags, nVer);
		verify_test(!(nFlags & nMskFlags)); // should not leak

		uint32_t nVer2 = proto::LoginFlags::Extension::get(nFlags);
		verify_test(nVer == nVer2);

		nFlags = nMskFlags;
		proto::LoginFlags::Extension::set(nFlags, nVer);
		verify_test((nFlags & nMskFlags) == nMskFlags); // should not loose flags

		nVer2 = proto::LoginFlags::Extension::get(nFlags);
		verify_test(nVer == nVer2);
	}
}

void TestRandom()
{
	PseudoRandomGenerator::Scope scopePrg(nullptr); // restore std

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

void TestLelantus(bool bWithAsset, bool bMpc)
{
	beam::Lelantus::Cfg cfg; // default

	bool bSpecial = bWithAsset || bMpc;
	if (bSpecial)
	{
		// set other parameters. Test small set (make it run faster)
		cfg.n = 3;
		cfg.M = 4; // 3^4 = 81
	}

	const uint32_t N = cfg.get_N();
	if (!bSpecial)
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
	p.m_Sigma.m_pUserData = &ud1;

	p.m_Witness.m_V = 100500;
	p.m_Witness.m_R = 4U;
	p.m_Witness.m_R_Output = 756U;
	p.m_Witness.m_L = 333 % N;
	SetRandom(p.m_Witness.m_SpendSk);

	Point::Native pt = Context::get().G * p.m_Witness.m_SpendSk;
	Point ptSpendPk = pt;
	Scalar::Native ser;
	beam::Lelantus::SpendKey::ToSerial(ser, ptSpendPk);

	pt = Context::get().G * p.m_Witness.m_R;
	Tag::AddValue(pt, &hGen, p.m_Witness.m_V);
	pt += Context::get().J * ser;
	pt.Export(lst.m_vec[p.m_Witness.m_L]);

	if (bMpc)
	{
		proof.m_SpendPk = ptSpendPk;

		pt = Context::get().G * p.m_Witness.m_R_Output;
		Tag::AddValue(pt, &hGen, p.m_Witness.m_V);
		proof.m_Commitment = pt;
	}

	if (bWithAsset)
	{
		// add blinding to the asset
		Scalar::Native skGen = 77345U;
		hGen = hGen + Context::get().G * skGen;

		skGen *= p.m_Witness.m_V;

		p.m_Witness.m_R_Adj = p.m_Witness.m_R_Output;
		p.m_Witness.m_R_Adj += -skGen;
	}

	beam::ByteBuffer bufProof;

	PseudoRandomGenerator prg = *PseudoRandomGenerator::s_pOverride; // save prnd state

	for (uint32_t iCycle = 0; iCycle < 3; iCycle++)
	{
		beam::ExecutorMT_R ex;
		ex.set_Threads(1 << iCycle);

		beam::Executor::Scope scope(ex);

		uint32_t t = beam::GetTime_ms();

		*PseudoRandomGenerator::s_pOverride = prg; // restore prnd state

		Oracle oracle;
		Hash::Value seed = Zero;

		if (bMpc)
		{
			// proof phase1 generation, Sigma with 0 blinding factor, SigGen is deferred
			p.Generate(seed, oracle, &hGen, beam::Lelantus::Prover::Phase::Step1);

			// Complete SigGen (normally this is done on another device, we just reuse this code)
			p.GenerateSigGen(&hGen);

			Scalar::Native sVal = 432123U; // custom tau[0]
			Point::Native ptG0;
			verify_test(ptG0.Import(proof.m_Part1.m_vG.front()));
			ptG0 += Context::get().G * sVal;
			proof.m_Part1.m_vG.front() = ptG0;

			Oracle o2(oracle); // copy
			proof.m_Part1.Expose(o2);

			Scalar::Native x;
			o2 >> x; // challenge

			// calculate initial zR
			Scalar::Native xPwr(x);

			for (uint32_t j = 1; j < proof.m_Cfg.M; j++)
				xPwr *= x;

			x = p.m_Witness.m_R - p.m_Witness.m_R_Output;

			sVal = -sVal;
			sVal += x * xPwr;

			proof.m_Part2.m_zR = sVal;


			p.Generate(seed, oracle, &hGen, beam::Lelantus::Prover::Phase::Step2);
		}
		else
			p.Generate(seed, oracle, &hGen);

		if (!bSpecial)
			printf("\tProof time = %u ms, Threads=%u\n", beam::GetTime_ms() - t, ex.get_Threads());

		// serialization
		beam::Serializer ser_;
		ser_ & proof;

		if (iCycle)
		{
			// verify the result is the same (doesn't depend on thread num)
			beam::SerializeBuffer sb = ser_.buffer();
			verify_test((sb.second == bufProof.size()) && !memcmp(sb.first, &bufProof.front(), sb.second));
		}
		else
		{
			ser_.swap_buf(bufProof);
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
		// deserialization
		proof.m_Part1.m_vG.clear();
		proof.m_Part2.m_vF.clear();

		beam::Deserializer der_;
		der_.reset(bufProof);
		der_ & proof;

		if (!bSpecial)
			printf("\tProof size = %u\n", (uint32_t) bufProof.size());
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

		if (!bSpecial)
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
	viewer.FromOwner(keyOwner, 0);

	Key::IKdf::Ptr pPrivateSpendGen;
	viewer.GenerateSerPrivate(pPrivateSpendGen, *pMaster, 0);
	verify_test(viewer.m_pSer->IsSame(*pPrivateSpendGen));

	beam::ShieldedTxo::PublicGen gen;
	gen.FromViewer(viewer);

	beam::ShieldedTxo::Data::TicketParams sprs, sprs2;
	beam::ShieldedTxo txo;

	Point::Native pt;

	sprs.Generate(txo.m_Ticket, gen, 115U);
	verify_test(txo.m_Ticket.IsValid(pt));
	verify_test(sprs2.Recover(txo.m_Ticket, viewer));
	verify_test(!sprs2.m_IsCreatedByViewer);

	sprs.Generate(txo.m_Ticket, viewer, 115U);
	verify_test(txo.m_Ticket.IsValid(pt));
	verify_test(sprs2.Recover(txo.m_Ticket, viewer));
	verify_test(sprs2.m_IsCreatedByViewer);
	verify_test(sprs2.m_SharedSecret == sprs.m_SharedSecret);

	// make sure we get the appropriate private spend key
	Scalar::Native kSpend;
	pPrivateSpendGen->DeriveKey(kSpend, sprs2.m_SerialPreimage);
	Point::Native ptSpend = Context::get().G * kSpend;
	verify_test(ptSpend == sprs2.m_SpendPk);

	beam::ShieldedTxo::Data::OutputParams oprs, oprs2;
	oprs.m_Value = 3002U;
	oprs.m_AssetID = 18;
	oprs.m_User.m_Sender = 1U; // fits
	oprs.m_User.m_pMessage[0] = Scalar::s_Order; // won't fit
	oprs.m_User.m_pMessage[1] = 1U; // fits
	{
		Oracle oracle;
		oprs.Generate(txo, sprs.m_SharedSecret, g_hFork, oracle);
	}
	{
		Oracle oracle;
		verify_test(txo.IsValid(oracle, g_hFork, pt, pt));
	}
	{
		Oracle oracle;
		verify_test(oprs2.Recover(txo, sprs.m_SharedSecret, g_hFork, oracle));
		verify_test(oprs.m_AssetID == oprs2.m_AssetID);
		verify_test(!memcmp(&oprs.m_User, &oprs2.m_User, sizeof(oprs.m_User)));
	}

	oprs.m_User.m_Sender.Negate(); // won't fit ECC::Scalar, special handling should be done
	oprs.m_User.m_pMessage[0].Negate();
	oprs.m_User.m_pMessage[0].Inc();
	oprs.m_User.m_pMessage[0].Negate(); // should be 1 less than the order
	oprs.m_User.m_pMessage[1] = Scalar::s_Order; // won't feet

	{
		Oracle oracle;
		oprs.Generate(txo, sprs.m_SharedSecret, g_hFork, oracle);
	}
	{
		Oracle oracle;
		verify_test(oprs2.Recover(txo, sprs.m_SharedSecret, g_hFork, oracle));
		verify_test(!memcmp(&oprs.m_User, &oprs2.m_User, sizeof(oprs.m_User)));
	}

	{
		Oracle oracle;
		verify_test(txo.IsValid(oracle, g_hFork, pt, pt));
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
	TestByteOrder();
	TestUintBig();
	TestHash();
	TestScalars();
	TestPoints();
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
	TestProtoVer();
	TestRandom();
	TestFourCC();
	TestTreasury();
	TestAssetProof();
	TestAssetEmission();
	TestLelantus(false, false);
	TestLelantus(false, true);
	TestLelantus(true, false);
	TestLelantus(true, true);
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
		GenRandom(pBuf, sizeof(pBuf));

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

		PseudoRandomGenerator::Scope scopePrg(nullptr); // restore std

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

namespace
{
	static constexpr size_t hash_size = 32;
	typedef std::array<uint8_t, hash_size> h256_t;

	// Validates prefix, compares "sharedNibbles(or key-end if leaf node)" and returns TrieKey offset
	int RemovePrefix(const beam::ByteBuffer& encodedPath, const beam::ByteBuffer& key, uint8_t keyPos)
	{
		// encodedPath to nibbles
		beam::ByteBuffer nibbles(2 * encodedPath.size());
		for (uint8_t i = 0; i < encodedPath.size(); i++)
		{
			nibbles[i * 2] = encodedPath[i] >> 4;
			nibbles[i * 2 + 1] = encodedPath[i] & 15;
		}

		// checking prefix
		/* 0 - even extension node
		 * 1 - odd extension node
		 * 2 - even leaf node
		 * 3 - odd leaf node
		*/
		assert(nibbles[0] <= 3 && "The proof has the incorrect format!");

		// even extension node OR even leaf node -> skips 2 nibbles
		uint8_t offset = (nibbles[0] == 0 || nibbles[0] == 2) ? 2 : 1;

		// checking that key contains nibbles
		if (std::equal(nibbles.cbegin() + offset, nibbles.cend(),
					   key.cbegin() + keyPos, key.cbegin() + keyPos + (nibbles.size() - offset)))
		{
			return static_cast<int>(nibbles.size() - offset);
		}

		assert(false && "encodedPath not found in the key");
		return -1;
	}

	beam::ByteBuffer GetTrieKeyInNibbles(const beam::ByteBuffer& key)
	{
		// get keccak256
		auto hash = ethash::keccak256(key.data(), key.size());

		// to nibbles
		beam::ByteBuffer nibbles(2 * sizeof(hash.bytes));

		for (uint8_t i = 0; i < sizeof(hash.bytes); i++)
		{
			nibbles[i * 2] = hash.bytes[i] >> 4;
			nibbles[i * 2 + 1] = hash.bytes[i] & 15;
		}
		return nibbles;
	}

	beam::ByteBuffer GetReceiptTrieKeyInNibbles(const beam::ByteBuffer& key)
	{
		// to nibbles
		beam::ByteBuffer nibbles(2 * key.size());

		for (uint8_t i = 0; i < key.size(); i++)
		{
			nibbles[i * 2] = key[i] >> 4;
			nibbles[i * 2 + 1] = key[i] & 15;
		}
		return nibbles;
	}

	// extract item length and set "position" to the begin of the item data
	uint32_t RLPDecodeLength(const beam::ByteBuffer& input, uint32_t& position)
	{
		constexpr uint8_t kOffsetList = 0xc0;
		constexpr uint8_t kOffsetString = 0x80;

		uint32_t offset = kOffsetList;
		if (input[position] < kOffsetList)
		{
			if (input[position] < 0x80)
			{
				return 1;
			}
			offset = kOffsetString;
		}

		uint32_t length = 0;
		if (input[position] <= offset + 55)
		{
			length = input[position++] - offset;
		}
		else
		{
			uint32_t now = input[position++] - offset - 55;
			for (uint32_t i = 0; i < now; i++)
			{
				length = length * 256 + input[position++];
			}
		}
		return length;
	}

	std::vector<beam::ByteBuffer> RLPDecodeList(const beam::ByteBuffer& input)
	{
		uint32_t index = 0;
		uint32_t length = RLPDecodeLength(input, index);
		uint32_t end = length + index;
		std::vector<beam::ByteBuffer> result;

		while (index < end)
		{
			// get item length
			uint32_t itemLength = RLPDecodeLength(input, index);
			// copy item
			result.push_back({input.cbegin() + index, input.cbegin() + index + itemLength });

			index += itemLength;
		}
		return result;
	}

	beam::ByteBuffer PadStorageKey(const beam::ByteBuffer& key)
	{
		beam::ByteBuffer result(32);
		std::copy(key.cbegin(), key.cend(), result.end() - key.size());
		return result;
	}

	std::pair<beam::ByteBuffer, bool> VerifyEthProof(const beam::ByteBuffer& trieKey, const std::vector<beam::ByteBuffer>& proof, const beam::ByteBuffer& rootHash)
	{
		assert(rootHash.size() == 32);
		beam::ByteBuffer newExpectedRoot = rootHash;
		uint8_t keyPos = 0;

		for (uint32_t i = 0; i < proof.size(); i++)
		{
			// TODO: is always a hash? check some samples with currentNode.size() < 32 bytes
			std::vector<beam::ByteBuffer> currentNode = RLPDecodeList(proof[i]);
			auto nodeHash = ethash::keccak256(proof[i].data(), proof[i].size());

			if (!std::equal(std::begin(newExpectedRoot), std::end(newExpectedRoot), std::begin(nodeHash.bytes)))
			{
				assert(false && "newExpectedRoot != keccak(rlp.encodeList(currentNode))");
				return std::make_pair(beam::ByteBuffer(), false);
			}

			if (keyPos > trieKey.size())
			{
				assert(false && "keyPos > trieKey.size()");
				return std::make_pair(beam::ByteBuffer(), false);;
			}
			switch (currentNode.size())
			{
				// branch node
			case 17:
			{
				if (keyPos == trieKey.size())
				{
					if (i == proof.size() - 1)
						// value stored in the branch
						return std::make_pair(currentNode.back(), true);
					else
						assert(false && "i != proof.size() - 1");

					return std::make_pair(beam::ByteBuffer(), false);
				}
				newExpectedRoot = currentNode[trieKey[keyPos]];
				keyPos += 1;
				break;
			}
			// leaf or extension node
			case 2:
			{
				int offset = RemovePrefix(currentNode[0], trieKey, keyPos);

				if (offset == -1)
					return std::make_pair(beam::ByteBuffer(), false);

				keyPos += static_cast<uint8_t>(offset);
				if (keyPos == trieKey.size())
				{
					// leaf node
					if (i == proof.size() - 1)
					{
						return std::make_pair(currentNode[1], true);
					}
					return std::make_pair(beam::ByteBuffer(), false);
				}
				else
				{
					// extension node
					newExpectedRoot = currentNode[1];
				}
				break;
			}
			default:
			{
				assert(false && "Unexpected node type, all nodes must be length 17 or 2");
				return std::make_pair(beam::ByteBuffer(), false);
			}
			}
		}
		assert(false && "Length of Proof is not enough");
		return std::make_pair(beam::ByteBuffer(), false);
	}
} // namespace

void TestEthProof()
{
	std::cout << std::endl << "TestEthProof: " << std::endl;
	{
		std::cout << "event proof " << std::endl;
		beam::ByteBuffer key = beam::from_hex("80");

		std::vector<beam::ByteBuffer> proof;
		proof.push_back(beam::from_hex("f901b0822080b901aaf901a70183bc047bb9010000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000080000000000000008000000000000000000000000000000000000000000000000020000000000000000008800000000000000000000000010000000000000000000000000000000000000000000000000000000000000000000000000800000000000000000000000000000000000000000000000008000000000000000000002000000000000000000000000000000000000000000004000000020000000000000000000000000000000000000000000004000000000000000000000f89df89b940000000000004946c0e9f43f4dee607b0ef1fa1cf863a0ddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3efa00000000000000000000000000000000000000000000000000000000000000000a000000000000000000000000066f1c77db874d72e6645f1dced8eea05819b4388a00000000000000000000000000000000000000000000000000000000000000153"));

		beam::ByteBuffer rootHash = beam::from_hex("f2e6b1bff02ab64889aa31d4b9566f0dde24d8e4d3e6758a5df7d0429d17fe48");
		auto result = VerifyEthProof(GetReceiptTrieKeyInNibbles(key), proof, rootHash);
		std::cout << "isValid: " << result.second << ", data: " << beam::to_hex(result.first.data(), result.first.size()) << std::endl;
	}

	{
		std::cout << "event proof " << std::endl;
		beam::ByteBuffer key = beam::from_hex("81b2"); // rlp encoded txIndex = 'b2'

		std::vector<beam::ByteBuffer> proof;
		proof.push_back(beam::from_hex("f90131a03560d9eed3444461ddfb5d160e0fb8a219f9024712a828021495d37f174559f2a07756074051552beda92a9e740b8e745b7e00d739055f8948d593dc00fbd3f7bfa0903307d973b0142fcd73b3356f0b722a8677ea1bde388dfc1aa9684397dd2f87a0a38b1ba99bdf4139007e99539f78fbb1ffb11c77390f97a0ce498ab2732cc5eba0cbf8a39475c1d6bb50748625194c31d8fd4a99881f5b031a02d78b7ad1f6f2dda01c7e262956bff1224f243ac306a5a36167604dba1548e409cb71450f2fb91140a0e9ee479aedfffb0c89c9e374e674199222ba81dd0b0225a4bffcc3bb8896e3c8a00e6ad449bd6eb9deae455939141b17f9119c8500a7d61ae58509b62d0968088fa0b4a234a3ecb24298915e64e2a1787a1723516706232e3eaafe40590c49211a4a8080808080808080"));
		proof.push_back(beam::from_hex("f871a0f95633e95a8558a1f500445d05c19a147a7ef922379fe464c254ff55185ae5daa02e3d1d6607511ac52bc09b00b52f92c3d878814d1177dd9813e3e2dfb2107acea0bebe2a45f302d818753d6eb2eb4958fd1d34bd94f97114ce38335e52f438a82b8080808080808080808080808080"));
		proof.push_back(beam::from_hex("f901118080808080808080a08843fb4bf42cd300485f04d507e98a802623aca141cd61c6a73503cfd6f0177ea0ac8c061c9a3f9d2e6f34dde923d4f0b7f5e5f216934056b89925ff520b181285a0c4baa35c1094223c148fb8f8ed54cfd9b310a72fdb1988e71c59935352b93d4fa0fab71929e2082ad0b8ba4d128a6421f575a3ef2d6a9eb59f3e82eef7e3ac39aca0223e2c8f3c24c63aece14f7f2caa2886d1b5c3f6dc5c113d09f0946e2e5394b3a037f2c55ff64db846deb1134da5c9353d3c106bf70d0d3b52cfda4700442025f0a0b2f6e85b04ad9da11f7481890fc09661e5ff5a0914aef8b69998de553261a543a0e1a76f686577614dd88e8b296d95e523da906d40d9a7fd743273af56c77b7b6f80"));
		proof.push_back(beam::from_hex("f90211a0c69f64874dbe6a51102d2ff0128ee73789cca16645fe91751baa39cea188e80fa08068020d46ee81e30e0b6c4c8efc6c499f3d569011dc1ee4c59464b863e8c16fa0c2b8aa794e3d11ac07e289227ea9e53f204b57f871b6865505e81592c3e054a1a059caca8bfcb20aefcd8e83a10d69bf5523879edd2da89b5f93bbbdd20807f5eaa0ec482b119c6aad6517ccb50e2aaef3d9ccd9088b3270e5a3ea55e503a419e38ca0d9533bc5d3a338346f40b90b50237d48afd3e3262810129c7b6171da510aaf68a03273c53d5c9e084ef9cc5ecca211ecf145827414577225e7d9453514b9b14f49a0e68d0907dbf5a984785544c62c47ca3aea5436c53d8f2d022a90343ba1b16fe6a0422e65f5e046a14d94bd2811877f676d2cca9ad7feceda07aa5ddcc4697bebd2a029765e6d40bffda57b5050939f86cbd9a288ef6c1e6a6499479214a01056c8b2a01d37fadcef7cf5124579fedcc8000d7e149feb8ea88c99a4d3cc9177fbf3b837a017e8d9b19a9af8f98e381b988520d18114ee023d450bea11138c9ecd1bc37876a0239ef3ea46ee1f7fc8ba7535acf9554a5c26967c1e196a45bbbcdde6f0355719a0e96c6c1e3f8f9b87b99595cfecf9bfc98769a08f60de7f2d49400a2596558cb0a09e29700ebdfca58740871d10040d21bc8fa86c4c63a9234d554ab3f69c33ccffa0da550e277223653019e81070ffeeda8391d4829c6c266a13b5d86d8b4a3c305d80"));
		proof.push_back(beam::from_hex("f905a320b9059ff9059c0183884e49b9010000000002000000000000000020000000000000000000000000000040000000000000000000000000000000100000000002000000080020000000000000000000000000000000000808200008400000000000000008000000000000008000000010000000000000000000180000000000000000000000002000000010000800000000008000000000000000000000000000000001010000000000000000000000000000000000200000000000800000000000080000020000000000008000000000000002200000020000000000000000000002000000000000000000000000000000200000000000008000000000000000000000000000400000000000000000f90491f89b94a0b86991c6218b36c1d19d4a2e9eb0ce3606eb48f863a0ddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3efa00000000000000000000000006c6bc977e13df9b0de53b251522280bb72383700a0000000000000000000000000a877184a3d42bf121c8182c3363427337ec49a3ba00000000000000000000000000000000000000000000000000000000023c34600f89b946b175474e89094c44da98b954eedeac495271d0ff863a0ddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3efa000000000000000000000000060594a405d53811d3bc4766596efd80fd545a270a00000000000000000000000006c6bc977e13df9b0de53b251522280bb72383700a000000000000000000000000000000000000000000000002082e413780b5db14af87a94c02aaa39b223fe8d0a0e5c4f27ead9083c756cc2f842a0e1fffcc4923d04b559f4d29a8bfc6cda04eb5b0d3c460751c2402c5c5cc9109ca0000000000000000000000000e592427a0aece92de3edee1f18e0157c05861564a00000000000000000000000000000000000000000000000000341252927ea3fbff89b94c02aaa39b223fe8d0a0e5c4f27ead9083c756cc2f863a0ddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3efa0000000000000000000000000e592427a0aece92de3edee1f18e0157c05861564a000000000000000000000000060594a405d53811d3bc4766596efd80fd545a270a00000000000000000000000000000000000000000000000000341252927ea3fbff9011c9460594a405d53811d3bc4766596efd80fd545a270f863a0c42079f94a6350d7e6235f29174924f928cc2ac818eb64fed8004e115fbcca67a0000000000000000000000000e592427a0aece92de3edee1f18e0157c05861564a00000000000000000000000006c6bc977e13df9b0de53b251522280bb72383700b8a0ffffffffffffffffffffffffffffffffffffffffffffffdf7d1bec87f4a24eb60000000000000000000000000000000000000000000000000341252927ea3fbf000000000000000000000000000000000000000005100f011f27a5e41938a53c0000000000000000000000000000000000000000000003a215dfb35f0e0caf8efffffffffffffffffffffffffffffffffffffffffffffffffffffffffffecd7af9011c946c6bc977e13df9b0de53b251522280bb72383700f863a0c42079f94a6350d7e6235f29174924f928cc2ac818eb64fed8004e115fbcca67a0000000000000000000000000e592427a0aece92de3edee1f18e0157c05861564a0000000000000000000000000a877184a3d42bf121c8182c3363427337ec49a3bb8a000000000000000000000000000000000000000000000002082e413780b5db14affffffffffffffffffffffffffffffffffffffffffffffffffffffffdc3cba000000000000000000000000000000000000000000000010c9047004407a08eaaf000000000000000000000000000000000000000000001206bbf0ef217797f1e7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffbc8a5"));

		beam::ByteBuffer rootHash = beam::from_hex("885c604594f953fb56cdc3204f2c91d0992bd60912717c29c2601ed83b34bd45");
		auto result = VerifyEthProof(GetReceiptTrieKeyInNibbles(key), proof, rootHash);
		std::cout << "isValid: " << result.second << ", data: " << beam::to_hex(result.first.data(), result.first.size()) << std::endl;
	}

	{
		std::cout << "storage proof " << std::endl;
		beam::ByteBuffer key = PadStorageKey(beam::from_hex(""));

		std::vector<beam::ByteBuffer> proof;
		proof.push_back(beam::from_hex("f8518080a036bb5f2fd6f99b186600638644e2f0396989955e201672f7e81e8c8f466ed5b98080808080808080808080a0f70bd5b82fa5222804070e8400da42b4ae39eb527a42f19106acf68ea58a4eb38080"));
		proof.push_back(beam::from_hex("e5a0390decd9548b62a8d60345a988386fc84ba6bc95484008f6362f93160ef3e563838204d2"));

		beam::ByteBuffer rootHash = beam::from_hex("c14953a64f69632619636fbdf327e883436b9fd1b1025220e50fb70ab7d2e2a8");
		auto result = VerifyEthProof(GetTrieKeyInNibbles(key), proof, rootHash);
		std::cout << "isValid: " << result.second << ", data: " << beam::to_hex(result.first.data(), result.first.size()) << std::endl;
		auto expected = beam::from_hex("8204d2");
		verify_test(expected == result.first);
	}
	{
		std::cout << "storage proof " << std::endl;
		beam::ByteBuffer key = PadStorageKey(beam::from_hex("d471a47ea0f50e55ea9fc248daa217279ed7ea3bb54c9c503788b85e674a93d1"));

		std::vector<beam::ByteBuffer> proof;
		proof.push_back(beam::from_hex("f8518080a036bb5f2fd6f99b186600638644e2f0396989955e201672f7e81e8c8f466ed5b98080808080808080808080a0f70bd5b82fa5222804070e8400da42b4ae39eb527a42f19106acf68ea58a4eb38080"));
		proof.push_back(beam::from_hex("e5a032bb43bc21210266ddc05034de2daca42e3506a91e276b749025c15863ae53858382162e"));

		beam::ByteBuffer rootHash = beam::from_hex("c14953a64f69632619636fbdf327e883436b9fd1b1025220e50fb70ab7d2e2a8");
		auto result = VerifyEthProof(GetTrieKeyInNibbles(key), proof, rootHash);
		std::cout << "isValid: " << result.second << ", data: " << beam::to_hex(result.first.data(), result.first.size()) << std::endl;
		auto expected = beam::from_hex("82162e");
		verify_test(expected == result.first);
	}

	{
		std::cout << "storage proof " << std::endl;
		// storage proof, block 12343176, eth_getProof: ["0xdAC17F958D2ee523a2206206994597C13D831ec7",["","1"]
		beam::ByteBuffer key = PadStorageKey(beam::from_hex("01"));

		std::vector<beam::ByteBuffer> proof;
		proof.push_back(beam::from_hex("f90211a07a2ce39a11358e8a7f8da45b80d08afa60097a1deedb5992d39e2d4d769da8aaa03ab41e236ce16bd6dc15eb163d3aea699c5e359c2ef2c1cf5631214cad693be0a00d12ad9adcf36a3d3bd21e1a02ccf34aebb5b77dacbb4b4761cfa70db4f04c1ea0e3f63e97e6c62f6280e09b010bc3fdc8eef3c683e3c8cb8d38b7b60d9fed52a2a09e39e31a51e30ab13f5d4bbce7b9026edf41b4bc6cd38cf1a7a2b229b95dcf27a0400df84090cd315346809102419e002b647057836a311e98e7d59712f68f878ba0370e9b56601dda64ffe83193faabeeda91d8b99c4d9b35c862b6dbddd48a8835a055224420d68561714b632a9eba019afb29556b1f68d3165863670e98fb222bada05a004836b53f2bd41a0ac545fc8c7d33532f77e0b8677512d1065ea527105a69a08acd7743fb04fb6b9380fdb4787935da854a39af79bb5ae732472cda74c52899a07e692859c910664aaeec226d687f89c29a61748ecd626b146dbf37232b6f919fa0a4578e1e5f0db4cffc27a375ca543895037dca18e7de3ba4d30a9a3105146f73a0df35ebfadd932a8ac5cf17a1208a48ccfb9662f5506d918bb69cb794c9669675a088399bd0ca90dbe0fc41b460cbd9e7e5f3e394cafd0961fefa54ba4c825121dea0b875465890ebfcb9b429d58ea6031d3d1ecd3873939a7a850da43d571ac165ada058a29ede92418a9ea6ac33b1ca58c46b8822bfed1dcab8d676593e774cbd5a9780"));
		proof.push_back(beam::from_hex("f90211a0c93f7eee3b23466a9e574f047b3202beadef419e7b5ae5c2d2c8670c21f4d36ca077f099b6af9d7e9f96be0f965591a0ebf8304c31977eaaeda4f8fb3fff4367b2a06567f3d9d2dc9bbdbfeb247c506136660d5fef2a81c5c0c8f3d342e382e03ae5a0c3b2b2f552a2669e5d50c31242bd95c5f50c070cb9a47c9d3a0cce1e64b42e1fa0b9575ba0abf30b81180f64f3a8146fb664eac8bd996fa18975df295830d8a302a0e332e722e52a4e83bd7017636e9347b39850bd0019be467e802046221c4d4440a06d6fa77b796c1ba876a437256820180fc63cd16b82e7aae2abdc2b665d912086a0589c5aec6dd08717b07a324b9cc93011ba7bab0bcc629ad87953a1eee8b75154a074d54cd625b7622d9ee6f8fde565679dbf594e7da27d61d049f6548cf8506a79a0178c53711818072fcd5fe03e537b95f778b0521ad03f35400987690aeec8eaf1a024d312acc5938625223d385c87d25ea0ba2204ff03e32bfa2f44913d4e3cb05da0c0e71a34335c9161137656ef3758908aa2c00a6cae2acc3f1476df7c38ffe003a00b5eb8c2b6f831657a490e6bd65c1c620f60bf2ff557d6b4f1670e2417d7cd0ea0d9ccd4922ec03e0b25923aee5ccaf10d40141948c08ee0bc41c720ad921e58a0a0d2b85b7d1dd59ed34f84fcb2820a169bbaf6d567d973e3976d010f201dc2aca2a0aea08ff453c5e2391eca9f7f421e3f6dc1bde9ca2e6a6647469f6012bf7fa61980"));
		proof.push_back(beam::from_hex("f90211a0cf7e578c80b1b042acca94a884a1e120ad1f4a3783e4b6adff90577483e996baa0c1422e28b753610c2006cc9072631727620bb62c4167dc396b14c154cf968319a07f425517a674b47de186f9ee2359fca2271b95b4cbca13172d7d2cc07636d748a0c765affa494db1f9d764f486a017e9394b3e4949e4fa1b4e7a6e1f2fa4caa721a006b32ffa240ea616ec8e8cb7b3e506911dc6ddbf0e6876a8df305addac90d1dfa05214249b5491de4499d74952b97608687105a66213163b649a752702daee5636a0c62156a939b2e029b99948123bce8ac45bc896c438f1e01bf16fae303d532607a02302e2613f97766e189fcc6ebfc3dcaa0bd5dc95b3ebab8a79b5cd7e10e16fdea00fff19f604e24da2537fb6248dc62db56911aa7dc955bcd06399a4eabd002801a0edec90d330c4f12dd9f87fe6713e7985486d951242702340f70f0bc2ecaeafc2a069f09fa0f261b1906d8d1b4f5e5b5ce233229de1a2a56dab3255b08fa894915ca05d0d06e9ee70629031d05a4ed7d4af465017beaeb851c7170f465f5bbda3ac5ea0f817703d3fcb3ca260cf06e3b2d4a1407205acc2779213f31861198f7d6b1246a072f17d6d0c7a41a65d55520d43560eee1dea4e104c86eadd8e72e2f742c886b0a0978f46ab770df6c5c5e357c291b0d2c1cd7c26cf8bb45ab944679e34f8ccff02a0a6ec6713c9ca0171c812e6d3ef14fc97dfeff29c153282822f9b0ac71abfe74380"));
		proof.push_back(beam::from_hex("f90211a0dbdd876c2186685b95d07cf3bdb44d69706b388cf8ca6a5f4682317c3670ef08a0d01ec1cbfadbe1bbf99160590abd75032dd29f33d76dcde27bdf0a34f6d8914ba00a6451d43317c31ea4d69dccc9175ad7eeffb4a27b7c9b72a5a8b73b6580fcd3a005ee975db94e4dbda76df33f4d50ac80511233b3e9326d4a4ace49b8d9c1df55a00c6eaa0b03bdb1db1286a43aea5f1eeb2ea154d7354d450da37362bd5a291ceaa0c02b3e9310d3e4630a0443af8d937331e4be5eec9dd40735786a74c07cc871a5a0da0f060f83c0a69e47e56d6e5884b79ac72e9ed191a54cd2e0b314f498619082a059a04c3558231a20637a57c82032395ab9a3fbac32c184e1570bc33125b7dff5a012f675b236e15050ef7b2bf235865fb21724ba1b1411b55d7b3ee0450beb439ca08a09660dd277b627d21cd54bb8cdc697841ac998b6665c5bf9b26e3033435c91a0d927193d2da1e27e49706fb381a2ec080ea394d3eee045b9fdbd40f2889b40f2a09979204db7a328a9dd00beb6b958a827a344a3a8d29f863cb39395099b4b84eea080bb8a6a42f1563526ca45c649c5f95bf483dd22f9020eea2e44a6932626f19ea077f6a5b1beab01720e3ba734f766348acbaa16d01fa123266391965a96b4656ba055a8cc551b3d67d0eacc2da84f7826a4235f084732bd069ebe65df230bd85e65a0417d94944dd69f2288702e398e3cb51f7ede202a128b1152e23a8040b7d8b97080"));
		proof.push_back(beam::from_hex("f90211a0e30fc8d408cedc94aebf46c75bf4be02c532c99efd6d1e33f1a85fcffc615a57a04b8ec869cc5a906ccf5084d6834b97885a49fb96bdba4fc3c46c345b74dc53e2a02ac394ea10a9ba607c6e407678d1b5c159dc4cedde82e6b30b7b5372b05f723ea0253d251cd481101ed14f3a64021aa9b824e24dde6be7413fd916be5b67c51f84a0055ab49d90591e63c5ef73fa6b96df471ab7fe7463d1a27d5fdffea7270c979fa0a595ba43e3b8fe530476225d2792d40c0ce6af97065ac6051919bb1bb0ddf283a09a4ad4fcb177fa5849f1a69d21003262d1cae41d01964d394b0a7d27181c165ba0b503ee795ae25ed74d32f18f978d5986d37f41dc420c7d1dc533dc46b709005ca06f785cfbca37c8a476078f5e85ffe2a4160866e1df062a41af9333b09e743a70a021308e6d3171ed853115e23499d2ca02dbb26e57efbd1a7067a8f370f5da250aa0a056f3262f0512c7681a43e8c09e3dd3a103e9d041189558bf7f8a5cba687b27a097a64b6e7f3badecc5c0f66205afec16f7a86896c58450a0274d5e0c2993bc9ca00d13f9ad2cdc27a8d0cd5b5739a7578e9525d2cf939c1c227e9ad3ed69f876e6a098ec78a01fc6aae494a4bff7e3e0be24ded144c0fe6fb2545237fa08819310b8a06e5b942add7296d2158cbf4d2030f26b94139c93f82021f61814368eb32c92b6a0fd739c72b5372ca7fa3f81d41ce82a891c850a55d3746a8859225704cf6bc08a80"));
		proof.push_back(beam::from_hex("f8918080808080808080a0cd1443e0ef5eb5e12861ba3447a6084feb9140ce4c46f8385c1c0d9ebcef1c7580a060d604788c3325154194e53a7bd702e32df3fc1277d1d4c17ea6d009ed36a84d80a0415474d1e98945423d3c3c7603c7bef9d367f9bef3f99265d0b0c1c5f24da732a0a18cf4a77a1fad0aef92205ab3c909461cb3ebd8a4a82641591bf17e260c7c4d808080"));
		proof.push_back(beam::from_hex("f8518080808080a07a11f43bae392095e072aa90db0667436923071d22b76d40a8a082bf496737c280a0e2c4cae024ed30d373d8ccbf4fd6377f9213089e1e4d7a4f658fa533f3075455808080808080808080"));
		proof.push_back(beam::from_hex("e79d327612073b26eecdfd717e6a320cf44b4afac2b0732d9fcbe2b7fa0cf6888758851a8df731ef"));

		beam::ByteBuffer rootHash = beam::from_hex("1394917cc2e15bbbd6e21b511898e8bb361dab9e85602a3d5e1ad7470cda41f7");
		auto result = VerifyEthProof(GetTrieKeyInNibbles(key), proof, rootHash);
		std::cout << "isValid: " << result.second << ", data: " << beam::to_hex(result.first.data(), result.first.size()) << std::endl;
		auto expected = beam::from_hex("8758851a8df731ef");
		verify_test(expected == result.first);
	}

	{
		std::cout << "account proof " << std::endl;
		// account proof, block 12343176, eth_getProof: ["0xdAC17F958D2ee523a2206206994597C13D831ec7",["","1"]
		// "address": "0xdac17f958d2ee523a2206206994597c13d831ec7"
		// "balance": "0x1"
		// "codeHash": "0xb44fb4e949d0f78f87f79ee46428f23a2a5713ce6fc6e0beb3dda78c2ac1ea55"
		// "nonce": "0x1"
		// "storageHash": "0x1394917cc2e15bbbd6e21b511898e8bb361dab9e85602a3d5e1ad7470cda41f7"

		// block "stateRoot": "0x0b5edae32250cf7177f3aba0fd5b77aab7675cef51fec3a238b4f52f2ed2f1bf"

		beam::ByteBuffer key = beam::from_hex("dac17f958d2ee523a2206206994597c13d831ec7");
		std::vector<beam::ByteBuffer> proof;
		proof.push_back(beam::from_hex("f90211a088ce75369e79ae0742a76db0334fe625610703fcbaa56c0cd53da0cff9532bf1a0a81dea17b8fbf0db793adeaceca4bd19206520914316b7beb9a1d6174f7a0c19a0ca1d27fa9858c34dfb41937739792104e5c62d8cae1b7a782ac2c450e1551e27a0aa713b118a2e454605a2ca9efb5b86361d38c3e1e3b3f2245a38e2ceb9989d54a0383a7d60ac668094625e342abe9e5caba41c39201bef37c8ae06d88aa60cd99da02196f15b443a0608a212c83a98236be84caea2aea93fd1b012eb069671d1a3faa0caf45819accb5f43afc4f8d89b89be46f926d75f572248a5d04f8366abad33e1a090d0ac1885259ddc8223de0cad30761a6097362a09271bfc985c1bb3d26b532fa07ba28f2eec260a77063869438b2ce75360fc7e5c62c661b15af18d6b0979ea0ba06e608a8f8e669533de183aae14e0b071c70b2f6757f5760875d9be484db7e4bba01b2a5692be2a2f588b6c90f916f67a677faf1ea80d288bdda62c8ecb9f6f22f8a0ffd2cfa18c3ea9bcc7a8f4655224b39a4187907e778629d09c023b52877e5e77a0cc64a70aec56ebd4a0da2b31b80751c4f0690336725a4fccabbb59977e4d4d23a0ff99a0f2a42cc9f5002e431ec1f6f9f6bd94f03e0323f55636eb1724047fd20da0a870351bd7f385e1e397887ba255f505146a57b86b09a6dca6c1cf886789f669a066c58358e40f3c19492e5badda1f8ec4e9c05d9647b48358919d1b7a9188515a80"));
		proof.push_back(beam::from_hex("f90211a01fe18caf16c26982f463355c52745ae93c45958393b73f04956ed7113ac06445a0714ae2be0e81ec033bbba2f8f518f4de0c48c171185231c0cf4c59724fa55d70a00f69603b0940d4b5e4b81d8201109facc7bebe16ecb421ac0129c8d61fde0220a07ef8b68efa236f696c881704a20075efcff31a2025a8b7b7f881ce6e096a1396a008bb6a26954478066bbddc1a22ab41f081da8d3de194eb499ee8598a829fbbfda08a74cd7dd8c92fb50def3708830b2aaba253c61607a81cb24a25a1918b416385a06a030ceeb62c104c469d0149d0389604313da29716d1d5b614f65d4ab9c0d080a00ed101210a39150b40e4320f0ee68f8400843d82d45c40d6c84f3fc9046532fba0392c1806055a499e9f2b85914e4e1a9e41d89b282b828baf03e9582a12fb4e4ba0342922c5404bf6b9d533da2e00e22c907e084c9eb6206713e9f711f9661beb1ca071fe40069c4d99e9fc74ec33da77d48be78fca9cf020d5f483f94b1125de5720a09e0405e49649a4ff22956b67b0305cd65d1977a66b3dad59a8b91c9f37f3351fa0dfaece107c01bd5f9824078409fbe2db68c8b5151e4518f82921236ab7ba43c1a0f91c9955ba311fa46b5e010e45cf3345fbfb99ac9fc7005027042f7faa701891a0518e05e535e96022fad7f7135b3a170be52d048bf5828a42caf0ac2bd1e0b34aa0fce7f2880019af805738cc2ee63160992161eba558707768f282161c2186e70580"));
		proof.push_back(beam::from_hex("f90211a06015a51733ab0614892487f0c4def00a1a0ad6df22cd3c2af6b1a38c9add6581a00cba6dfc381385abdc9877a0824c53589338e3ad8cc7002a1d34c867d1d22603a01a97f4f01eb890b00e0066c17791a3deae99a31bdb635f5e79338e75620783a2a078e98c304bafe75caaf2e81eba553437c7f90c2b2b7d61e449fd2fa0eeb43cdda08d869b3ec155b51504c10d43eef2a7e60980ee3c17c6774c37ad6fcc9b186923a03fa4ffcf0ff209cfb69226f111be9a4d925de57be8618bc6e25af2956be7bcd2a073115efacb5c60a5d8b3be4aaeffa9a202119bcce0c907bccbee3a7d511ddf29a076fd7c745fab93d793ead073753f4b49a3fbdc7030407ade4cf7d66b9a60a0b9a01c02ade489f4205b33924a16978ea7b2f65fc45884e283dd9dcce268a3523e6da0e3eb7d9429459e98bb405ebf66ca0f8cc9ad9e4d38ee91c23860b85419cd9d5aa01d2cd5aaab180d330edf6e2a434fc1cd74c182723229678ac57ef16ec9c5bdcfa0da09c9dec7ef0fbcd6ed8500fc7ddf250783871803d5e9a0f17e436f1f3c4ebaa02213ff0fc544a913cfcdbf5b5d5dae92a6d6beed34dc8f91d9bb9c8f5066e221a031446451ae6ea69af0474a5cf21aadc27822736cf76ae02885c4c2e2135a267ea0c18d6df662d43e0e1955644e0eaf53d95d7d824cd21d65c15d83f6196b9ac20da0e73b0a7cff02fb0f2cf93918675e0566f03532c990bb12575c1eeeee08f36d7f80"));
		proof.push_back(beam::from_hex("f90211a0c6dfd621fa04f87ef16b8e15d56eea82d8b2a58beb6d52f6c7feaf54182587d6a0b7b01212574b3438bb4088f6325e7548b3dfe6dd20cd28b7b2aa39d2f60af62fa0bb08e4268594cad73aebac02249a6613a3d292907436afec4671f6503311d62da092da28b7cc4bf871a1005b735f233a00c94aba69b85ccc1fc33fb19eea3587cda00783e4a1e33e059a6a9323ce9c14e54f4b99994c8307c7379aee31feea511202a053bb7b76ce8d7b105f161719f19775ae40d204db9798b8e2dd902ce0b53d53aca0d879a4b374072b1ac41d540f3075fc466888354c007d339a9ee87bc5d143f787a03cfc10d6ae750b4e75e186cfb5b7b7f68a9f7d95861f876ad0380c8ad60e1b09a09a74aa641184c10c1d50f58cfc6dabb429b9016c489cf77a0ba5fdb9f6f9f5bba096a2edad15cd89c19e3b14bb1dcff39766000e26e7295378f4ca1559d0ae5393a0fe12a4c5f5eb6fbdf32265c3de852a71c404b38c4e524af541ac7d8da9ae9559a03636b69e82dbfd9e490a194470505148a1820b4cc38bf2703e970aac227a3b21a075822ac5d37176d75b0c962d9bef38ae4f9a38fbf7e88b151b3bb6586e779f5aa0b874030586bb2afeda5f8a190c6e618a3ad7576dc5633321dd645af9a355b613a0da6ab4c058711b78aaf707ce33c8565187f5a288c7cebc6b0e6bddcfd34dfab8a0fe88683e4c9f0398c027b30d700f3a50775e1eba44f175d87e2241518214889380"));
		proof.push_back(beam::from_hex("f90211a0a4873537bd9afa0deed42da8b3aa00ef8da55fb213ede33f7d02065dba5cb7b3a01d152cb14da2ab2c402a85a854d70eb9383c79cc93d8d8d677026a02bfd0a279a068d2153407ec1d3a0136f3b277f9ec8c0362e89fc17261859ef91973dae5db46a0fc831d9e2c4432d9301811fe7fd6c6f5efec0cc285dbed6678caed68cd943d0fa0cab3890b3dc986c6838a1f0ac0087596bbd95535d1de54cf65de0aecf151ae77a000733891fce719629b1849de130681e5ebd6e213f0cd86e66c1642d04f1da670a0de8bc3c6c7fdb9c94843450c8ba69c652d24d6bb3ba8343cc5f3c2f856635f45a066fdbc394540455c4f11584c08787d1524d42f11572b119ba672e123954601aea02fb3697ecff1b72cc691e0fc1238a060d4eb6fd399bae56a83fd0248dc3f5031a05b4b47a212d4a48f3bc2439ec3fb2b2ac905889ecaaafecae87950d747755e59a0205c79430fd378d6f17f21403f18ea630805f6a20d94541103d5bbf1a2ea9a6da0d93308e4e9c8363cf6bf35580eb3165dca9dcdcb12ea38e0dbba8808ef6bc650a03349bf65d9eee483a274d35542a298055fd322a43224a7b4d1fe771a86744cbba0ce8337540e50ecc4895226a8ff0746385ce97111ba8ef819109122340cd8cc78a0ec06eea7fd8bfd26e9ff5e18e3d68de90d4518792dcf236bd1a51d953200f8c9a012733a5b582b1ace379ff86df467ebf9aa7ef735e4b08a3df5549178eae3aceb80"));
		proof.push_back(beam::from_hex("f90211a07a897343f2a35dd209df6a11195b84b58354cdf49514bfb671743b7d23d2a7a7a0f060906007684bba15aca67b8b8de5d52359d79ee8e0d5427c9816cc1f89b403a094b12d7fac3b4de0552f0659d66ce35481e873892366c2a13a52f889bc75899ca03b67732e981ed319791eed3403358d8a46f6afce4ea5347409ee8085bdc771c8a03ef6974fb165ccedc4d1272e8cef18fc7c7dd158051fa4fcbe50cf52309289e9a0a8140506311bf44f0d792bfe2ce6037f1dbdaa1d63b26b4084895b8d3e969304a030559f34bea90c745e0ca7627fe4ceefa63e2c07de88050b1259d79a2239ee7ea0b7feabab413608ad631d118c347b2bde0cd2a3165ffa2ed7198cda508a560a38a0677045650b4182e4184227e1229c7262edbe3fae9ad3dbc684240d5d713d98d9a0cc886c3090b518b63139fead8a2bb7cc6f914ec7382fcf07200b6e91b6390558a00c237252c62534e8a754bdb8f1c26112c1cc138861e56f02e951f63d89592d96a084d94ccb5e7ab094c94b91e2b9087c9c320e12841d73e698f611643f77bf257fa09ca36b56637983cb709bd476f08e9e924d7fb8c18bca370445bc8920323b21aea03dfa5652ca85286eee16bd84db578dd9b505846efc6b96181744c43dfa861fa5a09112c918f5bf090466c34febf5a8bda067ad7078e21760c9f540df9af737a9f7a04b767f6b38c7a6439a4b7b6066207638b9656c416f9ad09a3ee52c600e681c4780"));
		proof.push_back(beam::from_hex("f90111a01e7af2031302c134bbb3a6778c8cf033caa30300730c2f72e404cc7364cfec7780a09d1fde7ccd25f8c5a45399cd0bf2bc90006fd468f1a4cffa95a5a4eae7872b84a00b3a26a05b5494fb3ff6f0b3897688a5581066b20b07ebab9252d169d928717f80a01e2a1ed3d1572b872bbf09ee44d2ed737da31f01de3c0f4b4e1f0467400664618080a085687925842a6e20bf1d2412ce7249699b4111225d87c7c8959072b0cbab78ffa07aad8ea34d91339abdfdc55b0d5e0aa4fc3c506f56fd2518b6f8c7c5d2ed254880a0e9864fdfaf3693b2602f56cd938ccd494b8634b1f91800ef02203a3609ca4c21a05904860db3383d925f9a36578fcea05e0ef02ee49d21ce50d0df7056df00b0f880808080"));
		proof.push_back(beam::from_hex("f8669d3802a763f7db875346d03fbf86f137de55814b191c069e721f47474733b846f8440101a01394917cc2e15bbbd6e21b511898e8bb361dab9e85602a3d5e1ad7470cda41f7a0b44fb4e949d0f78f87f79ee46428f23a2a5713ce6fc6e0beb3dda78c2ac1ea55"));

		beam::ByteBuffer rootHash = beam::from_hex("794660825d2bd07c747766045fc64a32710043ed9808f3fba249ded6cb607993");
		auto result = VerifyEthProof(GetTrieKeyInNibbles(key), proof, rootHash);
		std::cout << "isValid: " << result.second << ", data: " << beam::to_hex(result.first.data(), result.first.size()) << std::endl;

		auto expected = beam::from_hex("f8440101a01394917cc2e15bbbd6e21b511898e8bb361dab9e85602a3d5e1ad7470cda41f7a0b44fb4e949d0f78f87f79ee46428f23a2a5713ce6fc6e0beb3dda78c2ac1ea55");
		verify_test(expected == result.first);
	}
}

int main()
{
	g_psecp256k1 = secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);

	ECC::PseudoRandomGenerator prg;
	ECC::PseudoRandomGenerator::Scope scopePrg(&prg);

	beam::Rules::get().CA.Enabled = true;
	beam::Rules::get().pForks[1].m_Height = g_hFork;
	beam::Rules::get().pForks[2].m_Height = g_hFork;
	beam::Rules::get().pForks[3].m_Height = g_hFork;
	ECC::TestAll();
	ECC::RunBenchmark();

	TestEthProof();

	secp256k1_context_destroy(g_psecp256k1);

    return g_TestsFailed ? -1 : 0;
}
