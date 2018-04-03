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
		Scalar::Native nx;
		if (!nx.Import(v.m_X))
			return false;

		secp256k1_ge ge;
		if (!secp256k1_ge_set_xquad(&ge, &nx.get()))
			return false;

		if (!v.m_bQuadraticResidue)
		{
			static_assert(sizeof(ge.y) == sizeof(Scalar::Native), "");
			Scalar::Native& ny = (Scalar::Native&) ge.y;
			ny.Neg();
		}

		secp256k1_gej_set_ge(this, &ge);

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

		secp256k1_ge ge;
		secp256k1_gej dup = *this;
		secp256k1_ge_set_gej(&ge, &dup);

		Scalar::Native& nx = (Scalar::Native&) ge.x;
		nx.Export(v.m_X);

		v.m_bQuadraticResidue = (secp256k1_gej_has_quad_y_var(this) != 0);

		return true;
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

} // namespace ECC
