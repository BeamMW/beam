#include "ecc_native.h"
#include <assert.h>

#define ENABLE_MODULE_GENERATOR
#define ENABLE_MODULE_RANGEPROOF

#pragma warning (disable: 4244) // conversion from ... to ..., possible loss of data, signed/unsigned mismatch
#pragma warning (disable: 4018) // signed/unsigned mismatch

#include "../beam/secp256k1-zkp/src/secp256k1.c"

#pragma warning (default: 4018)
#pragma warning (default: 4244)


namespace ECC {

	void SecureErase(void* p, uint32_t n)
	{
		memset(p, 0, n);
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

	void Scalar::SetRandom()
	{
		// accept/reject strategy
		do
			m_Value.SetRandom();
		while (!IsValid());
	}

	void Scalar::Native::SetZero()
	{
		secp256k1_scalar_clear(this);
	}

	bool Scalar::Native::IsZero() const
	{
		return secp256k1_scalar_is_zero(this) != 0;
	}

	void Scalar::Native::SetNeg(const Native& v)
	{
		secp256k1_scalar_negate(this, &v);
	}

	void Scalar::Native::Neg()
	{
		SetNeg(*this);
	}

	bool Scalar::Native::Import(const Scalar& v)
	{
		int overflow;
		secp256k1_scalar_set_b32(this, v.m_Value.m_pData, &overflow);
		return overflow != 0;
	}

	void Scalar::Native::ImportFix(uintBig& v)
	{
		static_assert(sizeof(v) == sizeof(Scalar), "");
		while (Import((const Scalar&) v))
		{
			// overflow - better to retry (to have uniform distribution)
			Hash::Processor hp; // NoLeak?
			hp.Write(v);
			hp.Finalize(v);
		}
	}

	void Scalar::Native::Export(Scalar& v) const
	{
		secp256k1_scalar_get_b32(v.m_Value.m_pData, this);
	}

	void Scalar::Native::Set(uint32_t v)
	{
		secp256k1_scalar_set_int(this, v);
	}

	void Scalar::Native::Set(uint64_t v)
	{
		secp256k1_scalar_set_u64(this, v);
	}

	void Scalar::Native::SetSum(const Native& a, const Native& b)
	{
		secp256k1_scalar_add(this, &a, &b);
	}

	void Scalar::Native::Add(const Native& v)
	{
		SetSum(*this, v);
	}

	void Scalar::Native::SetMul(const Native& a, const Native& b)
	{
		secp256k1_scalar_mul(this, &a, &b);
	}

	void Scalar::Native::Mul(const Native& v)
	{
		SetMul(*this, v);
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
		Write(sz, strlen(sz));
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
		Write(v.m_bQuadraticResidue);
	}

	void Hash::Processor::Write(const Point::Native& v)
	{
		Point pt;
		v.Export(pt);
		Write(pt);
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

		if (m_bQuadraticResidue < v.m_bQuadraticResidue)
			return -1;
		if (m_bQuadraticResidue > v.m_bQuadraticResidue)
			return 1;

		return 0;
	}

	bool Point::Native::ImportInternal(const Point& v)
	{
		NoLeak<secp256k1_fe> nx;
		if (!secp256k1_fe_set_b32(&nx.V, v.m_X.m_pData))
			return false;

		NoLeak<secp256k1_ge> ge;
		if (!secp256k1_ge_set_xquad(&ge.V, &nx.V))
			return false;

		if (!v.m_bQuadraticResidue)
			secp256k1_fe_negate(&ge.V.y, &ge.V.y, 1);

		secp256k1_gej_set_ge(this, &ge.V);

		return true;
	}

	bool Point::Native::Import(const Point& v)
	{
		if (ImportInternal(v))
			return true;

		SetZero();
		return false;
	}

	bool Point::Native::Export(Point& v) const
	{
		if (IsZero())
		{
			memset(&v, 0, sizeof(v));
			return false;
		}

		NoLeak<secp256k1_gej> dup;
		dup.V = *this;
		NoLeak<secp256k1_ge> ge;
		secp256k1_ge_set_gej(&ge.V, &dup.V);

		secp256k1_fe_normalize(&ge.V.x);
		secp256k1_fe_get_b32(v.m_X.m_pData, &ge.V.x);
		v.m_bQuadraticResidue = (secp256k1_fe_is_quad_var(&ge.V.y) != 0);

		return true;
	}

	void Point::Native::Import(const secp256k1_ge_storage& v)
	{
		NoLeak<secp256k1_ge> ge;
		secp256k1_ge_from_storage(&ge.V, &v);
		Import(ge.V);
	}

	void Point::Native::Import(const secp256k1_ge& v)
	{
		secp256k1_gej_set_ge(this, &v);
	}

	void Point::Native::Export(secp256k1_ge_storage& v)
	{
		NoLeak<secp256k1_ge> ge;
		secp256k1_ge_set_gej(&ge.V, this);
		secp256k1_ge_to_storage(&v, &ge.V);
	}

	void Point::Native::SetZero()
	{
		secp256k1_gej_set_infinity(this);
	}

	bool Point::Native::IsZero() const
	{
		return secp256k1_gej_is_infinity(this) != 0;
	}

	void Point::Native::SetNeg(const Native& v)
	{
		secp256k1_gej_neg(this, &v);
	}

	void Point::Native::Neg()
	{
		SetNeg(*this);
	}

	void Point::Native::SetSum(const Native& a, const Native& b)
	{
		secp256k1_gej_add_var(this, &a, &b, NULL);
	}

	void Point::Native::Add(const Native& v)
	{
		SetSum(*this, v);
	}

	void Point::Native::SetX2(const Native& v)
	{
		secp256k1_gej_double_var(this, &v, NULL);
	}

	void Point::Native::X2()
	{
		SetX2(*this);
	}

	void Point::Native::AddMul(const Native& v, const Scalar& k)
	{
		Point::Native pt = v;

		for (uint32_t iByte = _countof(k.m_Value.m_pData); iByte--; )
		{
			uint8_t n = k.m_Value.m_pData[iByte];

			for (uint32_t iBit = 0; iBit < 8; iBit++, pt.X2())
				if (1 & (n >> iBit))
					Add(pt);
		}
	}

	/////////////////////
	// Generator
	namespace Generator
	{
		bool CreatePointNnz(Point::Native& out, const uintBig& x)
		{
			Point pt;
			pt.m_X = x;
			pt.m_bQuadraticResidue = false;

			return out.Import(pt) && !out.IsZero();
		}

		bool CreatePointNnz(Point::Native& out, Hash::Processor& hp)
		{
			Hash::Value hv;
			hp.Finalize(hv);
			return CreatePointNnz(out, hv);
		}

		bool CreatePts(secp256k1_ge_storage* pPts, Point::Native& gpos, uint32_t nLevels, Hash::Processor& hp)
		{
			Point::Native nums, npos, pt;

			hp.Write("nums");
			if (!CreatePointNnz(nums, hp))
				return false;

			nums.Add(gpos);

			npos = nums;

			for (uint32_t iLev = 1; ; iLev++)
			{
				pt = npos;

				for (uint32_t iPt = 1; ; iPt++)
				{
					if (pt.IsZero())
						return false;

					pt.Export(*pPts++);

					if (iPt == nPointsPerLevel)
						break;

					pt.Add(gpos);
				}

				if (iLev == nLevels)
					break;

				for (uint32_t i = 0; i < nBitsPerLevel; i++)
					gpos.X2();

				npos.X2();
				if (iLev + 1 == nLevels)
				{
					npos.Neg();
					npos.Add(nums);
				}
			}

			return true;
		}

		void SetMul(Point::Native& res, bool bSet, const secp256k1_ge_storage* pPts, uint32_t nLevels, const Scalar& k)
		{
			static_assert(8 % nBitsPerLevel == 0, "");
			const uint8_t nLevelsPerByte = 8 / nBitsPerLevel;
			static_assert(!(nLevelsPerByte & (nLevelsPerByte - 1)), "should be power-of-2");

			NoLeak<Point::Native> np;
			NoLeak<secp256k1_ge_storage> ge;

			uint32_t n0 = _countof(k.m_Value.m_pData) - nLevels / nLevelsPerByte;

#ifdef _DEBUG
			for (uint32_t i = 0; i < n0; i++)
				assert(!k.m_Value.m_pData[i]);
#endif // _DEBUG

			// iterating in lsb to msb order
			for (uint32_t iByte = _countof(k.m_Value.m_pData); iByte-- > n0; )
			{
				uint8_t n = k.m_Value.m_pData[iByte];

				for (uint8_t j = 0; j < nLevelsPerByte; j++, pPts += nPointsPerLevel)
				{
					uint32_t nSel = (nPointsPerLevel - 1) & n;
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
						secp256k1_ge_storage_cmov(&ge.V, pPts + i, i == nSel);

					if (bSet)
					{
						bSet = false;
						res.Import(ge.V);
					} else
					{
						// secp256k1_gej_add_ge(r, r, &add);
						np.V.Import(ge.V);
						res.Add(np.V);
					} 
				}
			}
		}

		void SetMul(Point::Native& res, bool bSet, const secp256k1_ge_storage* pPts, uint32_t nLevels, const Scalar::Native& k)
		{
			NoLeak<Scalar> k2;
			k.Export(k2.V);
			SetMul(res, bSet, pPts, nLevels, k2.V);
		}

		void InitSeedIteration(Hash::Processor& hp, const char* szSeed, uint32_t n)
		{
			hp.Write(szSeed);
			hp.WriteOrd(n);
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

				hp.Write("blind-scalar");
				Scalar s0;
				hp.Finalize(s0.m_Value);
				if (m_AddScalar.Import(s0))
					continue;

				Generator::SetMul(pt2, true, m_pPts, nLevels, m_AddScalar); // pt2 = G * blind
				pt2.Export(m_AddPt);

				m_AddScalar.Neg();

				break;
			}
		}

		void Obscured::SetMul(Point::Native& res, bool bSet, const Scalar::Native& k) const
		{
			if (bSet)
				res.Import(m_AddPt);
			else
			{
				Point::Native pt;
				pt.Import(m_AddPt);
				res.Add(pt);
			}

			NoLeak<Scalar::Native> k2;
			k2.V.SetSum(k, m_AddScalar);

			Generator::SetMul(res, false, m_pPts, nLevels, k2.V);
		}

		void Obscured::SetMul(Point::Native& res, bool bSet, const Scalar& k) const
		{
			NoLeak<Scalar::Native> k2;
			k2.V.Import(k); // don't care if overflown (still valid operation)
			SetMul(res, bSet, k2.V);
		}

	} // namespace Generator

