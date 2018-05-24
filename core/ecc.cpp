#include "ecc_native.h"
#include <assert.h>

#define ENABLE_MODULE_GENERATOR
#define ENABLE_MODULE_RANGEPROOF

#if defined(__clang__) || defined(__GNUC__) || defined(__GNUG__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunused-function"
#else
    #pragma warning (push, 0) // suppress warnings from secp256k1
#endif

#include "../secp256k1-zkp/src/secp256k1.c"

#if defined(__clang__) || defined(__GNUC__) || defined(__GNUG__)
    #pragma GCC diagnostic pop
#else
    #pragma warning (pop)
#endif

// misc
bool memis0(const void* p, size_t n)
{
	for (size_t i = 0; i < n; i++)
		if (((const uint8_t*)p)[i])
			return false;
	return true;
}

namespace ECC {

	//void* NoErase(void*, size_t) { return NULL; }

	// Pointer to the 'eraser' function. The pointer should be non-const (i.e. variable that can be changed at run-time), so that optimizer won't remove this.
	void (*g_pfnEraseFunc)(void*, size_t) = memset0/*NoErase*/;

	void SecureErase(void* p, uint32_t n)
	{
		g_pfnEraseFunc(p, n);
	}

	/////////////////////
	// Scalar
	const uintBig Scalar::s_Order = { // fffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364141
		0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFE,
		0xBA,0xAE,0xDC,0xE6,0xAF,0x48,0xA0,0x3B,0xBF,0xD2,0x5E,0x8C,0xD0,0x36,0x41,0x41
	};

	bool Scalar::IsValid() const
	{
		return m_Value < s_Order;
	}

	Scalar& Scalar::operator = (const Native& v)
	{
		v.Export(*this);
		return *this;
	}

	Scalar& Scalar::operator = (const Zero_&)
	{
		m_Value = Zero;
		return *this;
	}

	Scalar::Native& Scalar::Native::operator = (Zero_)
	{
		secp256k1_scalar_clear(this);
		return *this;
	}

	bool Scalar::Native::operator == (Zero_) const
	{
		return secp256k1_scalar_is_zero(this) != 0;
	}

	bool Scalar::Native::operator == (const Native& v) const
	{
		for (int i = 0; i < _countof(d); i++)
			if (d[i] != v.d[i])
				return false;
		return true;
	}

	Scalar::Native& Scalar::Native::operator = (Minus v)
	{
		secp256k1_scalar_negate(this, &v.x);
		return *this;
	}

	bool Scalar::Native::Import(const Scalar& v)
	{
		int overflow;
		secp256k1_scalar_set_b32(this, v.m_Value.m_pData, &overflow);
		return overflow != 0;
	}

	Scalar::Native& Scalar::Native::operator = (const Scalar& v)
	{
		Import(v);
		return *this;
	}

	void Scalar::Native::Export(Scalar& v) const
	{
		secp256k1_scalar_get_b32(v.m_Value.m_pData, this);
	}

	Scalar::Native& Scalar::Native::operator = (uint32_t v)
	{
		secp256k1_scalar_set_int(this, v);
		return *this;
	}

	Scalar::Native& Scalar::Native::operator = (uint64_t v)
	{
		secp256k1_scalar_set_u64(this, v);
		return *this;
	}

	Scalar::Native& Scalar::Native::operator = (Plus v)
	{
		secp256k1_scalar_add(this, &v.x, &v.y);
		return *this;
	}

	Scalar::Native& Scalar::Native::operator = (Mul v)
	{
		secp256k1_scalar_mul(this, &v.x, &v.y);
		return *this;
	}

	void Scalar::Native::SetSqr(const Native& v)
	{
		secp256k1_scalar_sqr(this, &v);
	}

	void Scalar::Native::Sqr()
	{
		SetSqr(*this);
	}

	void Scalar::Native::SetInv(const Native& v)
	{
		secp256k1_scalar_inverse(this, &v);
	}

	void Scalar::Native::Inv()
	{
		SetInv(*this);
	}

	/////////////////////
	// Hash
	Hash::Processor::Processor()
	{
		Reset();
	}

	void Hash::Processor::Reset()
	{
		secp256k1_sha256_initialize(this);
	}

	void Hash::Processor::Write(const void* p, uint32_t n)
	{
		secp256k1_sha256_write(this, (const uint8_t*) p, n);
	}

	void Hash::Processor::Finalize(Hash::Value& v)
	{
		secp256k1_sha256_finalize(this, v.m_pData);
	}

	void Hash::Processor::Write(const char* sz)
	{
		Write(sz, (uint32_t) strlen(sz));
	}

	void Hash::Processor::Write(bool b)
	{
		uint8_t n = (false != b);
		Write(n);
	}

	void Hash::Processor::Write(uint8_t n)
	{
		Write(&n, sizeof(n));
	}

	void Hash::Processor::Write(const uintBig& v)
	{
		Write(v.m_pData, sizeof(v.m_pData));
	}

	void Hash::Processor::Write(const Scalar& v)
	{
		Write(v.m_Value);
	}

	void Hash::Processor::Write(const Scalar::Native& v)
	{
		NoLeak<Scalar> s;
		s.V = v;
		Write(s.V);
	}

