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

			Point::Native pt;
			pt = Commitment(k, val);
			pt.Export(out);
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
		Scalar::Native offset;
		offset.Import(m_Trans.m_Offset);

		Point::Native kG, xG;
		kG = Zero;
		xG = Zero;

		for (int i = 0; i < _countof(m_pPeers); i++)
		{
			Peer& p = m_pPeers[i];
			p.FinalizeExcess(kG, offset);

			Signature::MultiSig msig;
			msig.GenerateNonce(msg, p.m_k);

			xG += Context::get().G * msig.m_Nonce.V;
		}

		offset.Export(m_Trans.m_Offset);
		kG.Export(krn.m_Excess);

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

		offset.Export(krn.m_Signature.m_k);

	}

	void CreateTxKernel(beam::TxKernel::List& lstTrg, Amount fee, beam::TxKernel::List& lstNested)
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

		Point::Native pkContract;
		pkContract = Context::get().G * skContract;
		pkContract.Export(pKrn->m_pContract->m_PublicKey);

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

	beam::TxKernel::List lstNested, lstDummy;

	Amount fee1 = 100, fee2 = 2;

	tm.CreateTxKernel(lstNested, fee1, lstDummy);

	tm.AddOutput(0, 738);
	tm.AddInput(1, 740);
	tm.CreateTxKernel(tm.m_Trans.m_vKernels, fee2, lstNested);

	Amount fee;
	verify(tm.m_Trans.IsValid(fee, 0));
	verify(fee == fee1 + fee2);
}

void TestAll()
{
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
		verify(!clock_gettime(CLOCK_MONOTONIC, &tp));
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
	Scalar::Native k1, k2;
	SetRandom(k1);
	SetRandom(k2);

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

	Scalar k_;
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
	Point::Native p0, p1;

	Point p_;
	p_.m_bQuadraticResidue = false;

	SetRandom(p_.m_X);
	while (!p0.Import(p_))
		p_.m_X.Inc();

	SetRandom(p_.m_X);
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
				p0 = p0 * Two;

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
				p0 += Context::get().H * uint64_t(-1);

		} while (bm.ShouldContinue());
	}

	{
		k1 = uint64_t(-1);

		Point p_;
		p_.m_X = Zero;
		p_.m_bQuadraticResidue = false;

		while (!p0.Import(p_))
			p_.m_X.Inc();

		BenchmarkMeter bm("G.Mul");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				p0 += Context::get().G * k1;

		} while (bm.ShouldContinue());
	}

	{
		BenchmarkMeter bm("Commt");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				p0 = Commitment(k1, 275);

		} while (bm.ShouldContinue());
	}

	Hash::Value hv;
	{
		Hash::Processor hp;
		hp.Write("abcd");
		hp.Finalize(hv);
	}

	Signature sig;
	{
		BenchmarkMeter bm("S.Sig");
		do
		{
			for (uint32_t i = 0; i < bm.N; i++)
				sig.Sign(hv, k1);

		} while (bm.ShouldContinue());
	}

	p1 = Context::get().G * k1;
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
