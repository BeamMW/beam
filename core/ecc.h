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
#include "common.h"
#include "uintBig.h"

namespace ECC
{
	void InitializeContext(); // builds various generators. Necessary for commitments and signatures.
	// Not necessary for hashes, scalar and 'casual' point arithmetics

	void GenRandom(void*, uint32_t nSize); // with OS support

	template <uint32_t nBytes_>
	inline void GenRandom(beam::uintBig_t<nBytes_>& x) { GenRandom(x.m_pData, x.nBytes); }

	struct Mode {
		enum Enum {
			Secure, // maximum security. Constant-time guarantee whenever possible, protection from side-channel attacks
			Fast
		};

		class Scope {
			const Enum m_PrevMode;
		public:
			Scope(Enum e);
			~Scope();
		};
	};

	struct Initializer {
		Initializer() {
			InitializeContext();
		}
	};

	// Syntactic sugar!
	using beam::Zero_;
	using beam::Zero;
	enum Two_ { Two };

	struct Op
	{
		enum Sign {
			Plus,
			Minus,
			Mul,
			Div,
			Double
		};

		template <Sign, typename X>
		struct Unary {
			const X& x;
			Unary(const X& x_) :x(x_) {}
		};

		template <Sign, typename X, typename Y>
		struct Binary {
			const X& x;
			const Y& y;
			Binary(const X& x_, const Y& y_) :x(x_) ,y(y_) {}
		};
	};

	void SecureErase(void*, uint32_t);
	template <typename T> void SecureErase(T& t) { SecureErase(&t, sizeof(T)); }

	template <typename T>
	struct NoLeak
	{
		T V;
		~NoLeak() { SecureErase(V); }
	};

	static const uint32_t nBytes = 32;
	static const uint32_t nBits = nBytes << 3;
	typedef beam::uintBig_t<nBytes> uintBig;


	class Commitment;
	class Oracle;

	struct Scalar
	{
		static const uintBig s_Order;

		uintBig m_Value; // valid range is [0 .. s_Order)

		Scalar() {}
		template <typename T> explicit Scalar(const T& t) { *this = t; }

		bool IsValid() const;
		void TestValid() const; // will raise exc if invalid

		class Native;
		Scalar& operator = (const Native&);
		Scalar& operator = (const Zero_&);

		int cmp(const Scalar& x) const { return m_Value.cmp(x.m_Value); }
		COMPARISON_VIA_CMP
	};

	std::ostream& operator << (std::ostream&, const Scalar&);

	struct Point
	{
		static const uintBig s_FieldOrder; // The field order, it's different from the group order (a little bigger).

		uintBig	m_X; // valid range is [0 .. s_FieldOrder)
		uint8_t m_Y; // Flag for Y. Currently specifies if it's odd

		Point() {}

        class Native;
        Point(const Native& t) { *this = t; }
        Point(const Point& t) { *this = t; }
        Point(const Commitment& t) { *this = t; }

		int cmp(const Point&) const;
		COMPARISON_VIA_CMP

		Point& operator = (const Native&);
		Point& operator = (const Point&);
		Point& operator = (const Commitment&);

		struct Storage;
	};

	std::ostream& operator << (std::ostream&, const Point&);

	struct Hash
	{
		typedef uintBig Value;
		Value m_Value;

		class Processor;
		class Mac;
	};

	typedef beam::Amount Amount;

	struct Signature
	{
		Point m_NoncePub;
		Scalar m_k;

		bool IsValid(const Hash::Value& msg, const Point::Native& pk, const Scalar* pSer = nullptr) const;
		bool IsValidPartial(const Hash::Value& msg, const Point::Native& pubNonce, const Point::Native& pk, const Scalar* pSer = nullptr) const;

		// simple signature
		void Sign(const Hash::Value& msg, const Scalar::Native& sk);

		// multi-signature
		struct MultiSig;

		int cmp(const Signature&) const;
		COMPARISON_VIA_CMP

	private:
		static void get_Challenge(Scalar::Native&, const Point&, const Hash::Value& msg);
	};

	struct Key
	{
		typedef uint32_t Index; // a 'short ID' used when different children are given different sub-keys.

		struct Type
			:public beam::FourCC
		{
			Type() {}
			Type(uint32_t x) :FourCC(x) {}

			// definitions for common types, that are used in several places. But values can be arbitrary, not only for this list
			static const uint32_t Comission = FOURCC_FROM(fees);
			static const uint32_t Coinbase  = FOURCC_FROM(mine);
			static const uint32_t Regular   = FOURCC_FROM(norm);
			static const uint32_t Change    = FOURCC_FROM(chng);
			static const uint32_t Kernel    = FOURCC_FROM(kern); // tests only
			static const uint32_t Kernel2   = FOURCC_FROM(kerM); // used by the miner
			static const uint32_t Identity  = FOURCC_FROM(iden); // Node-Wallet auth
			static const uint32_t ChildKey  = FOURCC_FROM(SubK);
			static const uint32_t Bbs       = FOURCC_FROM(BbsM);
			static const uint32_t Decoy     = FOURCC_FROM(dcoy);
			static const uint32_t Treasury  = FOURCC_FROM(Tres);
		};

