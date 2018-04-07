#pragma once
#include "ecc.h"

#define USE_BASIC_CONFIG
#include "../secp256k1-zkp/src/basic-config.h"
#include "../secp256k1-zkp/include/secp256k1.h"
#include "../secp256k1-zkp/src/scalar.h"
#include "../secp256k1-zkp/src/group.h"
#include "../secp256k1-zkp/src/hash.h"


namespace ECC
{

	class Scalar::Native
		:private secp256k1_scalar
	{
	public:
		const secp256k1_scalar& get() const { return *this; }

		void SetZero();
		void Set(uint32_t);
		void Set(uint64_t);
		void SetNeg(const Native&);
		void SetSum(const Native& a, const Native& b);
		void SetMul(const Native& a, const Native& b);
		void SetSqr(const Native&);
		void SetInv(const Native&); // for 0 the result is also 0

		void Neg();
		void Add(const Native&); // not efective for big summations, due to excessive normalizations
		void Mul(const Native&);
		void Sqr();
		void Inv(); // for 0 the result is also 0

		bool IsZero() const;

		bool Import(const Scalar&); // on overflow auto-normalizes and returns true
		void ImportFix(uintBig&); // on overflow input is mutated (auto-hashed)
		void Export(Scalar&) const;
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
		void AddMul(const Native&, const Scalar&); // naive (non-secure) implementation, suitable for casual use (such as signature verification), otherwise should use generators
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

		template <uint32_t nBits_>
		class Base
		{
		protected:
			static const uint32_t nLevels = nBits_ / nBitsPerLevel;
			static_assert(nLevels * nBitsPerLevel == nBits_, "");

			secp256k1_ge_storage m_pPts[nLevels * nPointsPerLevel];
		};

		void GeneratePts(const char* szSeed, secp256k1_ge_storage* pPts, uint32_t nLevels);
		void SetMul(Point::Native& res, bool bSet, const secp256k1_ge_storage* pPts, uint32_t nLevels, const Scalar&);
		void SetMul(Point::Native& res, bool bSet, const secp256k1_ge_storage* pPts, uint32_t nLevels, const Scalar::Native&);

		template <uint32_t nBits_>
		class Simple
			:public Base<nBits_>
		{
		public:
			Simple(const char* szSeed) { GeneratePts(szSeed, m_pPts, nLevels); }

			void SetMul(Point::Native& res, bool bSet, const Scalar::Native& k) const
			{
				Generator::SetMul(res, bSet, m_pPts, nLevels, k);
			}

			void SetMul(Point::Native& res, bool bSet, const Scalar& k) const
			{
				Generator::SetMul(res, bSet, m_pPts, nLevels, k);
			}
		};

		class Obscured
			:public Base<nBits>
		{
			secp256k1_ge_storage m_AddPt;
			Scalar::Native m_AddScalar;

		public:
			Obscured(const char* szSeed);

			void SetMul(Point::Native& res, bool bSet, const Scalar::Native& k) const;
			void SetMul(Point::Native& res, bool bSet, const Scalar& k) const;
		};

	} // namespace Generator

	class Hash::Processor
		:private secp256k1_sha256_t
	{
	public:
		Processor();

		void Reset();
		void Write(const void*, uint32_t);
		void Write(const char*);
		void Finalize(Hash::Value&);
	};

	struct Context
	{
		Context();
		static const Context& get();

		const Generator::Obscured						G;
		const Generator::Simple<sizeof(Amount) << 3>	H;

		void Commit(Point::Native& res, const Scalar::Native& k, const Scalar::Native& v) const;
		void Commit(Point::Native& res, const Scalar::Native&, const Amount&) const;
		void Commit(Point::Native& res, const Scalar::Native&, const Amount& v, Scalar::Native& vOut) const;
		void Excess(Point::Native& res, const Scalar::Native&) const;
	};

	class Oracle
	{
		Hash::Processor m_hp;
	public:
		void Reset();
		void GetChallenge(Scalar::Native&);

		void Add(const void*, uint32_t);
		void Add(const uintBig&);
	};
}
