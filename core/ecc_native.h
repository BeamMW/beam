#pragma once
#include "ecc.h"

#define USE_BASIC_CONFIG
#include "../secp256k1-zkp/src/basic-config.h"
#include "../secp256k1-zkp/include/secp256k1.h"
#include "../secp256k1-zkp/src/group.h"
#include "../secp256k1-zkp/src/hash.h"


namespace ECC
{

	class Scalar::Native
		:private secp256k1_fe
	{
		void PostModify(); // for simplicity we add a "weak normalization" after every modifying operation.
	public:
		const secp256k1_fe& get() const { return *this; }

		void SetZero();
		void Set(uint32_t);
		void SetNeg(const Native&);
		void SetMul(const Native& a, const Native& b);
		void SetSqr(const Native&);
		bool SetSqrt(const Native&);
		void SetInv(const Native&); // for 0 the result is also 0

		void Neg();
		void Add(const Native&); // not efective for big summations, due to excessive normalizations
		void Mul(const Native&);
		void Mul(uint32_t);
		void Sqr();
		bool Sqrt();
		void Inv(); // for 0 the result is also 0

		bool IsZero() const;
		bool IsQuadraticResidue() const; // analogous to positive/negative in some sense

		bool Import(const Scalar&);
		void Export(Scalar&); // internally normalizes itself, hence non-const.
	};

	class Point::Native
		:private secp256k1_gej
	{
		bool ImportInternal(const Point&);
	public:

		void SetZero();
		void SetNeg(const Native&);
		void SetSum(const Native&, const Native&);

		void Neg();
		void Add(const Native&);

		bool IsZero() const;

		bool Import(const Point&);
		bool Export(Point&) const; // if the point is zero - returns false and zeroes the result
	};

	class Hash::Processor
		:private secp256k1_sha256_t
	{
	public:
		Processor();

		void Reset();
		void Write(const void*, uint32_t);
		void Finalize(Hash::Value&);
	};
}