		struct ID
		{
			uint64_t	m_Idx;
			Type		m_Type;
			Index		m_SubIdx; // currently set to the Kdf child index

			ID() {}
			ID(Zero_) { ZeroObject(*this); }

			ID(uint64_t nIdx, Type type, uint32_t nSubIdx = 0) // most common c'tor
			{
				m_Idx = nIdx;
				m_Type = type;
				m_SubIdx = nSubIdx;
			}

			void get_Hash(Hash::Value&) const;

#pragma pack (push, 1)
			struct Packed
			{
				beam::uintBigFor<uint64_t>::Type m_Idx;
				beam::uintBigFor<uint32_t>::Type m_Type;
				beam::uintBigFor<uint32_t>::Type m_SubIdx;
				void operator = (const ID&);
			};
#pragma pack (pop)

			void operator = (const Packed&);

			int cmp(const ID&) const;
			COMPARISON_VIA_CMP
		};

		struct IDV
			:public ID
		{
			struct Scheme
			{
				static const uint8_t V0 = 0;
				static const uint8_t V1 = 1;
				static const uint8_t BB21 = 2; // worakround for BB.2.1

				static const uint32_t s_SubKeyBits = 24;
				static const Index s_SubKeyMask = (static_cast<Index>(1) << s_SubKeyBits) - 1;
			};


			Amount m_Value;
			IDV() {}
			IDV(Zero_)
				:ID(Zero)
				,m_Value(0)
			{
				set_Subkey(0);
			}

			IDV(Amount v, uint64_t nIdx, Type type, Index nSubIdx = 0, Index nScheme = Scheme::V1)
				:ID(nIdx, type)
				,m_Value(v)
			{
				set_Subkey(nSubIdx, nScheme);
			}

			Index get_Scheme() const
			{
				return m_SubIdx >> Scheme::s_SubKeyBits;
			}

			Index get_Subkey() const
			{
				return m_SubIdx & Scheme::s_SubKeyMask;
			}

			void set_Subkey(Index nSubIdx, Index nScheme = Scheme::V1)
			{
				m_SubIdx = (nSubIdx & Scheme::s_SubKeyMask) | (nScheme << Scheme::s_SubKeyBits);
			}

#pragma pack (push, 1)
			struct Packed
				:public ID::Packed
			{
				beam::uintBigFor<Amount>::Type m_Value;
				void operator = (const IDV&);
			};
#pragma pack (pop)

			void operator = (const Packed&);

			bool IsBb21Possible() const
			{
				return m_SubIdx && (Scheme::V0 == get_Scheme());
			}

			void set_WorkaroundBb21()
			{
				set_Subkey(get_Subkey(), Scheme::BB21);
			}

			int cmp(const IDV&) const;
			COMPARISON_VIA_CMP
		};

		struct IPKdf
		{
			typedef std::shared_ptr<IPKdf> Ptr;

			virtual void DerivePKey(Scalar::Native&, const Hash::Value&) = 0;
			virtual void DerivePKeyG(Point::Native&, const Hash::Value&) = 0;
			virtual void DerivePKeyJ(Point::Native&, const Hash::Value&) = 0;

			bool IsSame(IPKdf&);
		};

		struct IKdf
			:public IPKdf
		{
			typedef std::shared_ptr<IKdf> Ptr;

			void DeriveKey(Scalar::Native&, const Key::ID&);
			virtual void DeriveKey(Scalar::Native&, const Hash::Value&) = 0;

			virtual void DerivePKeyG(Point::Native&, const Hash::Value&) override;
			virtual void DerivePKeyJ(Point::Native&, const Hash::Value&) override;
		};
	};

	std::ostream& operator << (std::ostream&, const Key::IDV&);

	struct InnerProduct
	{
		// Compact proof that the inner product of 2 vectors is a specified scalar.
		// Part of the bulletproof scheme
		//
		// Current implementation is 'fast' (i.e. not 'secure'), since the scheme isn't zero-knowledge wrt input vectors.
		// In bulletproof source vectors are blinded.

		static const uint32_t nDim = sizeof(Amount) << 3; // 64
		static const uint32_t nCycles = 6;
		static_assert(1 << nCycles == nDim, "");

		Point m_pLR[nCycles][2];	// pairs of L,R values, per reduction  iteration
		Scalar m_pCondensed[2];		// remaining 1-dimension vectors

		static void get_Dot(Scalar::Native& res, const Scalar::Native* pA, const Scalar::Native* pB);

		// optional modifier for the used generators. Needed for the bulletproof.
		struct Modifier
		{
			struct Channel;

			Channel* m_ppC[2]; // multipliers to vector elements
			const Scalar::Native* m_pAmbient; // multiplier to all the other elements: LR and G

			void Set(Scalar::Native& dst, const Scalar::Native& src, int i, int j) const;

			Modifier()
				:m_ppC{ 0 }
				,m_pAmbient(nullptr)
			{
			}
		};