	/////////////////////
	// Context
	Context::Context()
		:G("G-gen")
		,H("H-gen")
	{
	}

	void Context::Excess(Point::Native& res, const Scalar::Native& k) const
	{
		G.SetMul(res, true, k);
	}

	void Context::Commit(Point::Native& res, const Scalar::Native& k, const Scalar::Native& v) const
	{
		Excess(res, k);
		H.SetMul(res, false, v);
	}

	void Context::Commit(Point::Native& res, const Scalar::Native& k, const Amount& v, Scalar::Native& vOut) const
	{
		vOut.Set(v);
		Commit(res, k, vOut);
	}

	void Context::Commit(Point::Native& res, const Scalar::Native& k, const Amount& v) const
	{
		NoLeak<Scalar::Native> vOut;
		Commit(res, k, v, vOut.V);
	}

	/////////////////////
	// Oracle
	void Oracle::Reset()
	{
		m_hp.Reset();
	}

	void Oracle::GetChallenge(Scalar::Native& out)
	{
		Hash::Value hv; // not secret
		m_hp.Finalize(hv);
		out.ImportFix(hv);
	}

	void Oracle::Add(const void* p, uint32_t n)
	{
		m_hp.Write(p, n);
	}

	/////////////////////
	// Signature
	void Signature::get_Challenge(Scalar::Native& out, const Point::Native& pt, const Hash::Value& msg)
	{
		Oracle oracle;
		oracle.Add(pt);
		oracle.Add(msg);

		oracle.GetChallenge(out);
	}