	void Hash::Processor::Write(const Point& v)
	{
		Write(v.m_X);
		Write(v.m_Y);
	}

	void Hash::Processor::Write(const Point::Native& v)
	{
		Write(Point(v));
	}

	/////////////////////
	// Point
	const uintBig Point::s_FieldOrder = { // fffffffffffffffffffffffffffffffffffffffffffffffffffffffefffffc2f
		0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
		0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFE,0xFF,0xFF,0xFC,0x2F
	};

	int Point::cmp(const Point& v) const
	{
		int n = m_X.cmp(v.m_X);
		if (n)
			return n;

		if (m_Y < v.m_Y)
			return -1;
		if (m_Y > v.m_Y)
			return 1;

		return 0;
	}

	Point& Point::operator = (const Native& v)
	{
		v.Export(*this);
		return *this;
	}

	Point& Point::operator = (const Point& v)
	{
		m_X = v.m_X;
		m_Y = v.m_Y;
		return *this;
	}

	Point& Point::operator = (const Commitment& v)
	{
		return operator = (Native(v));
	}

	bool Point::Native::ImportInternal(const Point& v)
	{
		NoLeak<secp256k1_fe> nx;
		if (!secp256k1_fe_set_b32(&nx.V, v.m_X.m_pData))
			return false;

		NoLeak<secp256k1_ge> ge;
		if (!secp256k1_ge_set_xo_var(&ge.V, &nx.V, false != v.m_Y))
			return false;

		secp256k1_gej_set_ge(this, &ge.V);

		return true;
	}

	bool Point::Native::Import(const Point& v)
	{
		if (ImportInternal(v))
			return true;

		*this = Zero;
		return false;
	}

	Point::Native& Point::Native::operator = (const Point& v)
	{
		Import(v);
		return *this;
	}

	bool Point::Native::Export(Point& v) const
	{
		if (*this == Zero)
		{
			v.m_X = Zero;
			v.m_Y = false;
			return false;
		}

		NoLeak<secp256k1_gej> dup;
		dup.V = *this;
		NoLeak<secp256k1_ge> ge;
		secp256k1_ge_set_gej(&ge.V, &dup.V);

		// seems like normalization can be omitted (already done by secp256k1_ge_set_gej), but not guaranteed according to docs.
		// But this has a negligible impact on the performance
		secp256k1_fe_normalize(&ge.V.x);
		secp256k1_fe_normalize(&ge.V.y);

		secp256k1_fe_get_b32(v.m_X.m_pData, &ge.V.x);
		v.m_Y = (secp256k1_fe_is_odd(&ge.V.y) != 0);

		return true;
	}

	Point::Native& Point::Native::operator = (Zero_)
	{
		secp256k1_gej_set_infinity(this);
		return *this;
	}

	bool Point::Native::operator == (Zero_) const
	{
		return secp256k1_gej_is_infinity(this) != 0;
	}

	Point::Native& Point::Native::operator = (Minus v)
	{
		secp256k1_gej_neg(this, &v.x);
		return *this;
	}

	Point::Native& Point::Native::operator = (Plus v)
	{
		secp256k1_gej_add_var(this, &v.x, &v.y, NULL);
		return *this;
	}

	Point::Native& Point::Native::operator = (Double v)
	{
		secp256k1_gej_double_var(this, &v.x, NULL);
		return *this;
	}

	Point::Native& Point::Native::operator = (Mul v)
	{
		const int nBits = 4;
		const int nValuesPerLayer = (1 << nBits) - 1; // skip zero

		Point::Native pPt[nValuesPerLayer];
		pPt[0] = v.x;
		int nPrepared = 1;

		const secp256k1_scalar& k = v.y.get(); // alias

		const int nBitsPerWord = sizeof(Scalar::Native::uint) << 3;
		static_assert(!(nBitsPerWord % nBits), "");
		const int nLayersPerWord = nBitsPerWord / nBits;

		*this = Zero;

		for (int iWord = _countof(k.d); iWord--; )
		{
			const Scalar::Native::uint n = k.d[iWord];

			for (int iLayer = nLayersPerWord; iLayer--; )
			{
				if (!(*this == Zero))
					for (int i = 0; i < nBits; i++)
						*this = *this * Two;

				int nVal = (n >> (iLayer * nBits)) & nValuesPerLayer;
				if (nVal--)
				{
					for (; nPrepared <= nVal; nPrepared++)
						if (nPrepared & (nPrepared + 1))
							pPt[nPrepared] = pPt[nPrepared - 1] + pPt[0];
						else
							pPt[nPrepared] = pPt[nPrepared >> 1] * Two;

					*this += pPt[nVal];
				}
			}
		}

		return *this;
	}

	Point::Native& Point::Native::operator += (Mul v)
	{
		return operator += (Native(v));
	}

	/////////////////////
	// Generator
	namespace Generator
	{
		void FromPt(secp256k1_ge_storage& out, Point::Native& p)
		{
			secp256k1_ge ge; // used only for non-secret
			secp256k1_ge_set_gej(&ge, &p.get_Raw());
			secp256k1_ge_to_storage(&out, &ge);
		}