		void Create(Point::Native& commAB, const Scalar::Native& dotAB, const Scalar::Native* pA, const Scalar::Native* pB, const Modifier& = Modifier());
		bool IsValid(const Point::Native& commAB, const Scalar::Native& dotAB, const Modifier& = Modifier()) const;

		struct BatchContext;
		template <uint32_t nBatchSize> struct BatchContextEx;

		void Create(Oracle&, const Scalar::Native& dotAB, const Scalar::Native* pA, const Scalar::Native* pB, const Modifier& = Modifier());

		struct Challenges;
		bool IsValid(BatchContext&, Challenges&, const Scalar::Native& dotAB, const Modifier& = Modifier()) const;

	private:
		struct Calculator;

		void Create(Oracle&, Point::Native* pAB, const Scalar::Native& dotAB, const Scalar::Native* pA, const Scalar::Native* pB, const Modifier&);
		bool IsValid(BatchContext&, const Point::Native& commAB, const Scalar::Native& dotAB, const Modifier& mod) const;
	};

	namespace RangeProof
	{
		static const Amount s_MinimumValue = 1;

		struct CreatorParams
		{
			NoLeak<uintBig> m_Seed; // must be a function of the commitment and master secret
			Key::IDV m_Kidv;

			struct Padded;
		};

		struct Confidential
		{
			// Bulletproof scheme
			struct Part1 {
				Point m_A;
				Point m_S;
			} m_Part1;

			// <- y,z

			struct Part2 {
				Point m_T1;
				Point m_T2;
			} m_Part2;

			// <- x

			struct Part3 {
				Scalar m_TauX;
				int cmp(const Part3& v) const { return m_TauX.cmp(v.m_TauX); }
			} m_Part3;

			Scalar m_Mu;
			Scalar m_tDot;

			InnerProduct m_P_Tag; // contains commitment P - m_Mu*G

			// Nonce generation policy for signing. There are two distinct nonce generators, both need to be initialized with seeds. One for value blinding and Key::IDV embedding, and the other one that blinds the secret key.
			// Seed for value and Key::IDV is always specified explicitly, and should be deducible from the public Kdf and the commitment.
			// Regaring the seed for secret keys:
			//		In case of single-sig it's derived directly from the secret key itself AND external nonce source
			//		In case of multi-sig it should be specified explicitly by the caller (the resulting nonce must be the same for multiple invocations).
			//			Means - the caller must take care of constructing the nonce, which has external randomness

			void Create(const Scalar::Native& sk, const CreatorParams&, Oracle&, const Point::Native* pHGen = nullptr, Part3* pSer = nullptr, const Scalar::Native* pSkSer = nullptr); // single-pass
			bool IsValid(const Point::Native&, Oracle&, const Point::Native* pHGen = nullptr, Part3* pSer = nullptr) const;
			bool IsValid(const Point::Native&, Oracle&, InnerProduct::BatchContext&, const Point::Native* pHGen = nullptr, Part3* pSer = nullptr) const;

			bool Recover(Oracle&, CreatorParams&) const;

			int cmp(const Confidential&) const;
			COMPARISON_VIA_CMP

			// multisig
			struct MultiSig
			{
				Part1 m_Part1;
				Part2 m_Part2;

				struct Impl;

				static bool CoSignPart(const uintBig& seedSk, Part2&);
				void CoSignPart(const uintBig& seedSk, const Scalar::Native& sk, Oracle&, Part3&) const;
			};

			struct Phase {
				enum Enum {
					SinglePass, // regular, no multisig
					//Step1,
					Step2,
					Finalize,
				};
			};

			bool CoSign(const uintBig& seedSk, const Scalar::Native& sk, const CreatorParams&, Oracle&, Phase::Enum, const Point::Native* pHGen = nullptr, Part3* pSer = nullptr, const Scalar::Native* pSkSer = nullptr);

            static void GenerateSeed(uintBig& seedSk, const Scalar::Native& sk, Amount amount, Oracle& oracle);

		private:
			struct ChallengeSet0;
			struct ChallengeSet1;
			struct ChallengeSet2;
			static void CalcA(Point&, const Scalar::Native& alpha, Amount v);
		};

		struct Public
		{
			Signature m_Signature;
			Amount m_Value;

#pragma pack (push, 1)
			struct Recovery // encoded of course
			{
				Key::ID::Packed m_Kid;
				Hash::Value m_Checksum;
			} m_Recovery;
#pragma pack (pop)

			void Create(const Scalar::Native& sk, const CreatorParams&, Oracle&); // amount should have been set
			bool IsValid(const Point::Native&, Oracle&, const Point::Native* pHGen = nullptr) const;
			bool Recover(CreatorParams&) const;

			int cmp(const Public&) const;
			COMPARISON_VIA_CMP

		private:
			static void XCryptKid(Key::ID::Packed&, const CreatorParams&, Hash::Value& hvChecksum);
			void get_Msg(Hash::Value&, Oracle&) const;
		};
	}
}

