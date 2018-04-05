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

	const uintBig g_Prime = { 
		0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
		0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFE,0xFF,0xFF,0xFC,0x2F
	};

	/////////////////////
	// Scalar

	bool Scalar::IsValid() const
	{
		return m_Value < g_Prime;
	}

	void Scalar::SetRandom()
	{
		// accept/reject strategy
		do
			m_Value.SetRandom();
		while (!IsValid());
	}

	void Scalar::Native::PostModify()
	{
		secp256k1_fe_normalize_weak(this);
	}

	void Scalar::Native::SetZero()
	{
		secp256k1_fe_clear(this);
	}

	bool Scalar::Native::IsZero() const
	{
		return secp256k1_fe_normalizes_to_zero(this) != 0;
	}

	void Scalar::Native::SetNeg(const Native& v)
	{
		secp256k1_fe_negate(this, &v, 1);
		PostModify();
	}

	void Scalar::Native::Neg()
	{
		SetNeg(*this);
	}

	bool Scalar::Native::Import(const Scalar& v)
	{
		return secp256k1_fe_set_b32(this, v.m_Value.m_pData) != 0;
	}

	void Scalar::Native::Export(Scalar& v)
	{
		secp256k1_fe_normalize(this);
		secp256k1_fe_get_b32(v.m_Value.m_pData, this);
	}

	void Scalar::Native::Set(uint32_t v)
	{
		secp256k1_fe_set_int(this, v);
	}

	void Scalar::Native::Add(const Native& v)
	{
		secp256k1_fe_add(this, &v);
		PostModify();
	}

	void Scalar::Native::SetMul(const Native& a, const Native& b)
	{
		secp256k1_fe_mul(this, &a, &b);
	}

	void Scalar::Native::Mul(const Native& v)
	{
		SetMul(*this, v);
	}

	void Scalar::Native::Mul(uint32_t v)
	{
		secp256k1_fe_mul_int(this, v);
		PostModify();
	}

	void Scalar::Native::SetSqr(const Native& v)
	{
		secp256k1_fe_sqr(this, &v);
	}

	void Scalar::Native::Sqr()
	{
		SetSqr(*this);
	}

	bool Scalar::Native::SetSqrt(const Native& v)
	{
		assert(this != &v); // inplace isn't allowed. Actually the calculation'd be ok, but the retval will be false
		return secp256k1_fe_sqrt(this, &v) != 0;
	}

	bool Scalar::Native::Sqrt()
	{
		return SetSqrt(Native(*this));
	}

	bool Scalar::Native::IsQuadraticResidue() const
	{
		return secp256k1_fe_is_quad_var(this) != 0;
	}

	void Scalar::Native::SetInv(const Native& v)
	{
		secp256k1_fe_inv(this, &v);
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


	/////////////////////
	// Point

	bool Point::Native::ImportInternal(const Point& v)
	{
		NoLeak<Scalar::Native> nx;
		if (!nx.V.Import(v.m_X))
			return false;

		NoLeak<secp256k1_ge> ge;
		if (!secp256k1_ge_set_xquad(&ge.V, &nx.V.get()))
			return false;

		if (!v.m_bQuadraticResidue)
		{
			static_assert(sizeof(ge.V.y) == sizeof(Scalar::Native), "");
			Scalar::Native& ny = (Scalar::Native&) ge.V.y;
			ny.Neg();
		}

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

		Scalar::Native& nx = (Scalar::Native&) ge.V.x;
		nx.Export(v.m_X);

		v.m_bQuadraticResidue = (secp256k1_gej_has_quad_y_var(this) != 0);

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

	/////////////////////
	// Generator
	namespace Generator
	{
		void CreatePointUntilSucceed(Point::Native& out, const uintBig& x)
		{
			Point pt;
			pt.m_X.m_Value = x;
			pt.m_bQuadraticResidue = false;

			while (true)
			{
				if (out.Import(pt) && !out.IsZero())
					break;

				Hash::Processor hp;
				hp.Write(pt.m_X.m_Value.m_pData, sizeof(pt.m_X.m_Value));

				static const char sz[] = "point-gen-retry";
				hp.Write(sz, sizeof(sz)-1);
				hp.Finalize(pt.m_X.m_Value);
			}
		}

		void GetNums(Point::Native& nums)
		{
			static const char sz[] = "The scalar for this x is unknown";
			static_assert(sizeof(sz) == sizeof(uintBig) + 1, "");

			CreatePointUntilSucceed(nums, (const uintBig&) sz);
		}

		void CreatePts(secp256k1_ge_storage* pPts, Point::Native& gpos, uint32_t nLevels)
		{
			Point::Native nums, npos, pt;

			GetNums(nums);
			nums.Add(gpos);

			npos = nums;

			for (uint32_t iLev = 1; ; iLev++)
			{
				pt = npos;

				for (uint32_t iPt = 1; ; iPt++)
				{
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
		}

		void SetMul(Point::Native& res, bool bSet, const secp256k1_ge_storage* pPts, uint32_t nLevels, const Scalar& k)
		{
			static_assert(8 % nBitsPerLevel == 0, "");
			const uint8_t nLevelsPerByte = 8 / nBitsPerLevel;
			static_assert(!(nLevelsPerByte & (nLevelsPerByte - 1)), "should be power-of-2");

			NoLeak<Point::Native> np;
			NoLeak<secp256k1_ge_storage> ge;

			// iterating in lsb to msb order
			for (uint32_t iByte = nLevels / nLevelsPerByte; iByte--; )
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

		void HashFromSeedEx(Hash::Value& out, const char* szSeed, const char* szSeed2)
		{
			Hash::Processor hp;
			hp.Write(szSeed, strlen(szSeed));
			hp.Write(szSeed2, strlen(szSeed2));
			hp.Finalize(out);
		}

		void Create(secp256k1_ge_storage* pPts, Blind& blind, uint32_t nLevels, const char* szSeed)
		{
			Point::Native g;

			Hash::Value hv;
			HashFromSeedEx(hv, szSeed, "generator");
			CreatePointUntilSucceed(g, hv);

			Scalar k;
			HashFromSeedEx(k.m_Value, szSeed, "blind-scalar");
			while (true)
			{
				if (blind.m_AddScalar.Import(k))
					break;

				Hash::Processor hp;
				hp.Write(k.m_Value.m_pData, sizeof(k.m_Value.m_pData));
				hp.Finalize(k.m_Value);
			}

			// multiply the g by the blind.m_AddScalar
			const uint32_t nLevelsMax = nBits / nBitsPerLevel;
			secp256k1_ge_storage pPts2[nLevelsMax * nPointsPerLevel];

			Point::Native ptBlind = g;
			CreatePts(pPts2, ptBlind, nLevelsMax);

			SetMul(ptBlind, true, pPts2, nLevelsMax, k);

			ptBlind.Export(blind.m_AddPt);
			blind.m_AddScalar.Neg();

			CreatePts(pPts, g, nLevels); // finally
		}

		void SetMul(Point::Native& res, bool bSet, const secp256k1_ge_storage* pPts, uint32_t nLevels, const Blind& blind, Scalar::Native& k)
		{
/*			if (bSet)
				res.Import(blind.m_AddPt);
			else
			{
				Point::Native pt;
				pt.Import(blind.m_AddPt);
				res.Add(pt);
			}

			k.Add(blind.m_AddScalar);
*/
			NoLeak<Scalar> k2;
			k.Export(k2.V);

			SetMul(res, /*false*/bSet, pPts, nLevels, k2.V);
		}

		bool SetMul(Point::Native& res, bool bSet, const secp256k1_ge_storage* pPts, uint32_t nLevels, const Blind& blind, const Scalar& k)
		{
			NoLeak<Scalar::Native> k2;
			if (!k2.V.Import(k))
				return false;

			SetMul(res, bSet, pPts, nLevels, blind, k2.V);
			return true;
		}


	} // namespace Generator

} // namespace ECC
