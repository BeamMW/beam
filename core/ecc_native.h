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
        typedef Op::Binary<Op::Minus, Native, Native>	Minus2;
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
        Minus2	operator - (const Native& y) const { return Minus2(*this, y); }
		Mul		operator * (const Native& y) const { return Mul(*this, y); }

		bool operator == (Zero_) const;
        bool operator != (Zero_) const;
		bool operator == (const Native&) const;
        bool operator != (const Native&) const;

		Native& operator = (Zero_);
		Native& operator = (uint32_t);
		Native& operator = (uint64_t);
		Native& operator = (Minus);
		Native& operator = (Plus);
        Native& operator = (Minus2);
		Native& operator = (Mul);
		Native& operator = (const Scalar&);
		Native& operator += (const Native& v) { return *this = *this + v; }
        Native& operator -= (const Native& v) { return *this = *this - v; }
		Native& operator *= (const Native& v) { return *this = *this * v; }

		void SetSqr(const Native&);
		void Sqr();
		void SetInv(const Native&); // for 0 the result is also 0
		void Inv();

		bool Import(const Scalar&); // on overflow auto-normalizes and returns true
		void Export(Scalar&) const;

		bool ImportNnz(const Scalar&); // returns true if succeeded: i.e. must not overflow & non-zero. Constant time guaranteed.
		void GenRandomNnz();
	};

    std::ostream& operator << (std::ostream&, const Scalar::Native&);

	struct Point::Storage
	{
		uintBig m_X;
		uintBig m_Y;

		void FromNnz(secp256k1_ge&);
	};

	struct Point::Compact
		:public secp256k1_ge_storage
	{
		struct Converter;
		void Assign(secp256k1_ge&) const;
		void Assign(Point::Native&, bool bSet) const;
	};

	class Point::Native
		:private secp256k1_gej
	{
		typedef Op::Unary<Op::Minus, Native>				Minus;
		typedef Op::Unary<Op::Double, Native>				Double;
		typedef Op::Binary<Op::Plus, Native, Native>		Plus;
        typedef Op::Binary<Op::Minus, Native, Native>		Minus2;
		typedef Op::Binary<Op::Mul, Native, Scalar::Native>	Mul;

		Native(const Point&);

		void ExportNnz(secp256k1_ge&) const;

	public:
		secp256k1_gej& get_Raw() { return *this; } // use with care

		Native();
		template <typename T> Native(const T& t) { *this = t; }
		~Native() { SecureErase(*this); }

		Minus	operator - () const { return Minus(*this); }
		Plus	operator + (const Native& y_) const { return Plus(*this, y_); }
        Minus2	operator - (const Native& y_) const { return Minus2(*this, y_); }
		Mul		operator * (const Scalar::Native& y_) const { return Mul(*this, y_); }
		Double	operator * (Two_) const { return Double(*this); }

		bool operator == (Zero_) const;
        bool operator != (Zero_) const;

		bool operator == (const Native&) const;
		bool operator == (const Point&) const;

		Native& operator = (Zero_);
		Native& operator = (Minus);
		Native& operator = (Plus);
        Native& operator = (Minus2);
		Native& operator = (Double);
		Native& operator += (const Native& v) { return *this = *this + v; }
        Native& operator -= (const Native& v) { return *this = *this - v; }

		Native& operator = (Mul);
		Native& operator += (Mul);

		template <class Setter> Native& operator = (const Setter& v) { v.Assign(*this, true); return *this; }
		template <class Setter> Native& operator += (const Setter& v) { v.Assign(*this, false); return *this; }

		bool ImportNnz(const Point&, Storage* = nullptr); // won't accept zero point, doesn't zero itself in case of failure
		bool Import(const Point&, Storage* = nullptr);
		bool Export(Point&) const; // if the point is zero - returns false and zeroes the result

		static void ExportEx(Point&, const secp256k1_ge&);

		bool Import(const Storage&, bool bVerify);
		void Export(Storage&) const;

		struct BatchNormalizer
		{
			struct Element
			{
				Point::Native* m_pPoint;
				secp256k1_fe* m_pFe; // temp
			};

			virtual void Reset() = 0;
			virtual bool MoveNext(Element&) = 0;
			virtual bool MovePrev(Element&) = 0;

			void Normalize();
			void ToCommonDenominator(secp256k1_fe& zDenom);

			static void get_As(secp256k1_ge&, const Point::Native& ptNormalized);
			static void get_As(secp256k1_ge_storage&, const Point::Native& ptNormalized);

		private:
			void NormalizeInternal(secp256k1_fe&, bool bNormalize);
		};

		struct BatchNormalizer_Arr
			:public BatchNormalizer
		{
			uint32_t m_iIdx;
			uint32_t m_Size;
			Point::Native* m_pPts;
			secp256k1_fe* m_pFes;

			void get_At(Element& el, uint32_t iIdx);

			virtual void Reset() override;
			virtual bool MoveNext(Element& el) override;
			virtual bool MovePrev(Element& el) override;
		};

		template <uint32_t nSize>
		struct BatchNormalizer_Arr_T;
	};

	template <uint32_t nSize>
	struct Point::Native::BatchNormalizer_Arr_T
		:public BatchNormalizer_Arr
	{
		Point::Native m_pPtsBuf[nSize];
		secp256k1_fe m_pFesBuf[nSize];

		BatchNormalizer_Arr_T()
		{
			m_pPts = m_pPtsBuf;
			m_pFes = m_pFesBuf;
			m_Size = nSize;
		}
	};

    std::ostream& operator << (std::ostream&, const Point::Native&);

	struct Point::Compact::Converter
	{
		static const uint32_t N = 0x100;
		Native::BatchNormalizer_Arr_T<N> m_Batch;

		Compact* m_ppC[N];

		Converter();
		void Flush();

		void set_Deferred(Compact& trg, Native& src);
	};

	secp256k1_pubkey ConvertPointToPubkey(const Point& point);
	std::vector<uint8_t> SerializePubkey(const secp256k1_pubkey& pubkey);

	template <typename T, size_t nCount = 1>
	struct AlignedBuf
	{
		alignas(64) char m_p[sizeof(T) * nCount];
		T& get() const { return *reinterpret_cast<T*>((char*) m_p); }
	};

	struct MultiMac
	{
		struct WnafBase
		{
			// window NAF (wNAF) representation

			struct Entry
			{
				uint16_t m_Odd;
				uint16_t m_iBit;

				static const uint16_t s_Negative = uint16_t(1) << 15;
			};

			struct Link
			{
				unsigned int m_iElement;
				unsigned int m_iEntry; // 1-based

			} m_Next;

			struct Shared
			{
				Link m_pTable[ECC::nBits + 1];

				void Reset();

				unsigned int Add(Entry* pTrg, const Scalar::Native& k, unsigned int nWndBits, WnafBase&, unsigned int iElement);

				unsigned int Fetch(unsigned int iBit, WnafBase&, const Entry*, bool& bNeg);
			};

		protected:


			struct Context;
		};

		template <unsigned int nWndBits>
		struct Wnaf_T
			:public WnafBase
		{
			static const unsigned int nMaxEntries = ECC::nBits / (nWndBits + 1) + 1;

			Entry m_pVals[nMaxEntries];

			unsigned int Init(Shared& s, const Scalar::Native& k, unsigned int iElement)
			{
				return s.Add(m_pVals, k, nWndBits, *this, iElement);
			}

			unsigned int Fetch(Shared& s, unsigned int iBit, bool& bNeg)
			{
				return s.Fetch(iBit, *this, m_pVals, bNeg);
			}
		};

		struct Casual
		{
			struct Secure
			{
				// In secure mode: all the values are precalculated from the beginning, with the "nums" added (for futher obscuring)
				static const int nBits = 4;
				static const int nCount = 1 << nBits;

				Point::Native m_pPt[Secure::nCount];
			};

			struct Fast
			{
				// In fast mode: x1 is assigned from the beginning, then on-demand calculated x2 and then only odd multiples.
				static const int nBits = 4;
				static const int nMaxOdd = (1 << nBits) - 1; // 15
				static const int nCount = (nMaxOdd >> 1) + 1;

				Point::Native m_pPt[Fast::nCount];
				secp256k1_fe m_pFe[Fast::nCount];
				unsigned int m_nNeeded;

				typedef Wnaf_T<nBits> Wnaf;
				Wnaf m_Wnaf;
			};

			union
			{
				AlignedBuf<Secure> S;
				AlignedBuf<Fast> F;
			} U;

			void Init(const Point::Native&);
		};

		struct Prepared
		{
			struct Fast {
				static const int nBits = 8;
				static const int nMaxOdd = (1 << nBits) - 1; // 255
				// Currently we precalculate odd power up to 255.
				// For 511 precalculated odds nearly x2 global data increase (2.5MB instead of 1.3MB). For single bulletproof verification the performance gain is ~8%.
				// For 127 precalculated odds single bulletproof verfication is slower by about 6%.
				// The difference deminishes for batch verifications (performance is dominated by non-prepared point multiplication).
				static const int nCount = (nMaxOdd >> 1) + 1;
				Point::Compact m_pPt[nCount]; // odd powers

				typedef Wnaf_T<nBits> Wnaf;

			} m_Fast;

			struct Secure {
				// A variant of Generator::Obscured. Much less space & init time. Slower for single multiplication, nearly equal in MultiMac.
				static const int nBits = 4;
				Point::Compact m_pPt[(1 << nBits)];
				Point::Compact m_Compensation;
				Scalar::Native m_Scalar;
			} m_Secure;

			void Initialize(Oracle&, Hash::Processor& hpRes, Point::Compact::Converter&);
			void Initialize(Point::Native&, Oracle&, Point::Compact::Converter&);

			void Assign(Point::Native&, bool bSet) const;
		};

		Casual* m_pCasual;
		const Prepared** m_ppPrepared;
		Scalar::Native* m_pKPrep;
		Scalar::Native* m_pKCasual;
		Prepared::Fast::Wnaf* m_pWnafPrepared;

		int m_Casual;
		int m_Prepared;

		struct Reuse {
			enum Enum {
				None,
				Generate,
				UseGenerated
			};
		};

		Reuse::Enum m_ReuseFlag;

		MultiMac() { Reset(); }

		void Reset();
		void Calculate(Point::Native&) const;

	private:

		struct Normalizer;
	};

	template <int nMaxCasual, int nMaxPrepared>
	struct MultiMac_WithBufs
		:public MultiMac
	{
		struct Bufs {
			Casual m_pCasual[nMaxCasual];
			const Prepared* m_ppPrepared[nMaxPrepared];
			Scalar::Native m_pKPrep[nMaxPrepared];
			Scalar::Native m_pKCasual[nMaxCasual];
			Prepared::Fast::Wnaf m_pWnafPrepared[nMaxPrepared];
		} m_Bufs;

		MultiMac_WithBufs()
		{
			m_pCasual		= m_Bufs.m_pCasual;
			m_ppPrepared	= m_Bufs.m_ppPrepared;
			m_pKPrep		= m_Bufs.m_pKPrep;
			m_pKCasual		= m_Bufs.m_pKCasual;
			m_pWnafPrepared	= m_Bufs.m_pWnafPrepared;
		}

		void Calculate(Point::Native& res)
		{
			assert(m_Casual <= nMaxCasual);
			assert(m_Prepared <= nMaxPrepared);
			MultiMac::Calculate(res);
		}
	};

	struct MultiMac_Dyn
		:public MultiMac
	{
		std::vector<Casual> m_vCasual;
		std::vector<Scalar::Native> m_vKCasual;
		std::vector<const Prepared*> m_vpPrepared;
		std::vector<Scalar::Native> m_vKPrepared;
		std::vector<Prepared::Fast::Wnaf> m_vWnafPrepared;

		void Prepare(uint32_t nMaxCasual, uint32_t nMaxPrepared);
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
		static const uint32_t nBitsPerLevel = 4;
		static const uint32_t nPointsPerLevel = 1 << nBitsPerLevel; // 16

		template <uint32_t nBits_>
		class Base
		{
		protected:
			static const uint32_t nLevels = nBits_ / nBitsPerLevel;
			static_assert(nLevels * nBitsPerLevel == nBits_, "");

			Point::Compact m_pPts[nLevels * nPointsPerLevel];
		};

		void GeneratePts(const Point::Native&, Oracle&, Point::Compact* pPts, uint32_t nLevels, Point::Compact::Converter&);
		void SetMul(Point::Native& res, bool bSet, const Point::Compact* pPts, const Scalar::Native::uint* p, int nWords);

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
			void Initialize(const Point::Native& p, Oracle& oracle, Point::Compact::Converter& cpc)
			{
				GeneratePts(p, oracle, Base<nBits_>::m_pPts, Base<nBits_>::nLevels, cpc);
			}

			template <typename TScalar>
			Mul<TScalar> operator * (const TScalar& k) const { return Mul<TScalar>(*this, k); }
		};

		class Obscured
			:public Base<nBits>
		{
			Point::Compact m_AddPt;
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
			void Initialize(const Point::Native&, Oracle&, Point::Compact::Converter&);

			template <typename TScalar>
			Mul<TScalar> operator * (const TScalar& k) const { return Mul<TScalar>(*this, k); }
		};

	} // namespace Generator

	class Hash::Processor
		:private secp256k1_sha256_t
	{
		bool m_bInitialized;

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
		void FinalizeTruncated(uint8_t* p, uint32_t nSize);

	public:
		Processor();
		~Processor();

		void Reset();

		template <typename T>
		Processor& operator << (const T& t) { Write(t); return *this; }

		template <typename T>
		Processor& Serialize(const T&);

		void operator >> (Value& hv) { Finalize(hv); }

		template <uint32_t nBytes>
		void operator >> (beam::uintBig_t<nBytes>& hv)
		{
			static_assert(nBytes < Value::nBytes);
			FinalizeTruncated(hv.m_pData, hv.nBytes);
		}

		void Write(const void*, uint32_t);
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

	class NonceGenerator
	{
		// RFC-5869
		Hash::Mac m_HMac;

		Hash::Value m_Prk;
		Hash::Value m_Okm;
		beam::uintBig_t<1> m_Counter; // wraps-around, it's fine
		bool m_bFirstTime;

		void Reset();
		void WriteIkm(const beam::Blob&);

	public:

		template <uint32_t nSalt>
		NonceGenerator(const char(&szSalt)[nSalt])
			:m_HMac(szSalt, nSalt)
		{
			Reset();
		}

		~NonceGenerator() { SecureErase(*this); }

		beam::Blob m_Context;

		template <uint32_t nContext>
		NonceGenerator& SetContext(const char(&szContext)[nContext]) {
			m_Context.p = szContext;
			m_Context.n = nContext;
			return *this;
		}

		template <typename T>
		NonceGenerator& operator << (const T& t) {
			WriteIkm(t);
			return *this;
		}

		const Hash::Value& get_Okm();
		NonceGenerator& operator >> (Hash::Value&);
		NonceGenerator& operator >> (Scalar::Native&);
	};

	class HKdf
		:public Key::IKdf
	{
		friend class HKdfPub;
		HKdf(const HKdf&) = delete;

		struct Generator
		{
			Generator();
			// according to rfc5869
			NoLeak<uintBig> m_Secret;
			void Generate(Scalar::Native&, const Hash::Value&) const;

		} m_Generator;

		Scalar::Native m_kCoFactor;
	public:
		HKdf();
		virtual ~HKdf();
		// IPKdf
		virtual void DerivePKey(Scalar::Native&, const Hash::Value&) override;
		virtual uint32_t ExportP(void*) const override;
		// IKdf
		virtual void DeriveKey(Scalar::Native&, const Hash::Value&) override;
		virtual uint32_t ExportS(void*) const override;

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

		void GenerateChildParallel(Key::IKdf&, const Hash::Value&); // generate a subkey compatible with the appropriate HKdfPub
	};

	class HKdfPub
		:public Key::IPKdf
	{
		HKdfPub(const HKdfPub&) = delete;

		HKdf::Generator m_Generator;
		Point::Native m_PkG;
		Point::Native m_PkJ;

	public:
		HKdfPub();
		virtual ~HKdfPub();

		// IPKdf
		virtual void DerivePKey(Scalar::Native&, const Hash::Value&) override;
		virtual void DerivePKeyG(Point::Native&, const Hash::Value&) override;
		virtual void DerivePKeyJ(Point::Native&, const Hash::Value&) override;
		virtual uint32_t ExportP(void*) const override;

#pragma pack (push, 1)
		struct Packed
		{
			uintBig m_Secret;
			Point m_PkG;
			Point m_PkJ;
		};
		static_assert(sizeof(Packed) == uintBig::nBytes * 3 + 2, "");
#pragma pack (pop)

		void Export(Packed&) const;
		bool Import(const Packed&);

		void GenerateFrom(const HKdf&);

		void GenerateChildParallel(Key::IPKdf&, const Hash::Value&); // generate a subkey compatible with the appropriate HKdfPub
	};

	struct SignatureBase::Config
	{
		uint32_t m_nG; // num of generators
		uint32_t m_nKeys; // num of keys

		struct Generator
		{
			const MultiMac::Prepared* m_pGenPrep;
			const ECC::Generator::Obscured* m_pGen;
			uint32_t m_nBatchIdx;
		};

		const Generator* m_pG;
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
			Point::Compact m_pGet1_Minus[InnerProduct::nDim];
			MultiMac::Prepared m_GenDot_; // seems that it's not necessary, can use G instead
			MultiMac::Prepared m_Aux2_;
			MultiMac::Prepared G_;
			MultiMac::Prepared H_;
			MultiMac::Prepared J_;

			// useful constants
			Scalar::Native m_2Inv;

		} m_Ipp;

		struct Casual
		{
			Point::Compact m_Nums;
			Point::Compact m_Compensation;

		} m_Casual;

		struct Sig
		{
			SignatureBase::Config::Generator m_GenG;
			SignatureBase::Config::Generator m_pGenGJ[2];
			SignatureBase::Config::Generator m_pGenGH[2];

			SignatureBase::Config m_CfgG1; // regular
			SignatureBase::Config m_CfgGJ1; // Generalized G+J
			SignatureBase::Config m_CfgG2; // G, 2 keys
			SignatureBase::Config m_CfgGH2; // Generalized G+H, 2 keys

		} m_Sig;

		Hash::Value m_hvChecksum; // all the generators and signature version. In case we change seed strings or formula

	private:
		Context() {}
	};

	// simple pseudo-random generator. For tests only
	struct PseudoRandomGenerator
	{
		uintBig m_hv;
		uint32_t m_Remaining;

		PseudoRandomGenerator();

		void Generate(void*, uint32_t nSize);

		static thread_local PseudoRandomGenerator* s_pOverride; // set this to override the 'true' (standard) random generator. For tests, where repearability is needed.

		struct Scope
		{
			PseudoRandomGenerator* m_pPrev;

			Scope(PseudoRandomGenerator* p)
			{
				m_pPrev = s_pOverride;
				s_pOverride = p;
			}

			~Scope()
			{
				s_pOverride = m_pPrev;
			}
		};
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

		static const uint32_t s_CountPrepared = InnerProduct::nDim * 2 + 5; // [2][InnerProduct::nDim], m_GenDot_, m_Aux2_, G_, H_, J_

		static const uint32_t s_Idx_GenDot	= InnerProduct::nDim * 2;
		static const uint32_t s_Idx_Aux2	= InnerProduct::nDim * 2 + 1;
		static const uint32_t s_Idx_G		= InnerProduct::nDim * 2 + 2;
		static const uint32_t s_Idx_H		= InnerProduct::nDim * 2 + 3;
		static const uint32_t s_Idx_J		= InnerProduct::nDim * 2 + 4;

		struct Bufs {
			const Prepared* m_ppPrepared[s_CountPrepared];
			Scalar::Native m_pKPrep[s_CountPrepared];
			Prepared::Fast::Wnaf m_pWnafPrepared[s_CountPrepared];
		} m_Bufs;


		void Calculate();

		const uint32_t m_CasualTotal;
		bool m_bDirty;
		Scalar::Native m_Multiplier; // must be initialized in a non-trivial way
		Point::Native m_Sum; // intermediate result, sum of Casuals

		bool AddCasual(const Point& p, const Scalar::Native& k, bool bPremultiplied = false);
		void AddCasual(const Point::Native& pt, const Scalar::Native& k, bool bPremultiplied = false);
		void AddPrepared(uint32_t i, const Scalar::Native& k);
		void AddPreparedM(uint32_t i, const Scalar::Native& k);

		void EquationBegin();

		void Reset();
		bool Flush();

	protected:
		BatchContext(uint32_t nCasualTotal);
	};

	template <uint32_t nBatchSize>
	struct InnerProduct::BatchContextEx
		:public BatchContext
	{
		static const uint32_t s_Count = s_CasualCountPerProof * nBatchSize;
		AlignedBuf<MultiMac::Casual, s_Count> m_Buf1;
		AlignedBuf<Scalar::Native, s_Count> m_Buf2;

		BatchContextEx()
			:BatchContext(s_Count)
		{
			m_pCasual = &m_Buf1.get();
			m_pKCasual = &m_Buf2.get();
		}
	};

	struct InnerProduct::Modifier::Channel
	{
		Scalar::Native m_pV[nDim];
		void SetPwr(const Scalar::Native& x); // m_pV[i] = x ^ i
	};

	class Commitment
	{
		const Scalar::Native& k;
		const Amount& val;
	public:
		Commitment(const Scalar::Native& k_, const Amount& val_) :k(k_) ,val(val_) {}
		void Assign(Point::Native& res, bool bSet) const;
	};

	namespace Tag
	{
		bool IsCustom(const Point::Native* pHGen);
		void AddValue(Point::Native&, const Point::Native* pHGen, Amount);
	}

	class Oracle
	{
		Hash::Processor m_hp;
	public:
		void Reset();

		template <typename T>
		Oracle& operator << (const T& t) { m_hp << t; return *this; }

		template <typename T>
		Oracle& Serialize(const T& t) { m_hp.Serialize(t); return *this; }

		void operator >> (Scalar::Native&);
		void operator >> (Hash::Value&);
	};

	struct RangeProof::Confidential::Nonces
	{
		Scalar::Native m_tau1;
		Scalar::Native m_tau2;

		Nonces() {}
		Nonces(const uintBig& seedSk) { Init(seedSk); }

		void Init(const uintBig& seedSk);

		void AddInfo1(Point::Native& ptT1, Point::Native& ptT2) const;
		void AddInfo2(Scalar::Native& taux, const Scalar::Native& sk, const ChallengeSet&) const;
		void AddInfo2(Scalar::Native& taux, const ChallengeSet&) const;
	};
}
