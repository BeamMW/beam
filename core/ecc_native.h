// Copyright 2018 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once
#include "ecc.h"
#include <assert.h>

#define USE_BASIC_CONFIG

#if defined(__clang__) || defined(__GNUC__) || defined(__GNUG__)
#	pragma GCC diagnostic push
#	pragma GCC diagnostic ignored "-Wunused-function"
#else
#	pragma warning (push, 0) // suppress warnings from secp256k1
#	pragma warning (disable: 4706) // assignment within conditional expression
#endif

#include "secp256k1-zkp/src/basic-config.h"
#include "secp256k1-zkp/include/secp256k1.h"
#include "secp256k1-zkp/src/scalar.h"
#include "secp256k1-zkp/src/group.h"
#include "secp256k1-zkp/src/hash.h"

#if defined(__clang__) || defined(__GNUC__) || defined(__GNUG__)
#	pragma GCC diagnostic pop
#else
#	pragma warning (default: 4706)
#	pragma warning (pop)
#endif

namespace ECC
{
	// cmov - conditional mov. Constant memory access and runtime.
	template <typename T>
	void data_cmov_as(T* pDst, const T* pSrc, int nWords, int flag);

	template <typename T>
	inline void object_cmov(T& dst, const T& src, int flag)
	{
		typedef uint32_t TOrd;
		static_assert(sizeof(T) % sizeof(TOrd) == 0, "");
		data_cmov_as<TOrd>((TOrd*)&dst, (TOrd*)&src, sizeof(T) / sizeof(TOrd), flag);
	}


	class Scalar::Native
		:private secp256k1_scalar
	{
		typedef Op::Unary<Op::Minus, Native>			Minus;
		typedef Op::Binary<Op::Plus, Native, Native>	Plus;
		typedef Op::Binary<Op::Mul, Native, Native>		Mul;
	public:

		const secp256k1_scalar& get() const { return *this; }
		secp256k1_scalar& get_Raw() { return *this; } // use with care

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

		bool ImportNnz(const Scalar&); // returns true if succeeded: i.e. must not overflow & non-zero. Constant time guaranteed.
		void GenRandomNnz();

		void GenerateNonceNnz(const uintBig& sk, const uintBig& msg, const uintBig* pMsg2, uint32_t nAttempt = 0);
		void GenerateNonceNnz(const Scalar::Native& sk, const uintBig& msg, const uintBig* pMsg2, uint32_t nAttempt = 0);
	};

	class Point::Native
		:private secp256k1_gej
	{
		typedef Op::Unary<Op::Minus, Native>				Minus;
		typedef Op::Unary<Op::Double, Native>				Double;
		typedef Op::Binary<Op::Plus, Native, Native>		Plus;
		typedef Op::Binary<Op::Mul, Native, Scalar::Native>	Mul;

		Native(const Point&);
	public:
		secp256k1_gej& get_Raw() { return *this; } // use with care

		Native();
		template <typename T> Native(const T& t) { *this = t; }
		~Native() { SecureErase(*this); }

		Minus	operator - () const { return Minus(*this); }
		Plus	operator + (const Native& y_) const { return Plus(*this, y_); }
		Mul		operator * (const Scalar::Native& y_) const { return Mul(*this, y_); }
		Double	operator * (Two_) const { return Double(*this); }

		bool operator == (Zero_) const;

		Native& operator = (Zero_);
		Native& operator = (Minus);
		Native& operator = (Plus);
		Native& operator = (Double);
		Native& operator += (const Native& v) { return *this = *this + v; }

		Native& operator = (Mul);
		Native& operator += (Mul);

		template <class Setter> Native& operator = (const Setter& v) { v.Assign(*this, true); return *this; }
		template <class Setter> Native& operator += (const Setter& v) { v.Assign(*this, false); return *this; }

		bool ImportNnz(const Point&); // won't accept zero point, doesn't zero itself in case of failure
		bool Import(const Point&);
		bool Export(Point&) const; // if the point is zero - returns false and zeroes the result

		static void ExportEx(Point&, const secp256k1_ge&);
	};

#ifdef NDEBUG
#	define ECC_COMPACT_GEN // init time is insignificant in release build. ~1sec in debug.
#endif // NDEBUG

#ifdef ECC_COMPACT_GEN