		void ToPt(Point::Native& p, secp256k1_ge& ge, const secp256k1_ge_storage& ge_s, bool bSet)
		{
			secp256k1_ge_from_storage(&ge, &ge_s);

			if (bSet)
				secp256k1_gej_set_ge(&p.get_Raw(), &ge);
			else
				secp256k1_gej_add_ge(&p.get_Raw(), &p.get_Raw(), &ge);
		}

		bool CreatePointNnz(Point::Native& out, const uintBig& x)
		{
			Point pt;
			pt.m_X = x;
			pt.m_Y = false;

			return out.Import(pt) && !(out == Zero);
		}

		bool CreatePointNnz(Point::Native& out, Hash::Processor& hp)
		{
			Hash::Value hv;
			hp >> hv;
			return CreatePointNnz(out, hv);
		}

		bool CreatePts(secp256k1_ge_storage* pPts, Point::Native& gpos, uint32_t nLevels, Hash::Processor& hp)
		{
			Point::Native nums, npos, pt;

			hp << "nums";
			if (!CreatePointNnz(nums, hp))
				return false;

			nums += gpos;

			npos = nums;

			for (uint32_t iLev = 1; ; iLev++)
			{
				pt = npos;

				for (uint32_t iPt = 1; ; iPt++)
				{
					if (pt == Zero)
						return false;

					FromPt(*pPts++, pt);

					if (iPt == nPointsPerLevel)
						break;

					pt += gpos;
				}

				if (iLev == nLevels)
					break;

				for (uint32_t i = 0; i < nBitsPerLevel; i++)
					gpos = gpos * Two;

				npos = npos * Two;
				if (iLev + 1 == nLevels)
				{
					npos = -npos;
					npos += nums;
				}
			}

			return true;
		}

		void data_cmov(uint32_t* pDst, const uint32_t* pSrc, int nWords, int flag)
		{
			const uint32_t mask0 = flag + ~((uint32_t)0);
			const uint32_t mask1 = ~mask0;

			for (int n = 0; n < nWords; n++)
				pDst[n] = (pDst[n] & mask0) | (pSrc[n] & mask1);
		}

		void SetMul(Point::Native& res, bool bSet, const secp256k1_ge_storage* pPts, const Scalar::Native::uint* p, int nWords)
		{
			static_assert(8 % nBitsPerLevel == 0, "");
			const int nLevelsPerWord = (sizeof(Scalar::Native::uint) << 3) / nBitsPerLevel;
			static_assert(!(nLevelsPerWord & (nLevelsPerWord - 1)), "should be power-of-2");

			NoLeak<secp256k1_ge_storage> ge_s;
			NoLeak<secp256k1_ge> ge;

			// iterating in lsb to msb order
			for (int iWord = 0; iWord < nWords; iWord++)
			{
				Scalar::Native::uint n = p[iWord];

				for (int j = 0; j < nLevelsPerWord; j++, pPts += nPointsPerLevel)
				{
					int nSel = (nPointsPerLevel - 1) & n;
					n >>= nBitsPerLevel;

					/** This uses a conditional move to avoid any secret data in array indexes.
					*   _Any_ use of secret indexes has been demonstrated to result in timing
					*   sidechannels, even when the cache-line access patterns are uniform.
					*  See also:
					*   "A word of warning", CHES 2013 Rump Session, by Daniel J. Bernstein and Peter Schwabe
					*    (https://cryptojedi.org/peter/data/chesrump-20130822.pdf) and
					*   "Cache Attacks and Countermeasures: the Case of AES", RSA 2006,
					*    by Dag Arne Osvik, Adi Shamir, and Eran Tromer
					*    (http://www.tau.ac.il/~tromer/papers/cache.pdf)
					*/

					for (uint32_t i = 0; i < nPointsPerLevel; i++)
						data_cmov((uint32_t*) &ge_s.V, (uint32_t*) (pPts + i), sizeof(ge_s.V) / sizeof(uint32_t), i == nSel);

					ToPt(res, ge.V, ge_s.V, bSet);
					bSet = false;
				}
			}
		}

		void SetMul(Point::Native& res, bool bSet, const secp256k1_ge_storage* pPts, const Scalar::Native& k)
		{
			SetMul(res, bSet, pPts, k.get().d, _countof(k.get().d));
		}


		void InitSeedIteration(Hash::Processor& hp, const char* szSeed, uint32_t n)
		{
			hp << szSeed << n;
		}

		void GeneratePts(const char* szSeed, secp256k1_ge_storage* pPts, uint32_t nLevels)
		{
			for (uint32_t nCounter = 0; ; nCounter++)
			{
				Hash::Processor hp;
				InitSeedIteration(hp, szSeed, nCounter);

				Point::Native g;
				if (!CreatePointNnz(g, hp))
					continue;

				if (CreatePts(pPts, g, nLevels, hp))
					break;
			}
		}

		void Obscured::Initialize(const char* szSeed)
		{
			for (uint32_t nCounter = 0; ; nCounter++)
			{
				Hash::Processor hp;
				InitSeedIteration(hp, szSeed, nCounter);

				Point::Native g;
				if (!CreatePointNnz(g, hp))
					continue;

				Point::Native pt2 = g;
				if (!CreatePts(m_pPts, pt2, nLevels, hp))
					continue;

				hp << "blind-scalar";
				Scalar s0;
				hp >> s0.m_Value;
				if (m_AddScalar.Import(s0))
					continue;

				Generator::SetMul(pt2, true, m_pPts, m_AddScalar); // pt2 = G * blind
				FromPt(m_AddPt, pt2);

				m_AddScalar = -m_AddScalar;

				break;
			}
		}