	void Signature::MultiSig::GenerateNonce(const Hash::Value& msg, const Scalar::Native& sk)
	{
		NoLeak<Scalar> s0, sk_;
		sk.Export(sk_.V);

		for (uint32_t nAttempt = 0; ; nAttempt++)
			if (secp256k1_nonce_function_default(s0.V.m_Value.m_pData, msg.m_pData, sk_.V.m_Value.m_pData, NULL, NULL, nAttempt) && !m_Nonce.V.Import(s0.V))
				break;

		Context::get().Excess(m_NoncePub, m_Nonce.V);
	}

	void Signature::CoSign(const Hash::Value& msg, const Scalar::Native& sk, const MultiSig& msig)
	{
		Scalar::Native e;
		get_Challenge(e, msig.m_NoncePub, msg);

		e.Export(m_e);

		e.Mul(sk);
		e.Neg();
		e.Add(msig.m_Nonce.V);

		e.Export(m_k);
	}

	void Signature::Sign(const Hash::Value& msg, const Scalar::Native& sk)
	{
		MultiSig msig;
		msig.GenerateNonce(msg, sk);
		CoSign(msg, sk, msig);
	}

	bool Signature::IsValid(const Hash::Value& msg, const Point::Native& pk) const
	{
		Scalar::Native sig;
		sig.Import(m_k);

		Point::Native pt;
		Context::get().Excess(pt, sig);

		pt.AddMul(pk, m_e);

		get_Challenge(sig, pt, msg);
		Scalar e;
		sig.Export(e);

		return e.m_Value == m_e.m_Value;
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
			out.Import(comm);

			Scalar s;
			s.m_Value.Set(val);

			Point::Native ptAmount;
			Context::get().H.SetMul(ptAmount, true, s);

			ptAmount.Neg();
			out.Add(ptAmount);
		}


		// Confidential - mock only
		bool Confidential::IsValid(const Point&) const
		{
			for (int i = 0; i < _countof(m_pOpaque); i++)
				if (m_pOpaque[i])
					return false;
			return true;
		}

		void Confidential::Create(const Scalar::Native& sk, Amount val)
		{
			memset(m_pOpaque, 0, sizeof(m_pOpaque));
		}

		int Confidential::cmp(const Confidential& x) const
		{
			return memcmp(m_pOpaque, x.m_pOpaque, sizeof(m_pOpaque));
		}

		// Public
		void Public::get_Msg(Hash::Value& hv) const
		{
			Hash::Processor hp;
			hp.WriteOrd(m_Value);
			hp.Finalize(hv);
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