	// Generator tables are stored in compact structs (x,y in canonical form). Memory footprint: ~1.3MB. Slightly faster (probably due to better cache)
	// Disadvantage: slower initialization, because needs "normalizing". Insignificant in release build, ~1sec in debug.

	typedef secp256k1_ge_storage CompactPoint;

#else // ECC_COMPACT_GEN

	// Generator tables are stored in "jacobian" form. Memory footprint ~2.6. Slightly slower (probably due to increased mem)
	// Initialization is fast
	//
	// Currently used in debug to speed-up initialization.

	typedef secp256k1_gej CompactPoint;

#endif // ECC_COMPACT_GEN

	struct MultiMac
	{
		struct FastAux {
			unsigned int m_nNextItem;
			unsigned int m_nOdd;

			void Schedule(const Scalar::Native& k, unsigned int iBitsRemaining, unsigned int nMaxOdd, unsigned int* pTbl, unsigned int iThisEntry);
		};

		struct Casual
		{
			struct Secure {
				// In secure mode: all the values are precalculated from the beginning, with the "nums" added (for futher obscuring)
				static const int nBits = 4;
				static const int nCount = 1 << nBits;
			};

			struct Fast
			{
				// In fast mode: x1 is assigned from the beginning, then on-demand calculated x2 and then only odd multiples.
				static const int nMaxOdd = (1 << 5) - 1; // 31
				static const int nCount = (nMaxOdd >> 1) + 2; // we need a single even: x2
			};


			Point::Native m_pPt[(Secure::nCount > Fast::nCount) ? Secure::nCount : Fast::nCount];

			Scalar::Native m_K;

			// used in fast mode
			unsigned int m_nPrepared;
			FastAux m_Aux;

			void Init(const Point::Native&);
			void Init(const Point::Native&, const Scalar::Native&);
		};

		struct Prepared
		{
			struct Fast {
				static const int nMaxOdd = 0xff; // 255
				// Currently we precalculate odd power up to 255.
				// For 511 precalculated odds nearly x2 global data increase (2.5MB instead of 1.3MB). For single bulletproof verification the performance gain is ~8%.
				// For 127 precalculated odds single bulletproof verfication is slower by about 6%.
				// The difference deminishes for batch verifications (performance is dominated by non-prepared point multiplication).
				static const int nCount = (nMaxOdd >> 1) + 1;
				CompactPoint m_pPt[nCount]; // odd powers
			} m_Fast;

			struct Secure {
				// A variant of Generator::Obscured. Much less space & init time. Slower for single multiplication, nearly equal in MultiMac.
				static const int nBits = 4;
				CompactPoint m_pPt[(1 << nBits)];
				CompactPoint m_Compensation;
				Scalar::Native m_Scalar;
			} m_Secure;

			void Initialize(Oracle&, Hash::Processor& hpRes);
			void Initialize(Point::Native&, Oracle&);
		};

		Casual* m_pCasual;
		const Prepared** m_ppPrepared;
		Scalar::Native* m_pKPrep;
		FastAux* m_pAuxPrepared;

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
			FastAux m_pAuxPrepared[nMaxPrepared];
		} m_Bufs;

		MultiMac_WithBufs()
		{
			m_pCasual		= m_Bufs.m_pCasual;
			m_ppPrepared	= m_Bufs.m_ppPrepared;
			m_pKPrep		= m_Bufs.m_pKPrep;
			m_pAuxPrepared	= m_Bufs.m_pAuxPrepared;
		}

