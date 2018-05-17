#pragma once
#include "ecc.h"

#define USE_BASIC_CONFIG

#if defined(__clang__) || defined(__GNUC__) || defined(__GNUG__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunused-function"
#else
    #pragma warning (push, 0) // suppress warnings from secp256k1
#endif

#include "../secp256k1-zkp/src/basic-config.h"
#include "../secp256k1-zkp/include/secp256k1.h"
#include "../secp256k1-zkp/src/scalar.h"
#include "../secp256k1-zkp/src/group.h"
#include "../secp256k1-zkp/src/hash.h"

#if defined(__clang__) || defined(__GNUC__) || defined(__GNUG__)
    #pragma GCC diagnostic pop
#else
    #pragma warning (pop)
#endif

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

#ifdef USE_SCALAR_4X64
		typedef uint64_t uint;
#else // USE_SCALAR_4X64
		typedef uint32_t uint;
#endif // USE_SCALAR_4X64

		Native() {}
		template <typename T> Native(const T& t) { *this = t; }
		~Native() { SecureErase(*this); }

		Minus	operator - () const { return Minus(*this); }
		Plus	operator + (const Native& y) const { return Plus(*this, y); }
		Mul		operator * (const Native& y) const { return Mul(*this, y); }

		bool operator == (Zero_) const;
		bool operator == (const Native&) const;

		Native& operator = (Zero_);
		Native& operator = (uint32_t);
		Native& operator = (uint64_t);
		Native& operator = (Minus);
		Native& operator = (Plus);
		Native& operator = (Mul);
		Native& operator = (const Scalar&);
		Native& operator += (const Native& v) { return *this = *this + v; }
		Native& operator *= (const Native& v) { return *this = *this * v; }

		void SetSqr(const Native&);
		void Sqr();
		void SetInv(const Native&); // for 0 the result is also 0
		void Inv();

		bool Import(const Scalar&); // on overflow auto-normalizes and returns true
		void Export(Scalar&) const;
	};

	class Point::Native
		:private secp256k1_gej
	{
		typedef Op::Unary<Op::Minus, Native>				Minus;
		typedef Op::Unary<Op::Double, Native>				Double;
		typedef Op::Binary<Op::Plus, Native, Native>		Plus;
		typedef Op::Binary<Op::Mul, Native, Scalar::Native>	Mul;

		bool ImportInternal(const Point&);
	public:
		secp256k1_gej& get_Raw() { return *this; } // use with care

		Native() {}
		template <typename T> Native(const T& t) { *this = t; }
		~Native() { SecureErase(*this); }

		Minus	operator - () const { return Minus(*this); }
		Plus	operator + (const Native& y) const { return Plus(*this, y); }
		Mul		operator * (const Scalar::Native& y) const { return Mul(*this, y); }
		Double	operator * (Two_) const { return Double(*this); }

		bool operator == (Zero_) const;

		Native& operator = (Zero_);
		Native& operator = (Minus);
		Native& operator = (Plus);
		Native& operator = (Double);
		Native& operator = (const Point&);
		Native& operator += (const Native& v) { return *this = *this + v; }

		// non-secure implementation, suitable for casual use (such as signature verification), otherwise should use generators.
		// Optimized for small scalars
		Native& operator = (Mul);
		Native& operator += (Mul);

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
		void SetMul(Point::Native& res, bool bSet, const secp256k1_ge_storage* pPts, const Scalar::Native::uint* p, int nWords);

		template <uint32_t nBits_>
		class Simple
			:public Base<nBits_>
		{
			template <typename T>
			struct Mul
			{
				const Simple& me;
				const T& k;
				Mul(const Simple& me_, const T& k_) :me(me_) ,k(k_) {}

				void Assign(Point::Native& res, bool bSet) const
				{
					const int nWordBits = sizeof(Scalar::Native::uint) << 3;
					static_assert(!(nBits_ % nWordBits), "generator size should be multiple of native words");
					const int nWords = nBits_ / nWordBits;

					const int nWordsSrc = (sizeof(T) + sizeof(Scalar::Native::uint) - 1) / sizeof(Scalar::Native::uint);

					static_assert(nWordsSrc <= nWords, "generator too short");

					Scalar::Native::uint p[nWords];
					for (int i = 0; i < nWordsSrc; i++)
						p[i] = (Scalar::Native::uint) (k >> (i * nWordBits));

					for (int i = nWordsSrc; i < nWords; i++)
						p[i] = 0;

					Generator::SetMul(res, bSet, me.m_pPts, p, nWords);

					SecureErase(p, sizeof(Scalar::Native::uint) * nWordsSrc);
				}
			};

		public:
			Simple(const char* szSeed)
			{
				GeneratePts(szSeed, Base<nBits_>::m_pPts, Base<nBits_>::nLevels);
			}

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
				const TScalar& k;
				Mul(const Obscured& me_, const TScalar& k_) :me(me_) ,k(k_) {}

				void Assign(Point::Native& res, bool bSet) const;
			};

			void AssignInternal(Point::Native& res, bool bSet, Scalar::Native& kTmp, const Scalar::Native&) const;

		public:
			Obscured(const char* szSeed);

			template <typename TScalar>
			Mul<TScalar> operator * (const TScalar& k) const { return Mul<TScalar>(*this, k); }
		};

	} // namespace Generator

	struct Signature::MultiSig
	{
		Scalar::Native	m_Nonce;	// specific signer
		Point::Native	m_NoncePub;	// sum of all co-signers

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
