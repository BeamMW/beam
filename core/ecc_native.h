#pragma once
#include "ecc.h"
#include <assert.h>

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

		Native();
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

		void GenerateNonce(const uintBig& sk, const uintBig& msg, const uintBig* pMsg2, uint32_t nAttempt = 0);
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

		Native();
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

#ifdef NDEBUG
#	define ECC_COMPACT_GEN // init time is insignificant in release build. ~1sec in debug.
#endif // NDEBUG

#ifdef ECC_COMPACT_GEN

	// Generator tables are stored in compact structs (x,y in canonical form). Memory footprint: ~2.5MB. Slightly faster (probably due to better cache)
	// Disadvantage: slow initialization, because needs "normalizing". For all the generators takes ~1sec in release, 4-5 seconds in debug.
	//
	// Currently *disabled* to prevent app startup lag.

	typedef secp256k1_ge_storage CompactPoint;

#else // ECC_COMPACT_GEN

	// Generator tables are stored in "jacobian" form. Memory footprint ~4.7MB. Slightly slower (probably due to increased mem)
	// Initialization is fast
	//
	// Currently used.

	typedef secp256k1_gej CompactPoint;

#endif // ECC_COMPACT_GEN

	struct MultiMac
	{
		struct Casual
		{
			static const int nBits = 4;

			Point::Native m_pPt[(1 << nBits)];
			Scalar::Native m_K;
			int m_nPrepared;

			void Init(const Point::Native&);
			void Init(const Point::Native&, const Scalar::Native&);
		};

		struct Prepared
		{
			struct Fast {
				static const int nBits = 8;
				CompactPoint m_pPt[(1 << nBits) - 1]; // skip zero
			} m_Fast;

			struct Secure {
				// A variant of Generator::Obscured. Much less space & init time. Slower for single multiplication, nearly equal in MultiMac.
				static const int nBits = 4;
				CompactPoint m_pPt[(1 << nBits)];
				CompactPoint m_Compensation;
				Scalar::Native m_Scalar;
			} m_Secure;

			void Initialize(const char* szSeed, Hash::Processor& hp);
			void Initialize(Point::Native&, Hash::Processor&);
		};

		Casual* m_pCasual;
		const Prepared** m_ppPrepared;
		Scalar::Native* m_pKPrep;

		int m_Casual;
		int m_Prepared;

		MultiMac() { Reset(); }

		void Reset();
		void Calculate(Point::Native&) const;
	};

	template <int nMaxCasual, int nMaxPrepared>
	struct MultiMac_WithBufs
		:public MultiMac
	{
		struct Bufs {
			Casual m_pCasual[nMaxCasual];
			const Prepared* m_ppPrepared[nMaxPrepared];
			Scalar::Native m_pKPrep[nMaxPrepared];
		} m_Bufs;

		MultiMac_WithBufs()
		{
			m_pCasual		= m_Bufs.m_pCasual;
			m_ppPrepared	= m_Bufs.m_ppPrepared;
			m_pKPrep		= m_Bufs.m_pKPrep;
		}

		void Calculate(Point::Native& res)
		{
			assert(m_Casual <= nMaxCasual);
			assert(m_Prepared <= nMaxPrepared);
			MultiMac::Calculate(res);
		}
	};

	namespace Generator
	{
		void ToPt(Point::Native&, secp256k1_ge& ge, const CompactPoint&, bool bSet);

		static const uint32_t nBitsPerLevel = 4;
		static const uint32_t nPointsPerLevel = 1 << nBitsPerLevel; // 16

		template <uint32_t nBits_>
		class Base
		{
		protected:
			static const uint32_t nLevels = nBits_ / nBitsPerLevel;
			static_assert(nLevels * nBitsPerLevel == nBits_, "");

			CompactPoint m_pPts[nLevels * nPointsPerLevel];
		};

		void GeneratePts(const Point::Native&, Hash::Processor&, CompactPoint* pPts, uint32_t nLevels);
		void SetMul(Point::Native& res, bool bSet, const CompactPoint* pPts, const Scalar::Native::uint* p, int nWords);

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
			void Initialize(const Point::Native& p, Hash::Processor& hp)
			{
				GeneratePts(p, hp, Base<nBits_>::m_pPts, Base<nBits_>::nLevels);
			}

			template <typename TScalar>
			Mul<TScalar> operator * (const TScalar& k) const { return Mul<TScalar>(*this, k); }
		};

		class Obscured
			:public Base<nBits>
		{
			CompactPoint m_AddPt;
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
			void Initialize(const Point::Native&, Hash::Processor& hp);

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
		void Write(const Scalar::Native&);
		void Write(const Point&);
		void Write(const Point::Native&);

		template <typename T>
		void Write(T v)
		{
			// Must be independent of the endian-ness
			// Must prevent ambiguities (different inputs should be properly distinguished)
			// Make it also independent of the actual type width, so that size_t (and friends) will be treated the same on all the platforms
			static_assert(T(-1) > 0, "must be unsigned");

			for (; v >= 0x80; v >>= 7)
				Write(uint8_t(uint8_t(v) | 0x80));

			Write(uint8_t(v));
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

	class Hash::Mac
		:private secp256k1_hmac_sha256_t
	{
		void Finalize(Value&);
	public:
		Mac(const void* pSecret, uint32_t nSecret);
		~Mac() { SecureErase(*this); }

		void Write(const void*, uint32_t);

		template <typename T>
		Mac& operator << (const T& t)
		{
			static_assert(sizeof(Processor) == sizeof(inner));
			((Processor&)inner) << t;
			return *this;
		}

		void operator >> (Value& hv) { Finalize(hv); }
	};

	struct Context
	{
		static const Context& get();

		Generator::Obscured						G;
		Generator::Obscured						H_Big;
		Generator::Simple<sizeof(Amount) << 3>	H;

		struct IppCalculator
		{
			// generators used for inner product proof
			MultiMac::Prepared m_pGen_[2][InnerProduct::nDim];
			CompactPoint m_pGet1_Minus[InnerProduct::nDim];
			MultiMac::Prepared m_GenDot_; // seems that it's not necessary, can use G instead
			MultiMac::Prepared m_Aux2_;
			MultiMac::Prepared G_;
			MultiMac::Prepared H_;

		} m_Ipp;

		struct Casual
		{
			CompactPoint m_Nums;
			CompactPoint m_Compensation;

		} m_Casual;

		Hash::Value m_hvChecksum; // all the generators and signature version. In case we change seed strings or formula

	private:
		Context() {}
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
		void operator >> (Hash::Value& hv) { m_hp >> hv; }
	};
}