		void Calculate(Point::Native& res)
		{
			assert(m_Casual <= nMaxCasual);
			assert(m_Prepared <= nMaxPrepared);
			MultiMac::Calculate(res);
		}
	};

	struct ScalarGenerator
	{
		// needed to quickly calculate power of a predefined scalar.
		// Used to quickly sample a scalar and its inverse, by actually sampling the order.
		// Implementation is *NOT* secure (constant time/memory access). Should be used with challenges, but not nonces!

		static const uint32_t nBitsPerLevel = 8;
		static const uint32_t nLevels = nBits / nBitsPerLevel;
		static_assert(nLevels * nBitsPerLevel == nBits, "");

		struct PerLevel {
			Scalar::Native m_pVal[(1 << nBitsPerLevel) - 1];
		};

		PerLevel m_pLevel[nLevels];

		void Initialize(const Scalar::Native&);
		void Calculate(Scalar::Native& trg, const Scalar& pwr) const;
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

		void GeneratePts(const Point::Native&, Oracle&, CompactPoint* pPts, uint32_t nLevels);
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
			void Initialize(const Point::Native& p, Oracle& oracle)
			{
				GeneratePts(p, oracle, Base<nBits_>::m_pPts, Base<nBits_>::nLevels);
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
			void Initialize(const Point::Native&, Oracle&);

			template <typename TScalar>
			Mul<TScalar> operator * (const TScalar& k) const { return Mul<TScalar>(*this, k); }
		};

	} // namespace Generator

	struct Signature::MultiSig
	{
		Scalar::Native	m_Nonce;	// specific signer
		Point::Native	m_NoncePub;	// sum of all co-signers

		//	NOTE: Schnorr's multisig should be used carefully. If done naively it has the following potential weaknesses:
		//	1. Key cancellation. (The attacker may exclude you and actually create a signature for its private key).
		//		This isn't a problem for our case, but should be taken into consideration if used in other schemes.
		// 2. Private Key leak. If the same message signed with the same key but co-signers use different nonces (altering the challenge) - there's a potential for key leak. 
		//		This is indeed the case if the nonce is generated from the secret key and the message only.
		//		In order to prevent this the signer **MUST**  use an additional source of randomness, and make sure it's different for every ritual.

		void SignPartial(Scalar::Native& k, const Hash::Value& msg, const Scalar::Native& sk) const;
	};


	class Hash::Processor
		:private secp256k1_sha256_t
	{
		bool m_bInitialized;

		void Write(const void*, uint32_t);
		void Write(bool);
		void Write(uint8_t);
		void Write(const Scalar&);
		void Write(const Scalar::Native&);
		void Write(const Point&);
		void Write(const Point::Native&);
		void Write(const beam::Blob&);
		template <uint32_t nBytes_>
		void Write(const beam::uintBig_t<nBytes_>& x) { Write(x.m_pData, x.nBytes); }
		template <uint32_t n>
		void Write(const char(&sz)[n]) { Write(sz, n); }
		void Write(const std::string& str) { Write(str.c_str(), static_cast<uint32_t>(str.size() + 1)); }

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
		~Processor();

		void Reset();

		template <typename T>
		Processor& operator << (const T& t) { Write(t); return *this; }

		void operator >> (Value& hv) { Finalize(hv); }
	};

	class Hash::Mac
		:private secp256k1_hmac_sha256_t
	{
		void Finalize(Value&);
	public:
		Mac() {}
		Mac(const void* pSecret, uint32_t nSecret) { Reset(pSecret, nSecret); }
		~Mac() { SecureErase(*this); }

		void Reset(const void* pSecret, uint32_t nSecret);
		void Write(const void*, uint32_t);

		void operator >> (Value& hv) { Finalize(hv); }
	};

	class HKdf
		:public Key::IKdf
	{
		friend class HKdfPub;
		HKdf(const HKdf&) = delete;

		NoLeak<uintBig> m_Secret;
		Scalar::Native m_kCoFactor;
	public:
		HKdf();
		virtual ~HKdf();
		// IPKdf
		virtual void DerivePKey(Point::Native&, const Hash::Value&) override;
		virtual void DerivePKey(Scalar::Native&, const Hash::Value&) override;
		// IKdf
		virtual void DeriveKey(Scalar::Native&, const Hash::Value&) override;

#pragma pack (push, 1)
		struct Packed
		{
			uintBig m_Secret;
			Scalar m_kCoFactor;
		};
		static_assert(sizeof(Packed) == uintBig::nBytes * 2, "");
#pragma pack (pop)

		void Export(Packed&) const;
		bool Import(const Packed&);

		void Generate(const Hash::Value&);
		static void Create(Ptr&, const Hash::Value&);

		void GenerateChild(Key::IKdf&, Key::Index iKdf);
		static void CreateChild(Ptr&, Key::IKdf&, Key::Index iKdf);
	};

	class HKdfPub
		:public Key::IPKdf
	{
		HKdfPub(const HKdfPub&) = delete;

		NoLeak<uintBig> m_Secret;
		Point::Native m_Pk;

	public:
		HKdfPub();
		virtual ~HKdfPub();

		// IPKdf
		virtual void DerivePKey(Point::Native&, const Hash::Value&) override;
		virtual void DerivePKey(Scalar::Native&, const Hash::Value&) override;

#pragma pack (push, 1)
		struct Packed
		{
			uintBig m_Secret;
			Point m_Pk;
		};
		static_assert(sizeof(Packed) == uintBig::nBytes * 2 + 1, "");
#pragma pack (pop)

		void Export(Packed&) const;
		bool Import(const Packed&);

		void GenerateFrom(const HKdf&);
	};

	struct Context
	{
		static const Context& get();

		Generator::Obscured						G;
		Generator::Obscured						H_Big;
		Generator::Simple<sizeof(Amount) << 3>	H;
		Generator::Obscured						J; // for switch/ElGamal commitment

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

	struct InnerProduct::BatchContext
		:public MultiMac
	{
		static thread_local BatchContext* s_pInstance;

		struct Scope
		{
			BatchContext* m_pPrev;

			Scope(BatchContext& bc) {
				m_pPrev = s_pInstance;
				s_pInstance = &bc;
			}
			~Scope() {
				s_pInstance = m_pPrev;
			}
		};

		static const uint32_t s_CasualCountPerProof = nCycles * 2 + 5; // L[], R[], A, S, T1, T2, Commitment

		static const uint32_t s_CountPrepared = InnerProduct::nDim * 2 + 4; // [2][InnerProduct::nDim], m_GenDot_, m_Aux2_, G_, H_

		static const uint32_t s_Idx_GenDot	= InnerProduct::nDim * 2;
		static const uint32_t s_Idx_Aux2	= InnerProduct::nDim * 2 + 1;
		static const uint32_t s_Idx_G		= InnerProduct::nDim * 2 + 2;
		static const uint32_t s_Idx_H		= InnerProduct::nDim * 2 + 3;

		struct Bufs {
			const Prepared* m_ppPrepared[s_CountPrepared];
			Scalar::Native m_pKPrep[s_CountPrepared];
			FastAux m_pAuxPrepared[s_CountPrepared];
		} m_Bufs;


		void Reset();
		void Calculate(Point::Native& res);

		const uint32_t m_CasualTotal;
		bool m_bEnableBatch;
		bool m_bDirty;
		Scalar::Native m_Multiplier; // must be initialized in a non-trivial way

		bool AddCasual(const Point& p, const Scalar::Native& k);
		void AddCasual(const Point::Native& pt, const Scalar::Native& k);
		void AddPrepared(uint32_t i, const Scalar::Native& k);

		bool EquationBegin(uint32_t nCasualNeeded);
		bool EquationEnd();

		bool Flush();

	protected:
		BatchContext(uint32_t nCasualTotal);
	};

	template <uint32_t nBatchSize>
	struct InnerProduct::BatchContextEx
		:public BatchContext
	{
		uint64_t m_pBuf[(sizeof(MultiMac::Casual) * s_CasualCountPerProof * nBatchSize + sizeof(uint64_t) - 1) / sizeof(uint64_t)];

		BatchContextEx()
			:BatchContext(nBatchSize * s_CasualCountPerProof)
		{
			m_pCasual = (MultiMac::Casual*) m_pBuf;
		}
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
		void operator >> (Hash::Value&);
	};
}