		void Obscured::AssignInternal(Point::Native& res, bool bSet, Scalar::Native& kTmp, const Scalar::Native& k) const
		{
			secp256k1_ge ge;
			ToPt(res, ge, m_AddPt, bSet);

			kTmp = k + m_AddScalar;

			Generator::SetMul(res, false, m_pPts, kTmp);
		}

		template <>
		void Obscured::Mul<Scalar::Native>::Assign(Point::Native& res, bool bSet) const
		{
			Scalar::Native k2;
			me.AssignInternal(res, bSet, k2, k);
		}

		template <>
		void Obscured::Mul<Scalar>::Assign(Point::Native& res, bool bSet) const
		{
			Scalar::Native k2;
			k2.Import(k); // don't care if overflown (still valid operation)
			me.AssignInternal(res, bSet, k2, k2);
		}

	} // namespace Generator

	/////////////////////
	// Context
	uint64_t g_pContextBuf[(sizeof(Context) + sizeof(uint64_t) - 1) / sizeof(uint64_t)];

#ifndef NDEBUG
	bool g_bContextInitialized = false;
#endif // NDEBUG

	const Context& Context::get()
	{
		assert(g_bContextInitialized);
		return *(Context*) g_pContextBuf;
	}

	void InitializeContext()
	{
		Context& ctx = *(Context*) g_pContextBuf;

		ctx.G.Initialize("G-gen");
		ctx.H.Initialize("H-gen");
		ctx.H_Big.Initialize("H-gen");

		Scalar::Native one, minus_one;
		one = 1U;
		minus_one = -one;

		Point::Native pt, ptAux2(Zero);

#define STR_GEN_PREFIX "ip-"
		char szStr[0x20] = STR_GEN_PREFIX;
		szStr[_countof(STR_GEN_PREFIX) + 2] = 0;

		for (uint32_t i = 0; i < InnerProduct::nDim; i++)
		{
			szStr[_countof(STR_GEN_PREFIX) - 1]	= '0' + (i / 10);
			szStr[_countof(STR_GEN_PREFIX)]		= '0' + (i % 10);

			for (uint32_t j = 0; j < 2; j++)
			{
				szStr[_countof(STR_GEN_PREFIX) + 1] = '0' + j;
				ctx.m_Ipp.m_pGen[j][i].Initialize(szStr);
			}

			pt = ctx.m_Ipp.m_pGen[1][i] * minus_one;
			Generator::FromPt(ctx.m_Ipp.m_pAux1[0][i], pt);

			pt = ctx.m_Ipp.m_pGen[0][i] * one;
			Generator::FromPt(ctx.m_Ipp.m_pAux1[1][i], pt);

			ptAux2 += pt;
		}

		Generator::FromPt(ctx.m_Ipp.m_Aux2, ptAux2);

		ctx.m_Ipp.m_GenDot.Initialize("ip-dot");

#ifndef NDEBUG
		g_bContextInitialized = true;
#endif // NDEBUG
	}

	/////////////////////
	// Commitment
	void Commitment::Assign(Point::Native& res, bool bSet) const
	{
		(Context::get().G * k).Assign(res, bSet);
		res += Context::get().H * val;
	}

	/////////////////////
	// Nonce and key generation
	template <>
	void uintBig::GenerateNonce(const uintBig& sk, const uintBig& msg, const uintBig* pMsg2, uint32_t nAttempt /* = 0 */)
	{
		for (uint32_t i = 0; ; i++)
		{
			if (!nonce_function_rfc6979(m_pData, msg.m_pData, sk.m_pData, NULL, pMsg2 ? (void*) pMsg2->m_pData : NULL, i))
				continue;

			if (!nAttempt--)
				break;
		}
	}

	void Scalar::Native::GenerateNonce(const uintBig& sk, const uintBig& msg, const uintBig* pMsg2, uint32_t nAttempt /* = 0 */)
	{
		NoLeak<Scalar> s;

		for (uint32_t i = 0; ; i++)
		{
			s.V.m_Value.GenerateNonce(sk, msg, pMsg2, i);
			if (Import(s.V))
				continue;

			if (!nAttempt--)
				break;
		}
	}

	void Kdf::DeriveKey(Scalar::Native& out, uint64_t nKeyIndex, uint32_t nFlags, uint32_t nExtra) const
	{
		// the msg hash is not secret
		Hash::Value hv;
		Hash::Processor() << nKeyIndex << nFlags << nExtra >> hv;
		out.GenerateNonce(m_Secret.V, hv, NULL);
	}

	/////////////////////
	// Oracle
	void Oracle::Reset()
	{
		m_hp.Reset();
	}

	void Oracle::operator >> (Scalar::Native& out)
	{
		Scalar s; // not secret

		do
			m_hp >> s.m_Value;
		while (out.Import(s));
	}

