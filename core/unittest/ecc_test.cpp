#include <iostream>
#include "../ecc_native.h"
#include "../common.h"


#include "../beam/secp256k1-zkp/include/secp256k1_rangeproof.h" // For benchmark comparison with secp256k1
void secp256k1_ecmult_gen(const secp256k1_context* pCtx, secp256k1_gej *r, const secp256k1_scalar *a);

int g_TestsFailed = 0;

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

void TestHash()
{
	Hash::Processor hp;
	Hash::Value hv;
	hp >> hv;

	for (int i = 0; i < 10; i++)
	{
		Hash::Value hv2 = hv;
		hp >> hv;

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
}

void TestPoints()
{
	// generate, import, export
	Point::Native p0, p1;
	Point p_, p2_;

	p_.m_X = Zero; // should be zero-point
	p_.m_Y = true;
	verify_test(!p0.Import(p_));
	verify_test(p0 == Zero);

	p_.m_Y = false;
	verify_test(!p0.Import(p_));

	p2_ = p0;
	verify_test(!p_.cmp(p2_));

	for (int i = 0; i < 1000; i++)
	{
		SetRandom(p_.m_X);
		p_.m_Y = 0 != (1 & i);

		while (!p0.Import(p_))
		{
			verify_test(p0 == Zero);
			p_.m_X.Inc();
		}
		verify_test(!(p0 == Zero));

		p1 = -p0;
		verify_test(!(p1 == Zero));

		p1 += p0;
		verify_test(p1 == Zero);

		p2_ = p0;
		verify_test(!p_.cmp(p2_));
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
		GenerateRandom(&val, sizeof(val));

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
		SetRandom(mysig2.m_e.m_Value);
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
}

void TestRangeProof()
{
	Scalar::Native sk;
	SetRandom(sk);

	RangeProof::Public rp;
	rp.m_Value = 345000;
	rp.Create(sk);

	Point::Native comm = Commitment(sk, rp.m_Value);

	verify_test(rp.IsValid(Point(comm)));

	// tamper value
	rp.m_Value++;
	verify_test(!rp.IsValid(Point(comm)));
	rp.m_Value--;

	// try with invalid key
	SetRandom(sk);

	comm = Commitment(sk, rp.m_Value);

	verify_test(!rp.IsValid(Point(comm)));
}

struct TransactionMaker
{
	beam::Transaction m_Trans;

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

		void EncodeAmount(Point& out, Scalar::Native& k, Amount val)
		{
			SetRandom(k);
			out = Point::Native(Commitment(k, val));
		}

		void FinalizeExcess(Point::Native& kG, Scalar::Native& kOffset)
		{
			kOffset += m_k;

			SetRandom(m_k);
			kOffset += m_k;

			m_k = -m_k;
			kG += Context::get().G * m_k;
		}


		void AddInput(beam::Transaction& t, Amount val)
		{
			std::unique_ptr<beam::Input> pInp(new beam::Input);
			pInp->m_Height = 0;
			pInp->m_Coinbase = false;

			Scalar::Native k;
			EncodeAmount(pInp->m_Commitment, k, val);

			t.m_vInputs.push_back(std::move(pInp));

			m_k += k;
		}

		void AddOutput(beam::Transaction& t, Amount val)
		{
			std::unique_ptr<beam::Output> pOut(new beam::Output);
			pOut->m_Coinbase = false;

			Scalar::Native k;
			EncodeAmount(pOut->m_Commitment, k, val);

			pOut->m_pPublic.reset(new RangeProof::Public);
			pOut->m_pPublic->m_Value = val;
			pOut->m_pPublic->Create(k);

			t.m_vOutputs.push_back(std::move(pOut));

			k = -k;
			m_k += k;
		}

	};

	Peer m_pPeers[2]; // actually can be more

	void CoSignKernel(beam::TxKernel& krn)
	{
		Hash::Value msg;
		krn.get_Hash(msg);

		// 1st pass. Public excesses and Nonces are summed.
		Scalar::Native offset(m_Trans.m_Offset);

		Point::Native kG(Zero), xG(Zero);

		for (int i = 0; i < _countof(m_pPeers); i++)
		{
			Peer& p = m_pPeers[i];
			p.FinalizeExcess(kG, offset);

			Signature::MultiSig msig;
			msig.GenerateNonce(msg, p.m_k);

			xG += Context::get().G * msig.m_Nonce;
		}

		m_Trans.m_Offset = offset;
		krn.m_Excess = kG;

		// 2nd pass. Signing. Total excess is the signature public key
		offset = Zero;

		for (int i = 0; i < _countof(m_pPeers); i++)
		{
			Peer& p = m_pPeers[i];

			Signature::MultiSig msig;
			msig.GenerateNonce(msg, p.m_k);
			msig.m_NoncePub = xG;

			Scalar::Native k;
			krn.m_Signature.CoSign(k, msg, p.m_k, msig);

			offset += k;

			p.m_k = Zero; // signed, prepare for next tx
		}

		krn.m_Signature.m_k = offset;

	}

	void CreateTxKernel(std::vector<beam::TxKernel::Ptr>& lstTrg, Amount fee, std::vector<beam::TxKernel::Ptr>& lstNested)
	{
		std::unique_ptr<beam::TxKernel> pKrn(new beam::TxKernel);
		pKrn->m_Fee = fee;
		pKrn->m_HeightMin = 0;
		pKrn->m_HeightMax = -1;

		pKrn->m_vNested.swap(lstNested);

		// contract
		Scalar::Native skContract;
		SetRandom(skContract);

		pKrn->m_pContract.reset(new beam::TxKernel::Contract);
		SetRandom(pKrn->m_pContract->m_Msg);

		pKrn->m_pContract->m_PublicKey = Point::Native(Context::get().G * skContract);

		CoSignKernel(*pKrn);

		// sign contract
		Hash::Value hv;
		pKrn->get_Hash(hv);
		pKrn->get_HashForContract(hv, hv);

		pKrn->m_pContract->m_Signature.Sign(hv, skContract);

		lstTrg.push_back(std::move(pKrn));
	}

	void AddInput(int i, Amount val)
	{
		m_pPeers[i].AddInput(m_Trans, val);
	}

	void AddOutput(int i, Amount val)
	{
		m_pPeers[i].AddOutput(m_Trans, val);
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

	tm.CreateTxKernel(lstNested, fee1, lstDummy);

	tm.AddOutput(0, 738);
	tm.AddInput(1, 740);
	tm.CreateTxKernel(tm.m_Trans.m_vKernels, fee2, lstNested);

	Amount fee;
	verify_test(tm.m_Trans.IsValid(fee, 0));
	verify_test(fee == fee1 + fee2);
}

void TestAll()
{
	TestHash();
	TestScalars();
	TestPoints();
	TestSigning();
	TestCommitments();
	TestRangeProof();
	TestTransaction();
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
	Point::Native p0, p1;

	Point p_;
	p_.m_Y = false;

	SetRandom(p_.m_X);
	while (!p0.Import(p_))
		p_.m_X.Inc();

	SetRandom(p_.m_X);
	while (!p1.Import(p_))
		p_.m_X.Inc();

	{
		BenchmarkMeter bm("point.Negate");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				p0 = -p0;

		} while (bm.ShouldContinue());
	}

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
		k1 = Zero;

		BenchmarkMeter bm("point.Multiply.Min");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				p0 += p1 * k1;

		} while (bm.ShouldContinue());
	}

	{
		BenchmarkMeter bm("point.Multiply.Avg");
		do
		{
			SetRandom(k1);
			for (uint32_t i = 0; i < bm.N; i++)
				p0 += p1 * k1;

		} while (bm.ShouldContinue());
	}

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
				p0 += Context::get().H * uint64_t(-1);

		} while (bm.ShouldContinue());
	}

	{
		k1 = uint64_t(-1);

		Point p_;
		p_.m_X = Zero;
		p_.m_Y = false;

		while (!p0.Import(p_))
			p_.m_X.Inc();

		BenchmarkMeter bm("G.Multiply");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				p0 += Context::get().G * k1;

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
				Hash::Processor hp;
				hp.Write(pBuf, sizeof(pBuf));
				hp >> hv;
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

	secp256k1_context* pCtx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);

	{
		secp256k1_pedersen_commitment comm;

		BenchmarkMeter bm("secp256k1.Commit");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
			secp256k1_pedersen_commit(pCtx, &comm, k_.m_Value.m_pData, 78945, secp256k1_generator_h);

		} while (bm.ShouldContinue());
	}

	{
		BenchmarkMeter bm("secp256k1.G.Multiply");
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
