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
		void SetX2(const Native&);

		void Neg();
		void Add(const Native&);
		void X2();

		bool IsZero() const;

		bool Import(const Point&);
		bool Export(Point&) const; // if the point is zero - returns false and zeroes the result

		// more compact form, yet straightforward import
		// non-native Point is more compressed (just 1 coordinate and flag), but import is complex
		void Import(const secp256k1_ge_storage&);
		void Export(secp256k1_ge_storage&);

		void Import(const secp256k1_ge&);
	};

	namespace Generator
	{
		static const uint32_t nBitsPerLevel = 4;
		static const uint32_t nPointsPerLevel = 1 << nBitsPerLevel; // 16

		struct Blind {
			secp256k1_ge_storage m_AddPt;
			Scalar::Native m_AddScalar;
		};

		void Create(secp256k1_ge_storage* pPts, Blind&, uint32_t nLevels, const char* szSeed);

		void SetMul(Point::Native& res, bool bSet, const secp256k1_ge_storage* pPts, uint32_t nLevels, const Blind&, Scalar::Native& k);
		bool SetMul(Point::Native& res, bool bSet, const secp256k1_ge_storage* pPts, uint32_t nLevels, const Blind&, const Scalar& k);

		void SetMul(Point::Native& res, bool bSet, const secp256k1_ge_storage* pPts, uint32_t nLevels, const Scalar& k);

		template <uint32_t nBits_>
		class Instance
		{
			static const uint32_t nLevels = nBits_ / nBitsPerLevel;
			static_assert(nLevels * nBitsPerLevel == nBits_, "");

			Blind m_Blind;
			secp256k1_ge_storage m_pPts[nLevels * nPointsPerLevel];

		public:
			Instance(const char* szSeed) { Create(m_pPts, m_Blind, nLevels, szSeed); }
			// Generators are not secret

			void SetMul(Point::Native& res, bool bSet, const Scalar::Native& k) const
			{
				NoLeak<Scalar::Native> k2;
				k2.V = k;
				Generator::SetMul(res, bSet, m_pPts, nLevels, m_Blind, k2.V);
			}

			bool SetMul(Point::Native& res, bool bSet, const Scalar& k) const
			{
				//return Generator::SetMul(res, bSet, m_pPts, nLevels, m_Blind, k);
				Generator::SetMul(res, bSet, m_pPts, nLevels, k); return true;
			}
		};


	} // namespace Generator

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

