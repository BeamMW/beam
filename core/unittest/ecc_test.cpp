#include <iostream>
#include "../ecc_native.h"


#include "../beam/secp256k1-zkp/include/secp256k1_rangeproof.h" // For benchmark comparison with secp256k1
void secp256k1_ecmult_gen(const secp256k1_context* pCtx, secp256k1_gej *r, const secp256k1_scalar *a);

int g_TestsFailed = 0;

void TestFailed(const char* szExpr, uint32_t nLine)
{
	printf("Test failed! Line=%u, Expression: %s\n", nLine, szExpr);
	g_TestsFailed++;
}

#define verify(x) \
	do { \
		if (!(x)) \
			TestFailed(#x, __LINE__); \
	} while (false);

namespace ECC {

void GenerateRandom(void* p, uint32_t n)
{
	for (uint32_t i = 0; i < n; i++)
		((uint8_t*) p)[i] = (uint8_t) rand();
}

void SetRandom(uintBig& x)
{
	GenerateRandom(x.m_pData, sizeof(x.m_pData));
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

Context g_Ctx;
const Context& Context::get() { return g_Ctx; }

void TestScalars()
{
	Scalar::Native s0, s1, s2;
	s0 = 17U;

	// neg
	s1 = -s0;
	verify(!(s1 == Zero));
	s1 += s0;
	verify(s1 == Zero);

	// inv, mul
	s1.SetInv(s0);

	s2 = -s1;
	s2 += s0;
	verify(!(s2 == Zero));

	s1 *= s0;
	s2 = 1U;
	s2 = -s2;
	s2 += s1;
	verify(s2 == Zero);

	// import,export

	for (int i = 0; i < 1000; i++)
	{
		SetRandom(s0);

		Scalar s_;
		s0.Export(s_);
		s1.Import(s_);

		s1 = -s1;
		s1 += s0;
		verify(s1 == Zero);
	}
}

void TestPoints()
{
	// generate, import, export
	Point::Native p0, p1;

	for (int i = 0; i < 1000; i++)
	{
		Point p_;
		SetRandom(p_.m_X);
		p_.m_bQuadraticResidue = 0 != (1 & i);

		while (!p0.Import(p_))
		{
			verify(p0 == Zero);
			p_.m_X.Inc();
		}
		verify(!(p0 == Zero));

		p1 = -p0;
		verify(!(p1 == Zero));

		p1 += p0;
		verify(p1 == Zero);
	}

	// multiplication
	Scalar::Native s0, s1;
	Scalar s_;

	s0 = 1U;

	Point::Native g;
	g = Context::get().G * s0;
	verify(!(g == Zero));

	s0 = Zero;
	p0 = Context::get().G * s0;
	verify(p0 == Zero);

	s0.Export(s_);
	p0 += g * s_;
	verify(p0 == Zero);

	for (int i = 0; i < 300; i++)
	{
		SetRandom(s0);

		p0 = Context::get().G * s0; // via generator

		s1 = -s0;
		p1 = p0;
		p1 += Context::get().G * s1; // inverse, also testing +=
		verify(p1 == Zero);

		s1.Export(s_);
		p1 = p0;
		p1 += g * s_; // simple multiplication

		verify(p1 == Zero);
	}

	// H-gen
	Point::Native h;
	h = Context::get().H * 1U;
	verify(!(h == Zero));

	p0 = Context::get().H * 0U;
	verify(p0 == Zero);

	for (int i = 0; i < 300; i++)
	{
		Amount val;
		GenerateRandom(&val, sizeof(val));

		p0 = Context::get().H * val; // via generator

		s0 = val;
		s0.Export(s_);

		p1 = Zero;
		p1 += h * s_;
		p1 = -p1;
		p1 += p0;

		verify(p1 == Zero);
	}

	// doubling, all bits test
	s0 = 1U;
	s1 = 2U;
	p0 = g;

	for (int nBit = 1; nBit < 256; nBit++)
	{
		s0 *= s1;
		p1 = Context::get().G * s0;
		verify(!(p1 == Zero));

		p0 = p0 * Two;
		p0 = -p0;
		p0 += p1;
		verify(p0 == Zero);

		p0 = p1;
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

		verify(mysig.IsValid(msg, pk));

		// tamper msg
		uintBig msg2 = msg;
		msg2.Inc();

		verify(!mysig.IsValid(msg2, pk));

		// try to sign with different key
		Scalar::Native sk2;
		SetRandom(sk2);

		Signature mysig2;
		mysig2.Sign(msg, sk2);
		verify(!mysig2.IsValid(msg, pk));

		// tamper signature
		mysig2 = mysig;
		SetRandom(mysig2.m_e.m_Value);
		verify(!mysig2.IsValid(msg, pk));

		mysig2 = mysig;
		SetRandom(mysig2.m_k.m_Value);
		verify(!mysig2.IsValid(msg, pk));
	}
}

void TestCommitments()
{
	Scalar::Native kExcess;
	kExcess = Zero;

	Amount vSum = 0;

	Point::Native commInp;
	commInp = Zero;

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
	Point::Native commOutp;
	commOutp = Zero;
	{
		Scalar::Native sk;
		SetRandom(sk);

		commOutp += Commitment(sk, vSum);

		sk = -sk;
		kExcess += sk;
	}

	Point::Native sigma;
	sigma = Context::get().G * kExcess;
	sigma += commOutp;

	sigma = -sigma;
	sigma += commInp;

	verify(sigma == Zero);
}

void TestRangeProof()
{
	Scalar::Native sk;
	SetRandom(sk);

	RangeProof::Public rp;
	rp.m_Value = 345000;
	rp.Create(sk);

	Point::Native comm;
	Point comm_;

	comm = Commitment(sk, rp.m_Value);
	comm.Export(comm_);

	verify(rp.IsValid(comm_));

	// tamper value
	rp.m_Value++;
	verify(!rp.IsValid(comm_));
	rp.m_Value--;

	// try with invalid key
	SetRandom(sk);

	comm = Commitment(sk, rp.m_Value);
	comm.Export(comm_);

	verify(!rp.IsValid(comm_));
}

void TestAll()
{
	TestScalars();
	TestPoints();
	TestSigning();
	TestCommitments();
	TestRangeProof();
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
		Test::SysRet(clock_gettime(CLOCK_MONOTONIC, &tp));
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
			printf("%s: %.2f us\n", m_sz, dt_s * 1e6 / double(m_Cycles));
			return false;
		}

		if (dt_s < 0.5)
			N <<= 1;

		return true;
	}
};

void RunBenchmark()
{
	ECC::Scalar::Native k1, k2;
	ECC::SetRandom(k1);
	ECC::SetRandom(k2);

/*	{
		BenchmarkMeter bm("s.Add");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				k1.Add(k2);

		} while (bm.ShouldContinue());
	}

	{
		BenchmarkMeter bm("s.Mul");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				k1.Mul(k2);

		} while (bm.ShouldContinue());
	}
*/

	{
		BenchmarkMeter bm("s.Inv");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				k1.Inv();

		} while (bm.ShouldContinue());
	}

	ECC::Scalar k_;
/*
	{
		BenchmarkMeter bm("s.Exp");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				k1.Export(k_);

		} while (bm.ShouldContinue());
	}

	{
		BenchmarkMeter bm("s.Imp");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				k1.Import(k_);

		} while (bm.ShouldContinue());
	}
*/
	ECC::Point::Native p0, p1;

	ECC::Point p_;
	p_.m_bQuadraticResidue = false;

	ECC::SetRandom(p_.m_X);
	while (!p0.Import(p_))
		p_.m_X.Inc();

	ECC::SetRandom(p_.m_X);
	while (!p1.Import(p_))
		p_.m_X.Inc();

	{
		BenchmarkMeter bm("p.Neg");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				p0 = -p0;

		} while (bm.ShouldContinue());
	}

	{
		BenchmarkMeter bm("p.X_2");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				p0 = p0 * ECC::Two;

		} while (bm.ShouldContinue());
	}

	{
		BenchmarkMeter bm("p.Add");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				p0 += p1;

		} while (bm.ShouldContinue());
	}

	{
		k1 = 1U;
		k1.Inv();
		k1.Export(k_);

		BenchmarkMeter bm("p.Mul");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				p0 += p1 * k_;

		} while (bm.ShouldContinue());
	}

	{
		BenchmarkMeter bm("p.Exp");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				p0.Export(p_);

		} while (bm.ShouldContinue());
	}

	{
		BenchmarkMeter bm("p.Imp");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				p0.Import(p_);

		} while (bm.ShouldContinue());
	}

	{
		BenchmarkMeter bm("H.Mul");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				p0 += ECC::Context::get().H * uint64_t(-1);

		} while (bm.ShouldContinue());
	}

	{
		k1 = uint64_t(-1);

		ECC::Point p_;
		p_.m_X = ECC::Zero;
		p_.m_bQuadraticResidue = false;

		while (!p0.Import(p_))
			p_.m_X.Inc();

		BenchmarkMeter bm("G.Mul");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				p0 += ECC::Context::get().G * k1;

		} while (bm.ShouldContinue());
	}

	{
		BenchmarkMeter bm("Commt");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				p0 = ECC::Commitment(k1, 275);

		} while (bm.ShouldContinue());
	}

	ECC::Hash::Value hv;
	{
		ECC::Hash::Processor hp;
		hp.Write("abcd");
		hp.Finalize(hv);
	}

	ECC::Signature sig;
	{
		BenchmarkMeter bm("S.Sig");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				sig.Sign(hv, k1);

		} while (bm.ShouldContinue());
	}

	p1 = ECC::Context::get().G * k1;
	{
		BenchmarkMeter bm("S.Ver");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				sig.IsValid(hv, p1);

		} while (bm.ShouldContinue());
	}

	secp256k1_context* pCtx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);

	{
		secp256k1_pedersen_commitment comm;

		BenchmarkMeter bm("Pdrsn");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
			secp256k1_pedersen_commit(pCtx, &comm, k_.m_Value.m_pData, 78945, secp256k1_generator_h);

		} while (bm.ShouldContinue());
	}

	{
		BenchmarkMeter bm("ecmul");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				secp256k1_ecmult_gen(pCtx, &p0.get_Raw(), &k1.get());

		} while (bm.ShouldContinue());
	}

	secp256k1_context_destroy(pCtx);
}


} // namespace ECC

int main()
{
	ECC::TestAll();
	ECC::RunBenchmark();

    return g_TestsFailed ? -1 : 0;
}