	/////////////////////
	// Signature
	void Signature::get_Challenge(Scalar::Native& out, const Point::Native& pt, const Hash::Value& msg)
	{
		Oracle() << pt << msg >> out;
	}

	void Signature::MultiSig::GenerateNonce(const Hash::Value& msg, const Scalar::Native& sk)
	{
		NoLeak<Scalar> sk_;
		sk_.V = sk;

		m_Nonce.GenerateNonce(sk_.V.m_Value, msg, NULL);
	}

	void Signature::CoSign(Scalar::Native& k, const Hash::Value& msg, const Scalar::Native& sk, const MultiSig& msig)
	{
		get_Challenge(k, msig.m_NoncePub, msg);
		m_e = k;

		k *= sk;
		k = -k;
		k += msig.m_Nonce;
	}

	void Signature::Sign(const Hash::Value& msg, const Scalar::Native& sk)
	{
		MultiSig msig;
		msig.GenerateNonce(msg, sk);
		msig.m_NoncePub = Context::get().G * msig.m_Nonce;

		Scalar::Native k;
		CoSign(k, msg, sk, msig);
		m_k = k;
	}

	bool Signature::IsValid(const Hash::Value& msg, const Point::Native& pk) const
	{
		Scalar::Native k(m_k), e(m_e);

		Point::Native pt = Context::get().G * k;

		pt += pk * e;

		get_Challenge(k, pt, msg);

		return e == k;
	}

	int Signature::cmp(const Signature& x) const
	{
		int n = m_e.cmp(x.m_e);
		if (n)
			return n;

		return m_k.cmp(x.m_k);
	}

	/////////////////////
	// RangeProof
	namespace RangeProof
	{
		void get_PtMinusVal(Point::Native& out, const Point& comm, Amount val)
		{
			out = comm;

			Point::Native ptAmount = Context::get().H * val;

			ptAmount = -ptAmount;
			out += ptAmount;
		}

		// Public
		void Public::get_Msg(Hash::Value& hv) const
		{
			Hash::Processor() << m_Value >> hv;
		}

		bool Public::IsValid(const Point& comm) const
		{
			if (m_Value < s_MinimumValue)
				return false;

			Point::Native pk;
			get_PtMinusVal(pk, comm, m_Value);

			Hash::Value hv;
			get_Msg(hv);

			return m_Signature.IsValid(hv, pk);
		}

		void Public::Create(const Scalar::Native& sk)
		{
			assert(m_Value >= s_MinimumValue);
			Hash::Value hv;
			get_Msg(hv);

			m_Signature.Sign(hv, sk);
		}

		int Public::cmp(const Public& x) const
		{
			int n = m_Signature.cmp(x.m_Signature);
			if (n)
				return n;

			if (m_Value < x.m_Value)
				return -1;
			if (m_Value > x.m_Value)
				return 1;

			return 0;
		}


	} // namespace RangeProof


	/////////////////////
	// InnerProduct
	struct InnerProduct::Calculator
	{
		struct ChallengeSet {
			Scalar::Native m_DotMultiplier;
			Scalar::Native m_Val[nCycles][2];
		};

		struct State {
			Point::Native* m_pGen[2];
			Scalar::Native* m_pVal[2];

			void CalcCrossTerm(uint32_t n, const ChallengeSet&, Point::Native* pLR) const;
			void PerformCycle0(const State& src, const ChallengeSet&, Point::Native* pLR, const Modifier&);
			void PerformCycle(uint32_t iCycle, const ChallengeSet&, Point::Native* pLR);
		};

		static void get_Challenge(Scalar::Native* pX, Oracle&);
		static void Mac(Point::Native&, const Generator::Obscured& g, const Scalar::Native& k, const Scalar::Native* pPwrMul, Scalar::Native& pwr, bool bPwrInc);

		static void Aggregate(Point::Native& res, const ChallengeSet&, const Scalar::Native&, int j, uint32_t iPos, uint32_t iCycle, Scalar::Native& pwr, const Scalar::Native* pPwrMul);
	};


	void InnerProduct::Calculator::get_Challenge(Scalar::Native* pX, Oracle& oracle)
	{
		do
			oracle >> pX[0];
		while (pX[0] == Zero);

		pX[1].SetInv(pX[0]);
	}

	void InnerProduct::Calculator::Mac(Point::Native& res, const Generator::Obscured& g, const Scalar::Native& k, const Scalar::Native* pPwrMul, Scalar::Native& pwr, bool bPwrInc)
	{
		if (pPwrMul)
		{
			Scalar::Native k2(k);
			k2 *= pwr;

			if (bPwrInc)
				pwr *= *pPwrMul;

			Mac(res, g, k2, NULL, pwr, bPwrInc);
		}
		else
			res += g * k;
	}

	void InnerProduct::Calculator::State::CalcCrossTerm(uint32_t n, const ChallengeSet& cs, Point::Native* pLR) const
	{
		Scalar::Native crossTrm;
		for (int j = 0; j < 2; j++)
		{
			crossTrm = Zero;

			for (uint32_t i = 0; i < n; i++)
				crossTrm += m_pVal[j][i] * m_pVal[!j][n + i];

			crossTrm *= cs.m_DotMultiplier;

			pLR[j] = Context::get().m_Ipp.m_GenDot * crossTrm;
		}
	}

