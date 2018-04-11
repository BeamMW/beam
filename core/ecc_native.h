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
		typedef Op::Unary<Op::Minus, Native>			Minus;
		typedef Op::Binary<Op::Plus, Native, Native>	Plus;
		typedef Op::Binary<Op::Mul, Native, Native>		Mul;
	public:
		const secp256k1_scalar& get() const { return *this; }

		Minus	operator - () const { return Minus(*this); }
		Plus	operator + (const Native& y) const { return Plus(*this, y); }
		Mul		operator * (const Native& y) const { return Mul(*this, y); }

		bool operator == (Zero_) const;

		Native& operator = (Zero_);
		Native& operator = (uint32_t);
		Native& operator = (uint64_t);
		Native& operator = (Minus);
		Native& operator = (Plus);
		Native& operator = (Mul);
		Native& operator += (const Native& v) { return *this = *this + v; }
		Native& operator *= (const Native& v) { return *this = *this * v; }

		void SetSqr(const Native&);
		void Sqr();
		void SetInv(const Native&); // for 0 the result is also 0
		void Inv();

		bool Import(const Scalar&); // on overflow auto-normalizes and returns true
		void ImportFix(uintBig&); // on overflow input is mutated (auto-hashed)
		void Export(Scalar&) const;
	};

	class Point::Native
		:private secp256k1_gej
	{
		typedef Op::Unary<Op::Minus, Native>			Minus;
		typedef Op::Unary<Op::Double, Native>			Double;
		typedef Op::Binary<Op::Plus, Native, Native>	Plus;
		typedef Op::Binary<Op::Mul, Native, Scalar>		Mul;

		bool ImportInternal(const Point&);
	public:
		secp256k1_gej& get_Raw() { return *this; } // use with care

		Minus	operator - () const { return Minus(*this); }
		Plus	operator + (const Native& y) const { return Plus(*this, y); }
		Mul		operator * (const Scalar& y) const { return Mul(*this, y); }
		Double	operator * (Two_) const { return Double(*this); }

		bool operator == (Zero_) const;

		Native& operator = (Zero_);
		Native& operator = (Minus);
		Native& operator = (Plus);
		Native& operator = (Double);
		Native& operator += (const Native& v) { return *this = *this + v; }
		Native& operator += (Mul); // naive (non-secure) implementation, suitable for casual use (such as signature verification), otherwise should use generators

		template <class Setter> Native& operator = (const Setter& v) { v.Assign(*this, true); return *this; }
		template <class Setter> Native& operator += (const Setter& v) { v.Assign(*this, false); return *this; }

		bool Import(const Point&);
		bool Export(Point&) const; // if the point is zero - returns false and zeroes the result
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
			template <typename TScalar>
			struct Mul
			{
				const Simple& me;
				const TScalar k;
				Mul(const Simple& me_, const TScalar k_) :me(me_) ,k(k_) {}

				void Assign(Point::Native& res, bool bSet) const
				{
					static_assert(sizeof(TScalar) * 8 <= nBits_, "generator too short");

					NoLeak<Scalar> s;
					s.V.m_Value = k;
					Generator::SetMul(res, bSet, me.m_pPts, Base<nBits_>::nLevels, s.V);
				}
			};

		public:
			Simple(const char* szSeed) { GeneratePts(szSeed, Base<nBits_>::m_pPts, Base<nBits_>::nLevels); }

			template <typename TScalar>
			Mul<TScalar> operator * (const TScalar& k) const { return Mul<TScalar>(*this, k); }
		};

		class Obscured
			:public Base<nBits>
		{
			secp256k1_ge_storage m_AddPt;
			Scalar::Native m_AddScalar;

			template <typename TScalar>
			struct Mul
			{
				const Obscured& me;
				const TScalar k;
				Mul(const Obscured& me_, const TScalar k_) :me(me_) ,k(k_) {}

				void Assign(Point::Native& res, bool bSet) const;
			};

		public:
			Obscured(const char* szSeed);

			template <typename TScalar>
			Mul<TScalar> operator * (const TScalar& k) const { return Mul<TScalar>(*this, k); }
		};

	} // namespace Generator

	struct Signature::MultiSig
	{
		NoLeak<Scalar::Native> m_Nonce; // specific signer
		Point::Native m_NoncePub;		// sum of all co-signers

		void GenerateNonce(const Hash::Value& msg, const Scalar::Native& sk);
	};


	class Hash::Processor
		:private secp256k1_sha256_t
	{
		void Write(const char*);
		void Write(bool);
		void Write(uint8_t);
		void Write(const uintBig&);
		void Write(const Scalar&);
		void Write(const Point&);
		void Write(const Point::Native&);

		template <typename T>
		void Write(T v)
		{
			static_assert(T(-1) > 0, "must be unsigned");
			for (; ; v >>= 8)
			{
				Write((uint8_t) v);
				if (!v)
					break;
			}
		}

		void Finalize(Value&);

	public:
		Processor();

		void Reset();

		void Write(const void*, uint32_t);

		template <typename T>
		Processor& operator << (const T& t) { Write(t); return *this; }

		void operator >> (Value& hv) { Finalize(hv); }
	};

	struct Context
	{
		Context();
		static const Context& get();

		const Generator::Obscured						G;
		const Generator::Simple<sizeof(Amount) << 3>	H;
	};

	class Commitment
	{
		const Scalar::Native& k;
		const Amount& val;
	public:
		Commitment(const Scalar::Native& k_, const Amount& val_) :k(k_) ,val(val_) {}
		void Assign(Point::Native& res, bool bSet) const;
	};

	class Oracle
	{
		Hash::Processor m_hp;
	public:
		void Reset();

		template <typename T>
		Oracle& operator << (const T& t) { m_hp << t; return *this; }

		void operator >> (Scalar::Native&);
	};
}
