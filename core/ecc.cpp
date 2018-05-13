#include "ecc_native.h"
#include <assert.h>

#define ENABLE_MODULE_GENERATOR
#define ENABLE_MODULE_RANGEPROOF

#pragma warning (push, 0) // suppress warnings from secp256k1
#include "../secp256k1-zkp/src/secp256k1.c"
#pragma warning (pop)

// misc
void memset0(void* p, size_t n)
{
	memset(p, 0, n);
}

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
						secp256k1_ge_storage_cmov(&ge_s.V, pPts + i, i == nSel);

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

		Obscured::Obscured(const char* szSeed)
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
	Context::Context()
		:G("G-gen")
		,H("H-gen")
	{
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
		int n = m_e.m_Value.cmp(x.m_e.m_Value);
		if (n)
			return n;

		return m_k.m_Value.cmp(x.m_k.m_Value);
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


		// Confidential - mock only
		bool Confidential::IsValid(const Point&) const
		{
			return memis0(m_pOpaque, sizeof(m_pOpaque));
		}

		void Confidential::Create(const Scalar::Native& sk, Amount val)
		{
			ZeroObject(m_pOpaque);
		}

		int Confidential::cmp(const Confidential& x) const
		{
			return memcmp(m_pOpaque, x.m_pOpaque, sizeof(m_pOpaque));
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

} // namespace ECC

// Needed for test
void secp256k1_ecmult_gen(const secp256k1_context* pCtx, secp256k1_gej *r, const secp256k1_scalar *a)
{
	secp256k1_ecmult_gen(&pCtx->ecmult_gen_ctx, r, a);
}