	void InnerProduct::Calculator::State::PerformCycle0(const State& src, const ChallengeSet& cs, Point::Native* pLR, const Modifier& mod)
	{
		const uint32_t n = nDim >> 1;
		static_assert(n, "");

		src.CalcCrossTerm(n, cs, pLR);

		Scalar::Native pwr0, pwr1;

		for (int j = 0; j < 2; j++)
		{
			if (mod.m_pMultiplier[j])
			{
				pwr0 = 1U;
				pwr1 = *mod.m_pMultiplier[j];

				for (uint32_t i = 1; i < n; i++)
					pwr1 *= *mod.m_pMultiplier[j];
			}

			for (uint32_t i = 0; i < n; i++)
			{
				const Generator::Obscured& g0 = Context::get().m_Ipp.m_pGen[j][i];
				const Generator::Obscured& g1 = Context::get().m_Ipp.m_pGen[j][n + i];

				const Scalar::Native& v0 = src.m_pVal[j][i];
				const Scalar::Native& v1 = src.m_pVal[j][n + i];

				Mac(pLR[j], g1, v0, mod.m_pMultiplier[j], pwr1, false);
				Mac(pLR[!j], g0, v1, mod.m_pMultiplier[j], pwr0, false);

				m_pVal[j][i] = v0 * cs.m_Val[0][j];
				m_pVal[j][i] += v1 * cs.m_Val[0][!j];

				m_pGen[j][i] = Zero;
				Mac(m_pGen[j][i], g0, cs.m_Val[0][!j], mod.m_pMultiplier[j], pwr0, true);
				Mac(m_pGen[j][i], g1, cs.m_Val[0][j], mod.m_pMultiplier[j], pwr1, true);
			}
		}
	}

	void InnerProduct::Calculator::State::PerformCycle(uint32_t iCycle, const ChallengeSet& cs, Point::Native* pLR)
	{
		uint32_t n = nDim >> (iCycle + 1);
		assert(n);

		CalcCrossTerm(n, cs, pLR);

		for (int j = 0; j < 2; j++)
		{
			for (uint32_t i = 0; i < n; i++)
			{
				// inplace modification
				Point::Native& g0 = m_pGen[j][i];
				Point::Native& g1 = m_pGen[j][n + i];

				Scalar::Native& v0 = m_pVal[j][i];
				Scalar::Native& v1 = m_pVal[j][n + i];

				pLR[j] += g1 * v0;
				pLR[!j] += g0 * v1;

				v0 *= cs.m_Val[iCycle][j];
				v0 += v1 * cs.m_Val[iCycle][!j];

				g0 = g0 * cs.m_Val[iCycle][!j];
				g0 += g1 * cs.m_Val[iCycle][j];
			}
		}
	}

	void InnerProduct::Calculator::Aggregate(Point::Native& res, const ChallengeSet& cs, const Scalar::Native& k, int j, uint32_t iPos, uint32_t iCycle, Scalar::Native& pwr, const Scalar::Native* pPwrMul)
	{
		if (iCycle)
		{
			assert(iCycle <= nCycles);
			Scalar::Native k0 = k;
			k0 *= cs.m_Val[nCycles - iCycle][!j];

			Aggregate(res, cs, k0, j, iPos, iCycle - 1, pwr, pPwrMul);

			k0 = k;
			k0 *= cs.m_Val[nCycles - iCycle][j];

			uint32_t nStep = 1 << (iCycle - 1);

			Aggregate(res, cs, k0, j, iPos + nStep, iCycle - 1, pwr, pPwrMul);

		} else
		{
			assert(iPos < nDim);
			Mac(res, Context::get().m_Ipp.m_pGen[j][iPos], k, pPwrMul, pwr, true);
		}
	}

	void InnerProduct::get_Dot(Scalar::Native& res, const Scalar::Native* pA, const Scalar::Native* pB)
	{
		static_assert(nDim, "");
		res = pA[0];
		res *= pB[0];

		Scalar::Native tmp;

		for (uint32_t i = 1; i < nDim; i++)
		{
			tmp = pA[i];
			tmp *= pB[i];
			res += tmp;
		}
	}

	void InnerProduct::Create(const Scalar::Native* pA, const Scalar::Native* pB, const Modifier& mod)
	{
		// bufs
		const uint32_t nBufDim = nDim >> 1;
		Point::Native pBufGen[2][nBufDim];
		Scalar::Native pBufVal[2][nBufDim];

		Calculator::State s0, s1;
		s0.m_pVal[0] = (Scalar::Native*) pA;
		s0.m_pVal[1] = (Scalar::Native*) pB;

		for (int i = 0; i < 2; i++)
		{
			s1.m_pGen[i] = pBufGen[i];
			s1.m_pVal[i] = pBufVal[i];
		}

		Point::Native comm(Zero);


		for (int j = 0; j < 2; j++)
		{
			Scalar::Native pwr0(1U);
			for (uint32_t i = 0; i < nDim; i++)
				Calculator::Mac(comm, Context::get().m_Ipp.m_pGen[j][i], s0.m_pVal[j][i], mod.m_pMultiplier[j], pwr0, true);
		}

		m_AB = comm;

		Oracle oracle;
		oracle << m_AB;

		Calculator::ChallengeSet cs;
		oracle >> cs.m_DotMultiplier;

		Point::Native pLR[2];

		uint32_t n = nDim;
		for (uint32_t iCycle = 0; iCycle < nCycles; iCycle++, n >>= 1)
		{
			Calculator::get_Challenge(cs.m_Val[iCycle], oracle);

			if (iCycle)
				s1.PerformCycle(iCycle, cs, pLR);
			else
				s1.PerformCycle0(s0, cs, pLR, mod);

			for (int j = 0; j < 2; j++)
			{
				m_pLR[iCycle][j] = pLR[j];
				oracle << m_pLR[iCycle][j];
			}
		}

		assert(1 == n);

		for (int i = 0; i < 2; i++)
			m_pCondensed[i] = s1.m_pVal[i][0];
	}

	bool InnerProduct::IsValid(const Scalar::Native& dot, const Modifier& mod) const
	{
		Oracle oracle;
		oracle << m_AB;

		Calculator::ChallengeSet cs;
		oracle >> cs.m_DotMultiplier;

		Point::Native comm = m_AB;
		Point::Native ptTmp;
		Scalar::Native valTmp;

		uint32_t n = nDim;
		for (uint32_t iCycle = 0; iCycle < nCycles; iCycle++, n >>= 1)
		{
			Calculator::get_Challenge(cs.m_Val[iCycle], oracle);

			const Point* pLR = m_pLR[iCycle];
			for (int i = 0; i < 2; i++)
			{
				valTmp = cs.m_Val[iCycle][i];
				valTmp *= valTmp;

				ptTmp = pLR[i];
				comm += ptTmp * valTmp;
			}

			oracle << pLR[0] << pLR[1];
		}

		assert(1 == n);

		// finally verify commitment
		comm = -comm;

		for (int j = 0; j < 2; j++)
		{
			// calculate the transformed generator
			valTmp = m_pCondensed[j];
			ptTmp = Zero;

			Scalar::Native pwr(1U);

			Calculator::Aggregate(ptTmp, cs, valTmp, j, 0, nCycles, pwr, mod.m_pMultiplier[j]);
			comm += ptTmp;
		}

		// add the new (mutated) dot product, substract the original (claimed)
		valTmp = m_pCondensed[0];
		valTmp *= m_pCondensed[1];
		valTmp += -dot;

		valTmp *= cs.m_DotMultiplier;

		comm += Context::get().m_Ipp.m_GenDot * valTmp;

		return comm == Zero;
	}


	/////////////////////
	// Bulletproof
	void RangeProof::Confidential::Create(const Scalar::Native& sk, Amount v)
	{
		Oracle nonceGen, oracle;
		nonceGen << sk << v; // init

		// A = G*alpha + vec(aL)*vec(G) + vec(aR)*vec(H)
		Scalar::Native alpha;
		nonceGen >> alpha;

		Point::Native comm = Context::get().G * alpha;
		Point::Native ptVal(Zero);

		NoLeak<secp256k1_ge> ge;

		for (uint32_t i = 0; i < InnerProduct::nDim; i++)
		{
			uint32_t iBit = 1 & (v >> i);
			Generator::ToPt(comm, ge.V, Context::get().m_Ipp.m_pAux1[iBit][i], false);
		}
		ptVal = -ptVal;
		comm += ptVal;

		m_A = comm;
		oracle << m_A; // exposed

		// S = G*ro + vec(sL)*vec(G) + vec(sR)*vec(H)
		Scalar::Native ro;
		nonceGen >> ro;
		comm = Context::get().G * ro;

		Scalar::Native pS[2][InnerProduct::nDim];

		for (int j = 0; j < 2; j++)
			for (uint32_t i = 0; i < InnerProduct::nDim; i++)
			{
				nonceGen >> pS[j][i];
				comm += Context::get().m_Ipp.m_pGen[j][i] * pS[j][i];
			}

		m_S = comm;
		oracle << m_S; // exposed

		// get challenges

		Scalar::Native x, y, z;
		oracle >> y;
		oracle >> z;

		// calculate t1, t2 - parts of vec(L)*vec(R) which depend on (future) x and x^2.
		Scalar::Native t0(Zero), t1(Zero), t2(Zero);

		Scalar::Native l0, lx, r0, rx, one(1U), two(2U), zz, yPwr, zz_twoPwr;

		zz = z;
		zz *= z;
		yPwr = one;
		zz_twoPwr = zz;

		for (uint32_t i = 0; i < InnerProduct::nDim; i++)
		{
			uint32_t bit = 1 & (v >> i);

			l0 = -z;
			if (bit)
				l0 += one;

			lx = pS[0][i];

			r0 = z;
			if (!bit)
				r0 += -one;

			r0 *= yPwr;
			r0 += zz_twoPwr;

			rx = yPwr;
			rx *= pS[1][i];

			zz_twoPwr *= two;
			yPwr *= y;

			t0 += l0 * r0;
			t1 += l0 * rx;
			t1 += lx * r0;
			t2 += lx * rx;
		}

		Scalar::Native tau1, tau2;
		nonceGen >> tau1;
		nonceGen >> tau2;

		comm = Context::get().G * tau1;
		comm += Context::get().H_Big * t1;

		m_T1 = comm;
		oracle << m_T1; // exposed

		comm = Context::get().G * tau2;
		comm += Context::get().H_Big * t2;

		m_T2 = comm;
		oracle << m_T2; // exposed

		// get challenge 
		oracle >> x;

		// m_TauX = tau2*x^2 + tau1*x + sk*z^2
		l0 = tau2;
		l0 *= x;
		l0 *= x;

		r0 = tau1;
		r0 *= x;
		l0 += r0;

		r0 = zz;
		r0 *= sk; // UTXO blinding factor
		l0 += r0;

		m_TauX = l0;

		// m_Mu = alpha + ro*x
		l0 = ro;
		l0 *= x;
		l0 += alpha;
		m_Mu = l0;

		// m_tDot
		l0 = t0;

		r0 = t1;
		r0 *= x;
		l0 += r0;

		r0 = t2;
		r0 *= x;
		r0 *= x;
		l0 += r0;

		m_tDot = l0;

		// construct vectors l,r, use buffers pS
		// P - m_Mu*G
		yPwr = one;
		zz_twoPwr = zz;

		for (uint32_t i = 0; i < InnerProduct::nDim; i++)
		{
			uint32_t bit = 1 & (v >> i);

			pS[0][i] *= x;

			pS[0][i] += -z;
			if (bit)
				pS[0][i] += one;

			pS[1][i] *= x;
			pS[1][i] *= yPwr;

			r0 = z;
			if (!bit)
				r0 += -one;

			r0 *= yPwr;
			r0 += zz_twoPwr;

			pS[1][i] += r0;

			zz_twoPwr *= two;
			yPwr *= y;
		}

		yPwr.SetInv(y);

		InnerProduct::Modifier mod;
		mod.m_pMultiplier[1] = &yPwr;

		m_P_Tag.Create(pS[0], pS[1], mod);
	}

	bool RangeProof::Confidential::IsValid(const Point& commitment) const
	{
		Oracle oracle;
		Scalar::Native x, y, z, xx, zz, tDot;

		oracle << m_A << m_S;
		oracle >> y;
		oracle >> z;
		oracle << m_T1 << m_T2;
		oracle >> x;

		// calculate delta(y,z) = (z - z^2) * sumY - z^3 * sum2
		Scalar::Native delta, sum2, sumY;


		sum2 = 1U;
		sumY = Zero;
		for (uint32_t i = 0; i < InnerProduct::nDim; i++)
		{
			sumY += sum2;
			sum2 *= y;
		}

		sum2 = Amount(-1);

		zz = z * z;

		delta = z;
		delta += -zz;
		delta *= sumY;

		sum2 *= zz;
		sum2 *= z;
		delta += -sum2;

		// H_Big * m_tDot + G * m_TauX =?= commitment * z^2 + H_Big * delta(y,z) + m_T1*x + m_T2*x^2
		// H_Big * (m_tDot - delta(y,z)) + G * m_TauX =?= commitment * z^2 + m_T1*x + m_T2*x^2

		Point::Native ptVal;

		xx = x * x;

		ptVal = Point::Native(commitment) * zz;
		ptVal += Point::Native(m_T1) * x;
		ptVal += Point::Native(m_T2) * xx;

		ptVal = -ptVal;

		ptVal += Context::get().G * m_TauX;

		tDot = m_tDot;
		sumY = tDot;
		sumY += -delta;
		ptVal += Context::get().H_Big * sumY;

		if (!(ptVal == Zero))
			return false;

		// (P - m_Mu*G) + m_Mu*G =?= m_A + m_S*x - vec(G)*vec(z) + vec(H)*( vec(z) + vec(z^2*2^n*y^-n) )
		ptVal = m_P_Tag.m_AB;
		ptVal += Context::get().G * m_Mu;

		secp256k1_ge ge;
		Point::Native ptSum0;
		Generator::ToPt(ptSum0, ge, Context::get().m_Ipp.m_Aux2, true);
		ptVal += ptSum0 * z;

		ptVal = -ptVal;

		ptVal += Point::Native(m_A);
		ptVal += Point::Native(m_S) * x;

		Scalar::Native yInv, pwr, mul;
		yInv.SetInv(y);

		mul = 2U;
		mul *= yInv;
		pwr = zz;

		for (uint32_t i = 0; i < InnerProduct::nDim; i++)
		{
			sum2 = pwr;
			sum2 += z;

			ptVal += Context::get().m_Ipp.m_pGen[1][i] * sum2;

			pwr *= mul;
		}

		if (!(ptVal == Zero))
			return false;

		// finally check the inner product
		InnerProduct::Modifier mod;
		mod.m_pMultiplier[1] = &yInv;
		if (!m_P_Tag.IsValid(tDot, mod))
			return false;

		return true;
	}

	int RangeProof::Confidential::cmp(const Confidential& x) const
	{
		// don't care
		return memcmp(this, &x, sizeof(*this));
	}

} // namespace ECC

// Needed for test
void secp256k1_ecmult_gen(const secp256k1_context* pCtx, secp256k1_gej *r, const secp256k1_scalar *a)
{
	secp256k1_ecmult_gen(&pCtx->ecmult_gen_ctx, r, a);
}
