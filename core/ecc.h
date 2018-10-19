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

	static const uint32_t nBits = 256;
	typedef beam::uintBig_t<nBits> uintBig;


	class Commitment;
	class Oracle;
	struct NonceGenerator;

	void GenerateNonce(uintBig&, const uintBig& sk, const uintBig& msg, const uintBig* pMsg2, uint32_t nAttempt /* = 0 */);

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
		bool	m_Y; // Flag for Y. Currently specifies if it's odd

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

		bool IsValid(const Hash::Value& msg, const Point::Native& pk) const;
		bool IsValidPartial(const Hash::Value& msg, const Point::Native& pubNonce, const Point::Native& pk) const;

		// simple signature
		void Sign(const Hash::Value& msg, const Scalar::Native& sk);

		// multi-signature
		struct MultiSig;

		int cmp(const Signature&) const;
		COMPARISON_VIA_CMP

	private:
		static void get_Challenge(Scalar::Native&, const Point&, const Hash::Value& msg);
	};

	struct Kdf
	{
		NoLeak<uintBig> m_Secret;
		void DeriveKey(Scalar::Native&, uint64_t nKeyIndex, uint32_t nFlags, uint32_t nExtra = 0) const;
	};

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
		struct Modifier {
			const Scalar::Native* m_pMultiplier[2];
			Modifier() { ZeroObject(m_pMultiplier); }
		};

		void Create(Point::Native& commAB, const Scalar::Native& dotAB, const Scalar::Native* pA, const Scalar::Native* pB, const Modifier& = Modifier());
		bool IsValid(const Point::Native& commAB, const Scalar::Native& dotAB, const Modifier& = Modifier()) const;

		struct BatchContext;
		template <uint32_t nBatchSize> struct BatchContextEx;

		void Create(Oracle&, const Scalar::Native& dotAB, const Scalar::Native* pA, const Scalar::Native* pB, const Modifier& = Modifier());
		bool IsValid(BatchContext&, Oracle&, const Scalar::Native& dotAB, const Modifier& = Modifier()) const;

	private:
		struct Calculator;

		void Create(Oracle&, Point::Native* pAB, const Scalar::Native& dotAB, const Scalar::Native* pA, const Scalar::Native* pB, const Modifier&);
		bool IsValid(BatchContext&, const Point::Native& commAB, const Scalar::Native& dotAB, const Modifier& mod) const;
	};

	namespace RangeProof
	{
		static const Amount s_MinimumValue = 1;

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
			} m_Part3;

			Scalar m_Mu;
			Scalar m_tDot;

			InnerProduct m_P_Tag; // contains commitment P - m_Mu*G

			struct CreatorParams
			{
				NoLeak<uintBig> m_Seed; // must be a function of the commitment and master secret
				Amount m_Value;
				uint8_t m_pOpaque[sizeof(uintBig) - sizeof(Amount)];

				struct Packed;
			};

			void Create(const Scalar::Native& sk, const CreatorParams&, Oracle&);
			bool IsValid(const Point::Native&, Oracle&) const;
			bool IsValid(const Point::Native&, Oracle&, InnerProduct::BatchContext&) const;

			void Recover(Oracle&, CreatorParams&) const;

			int cmp(const Confidential&) const;
			COMPARISON_VIA_CMP

			// multisig
			static void CoSignPart(const Scalar::Native& sk, Amount v, Part2&);
			static void CoSignPart(const Scalar::Native& sk, Amount v, Oracle&, const Part1&, const Part2&, Part3&);

			struct Phase {
				enum Enum {
					SinglePass, // regular, no multisig
					//Step1,
					Step2,
					Finalize,
				};
			};

			bool CoSign(const Scalar::Native& sk, const CreatorParams&, Oracle&, Phase::Enum); // for multi-sig use 1,2,3 for 1st-pass


		private:
			struct MultiSig;
			struct ChallengeSet;
		};

		struct Public
		{
			Signature m_Signature;
			Amount m_Value;

			void Create(const Scalar::Native& sk, Oracle&); // amount should have been set
			bool IsValid(const Point::Native&, Oracle&) const;

			int cmp(const Public&) const;
			COMPARISON_VIA_CMP
		};
	}
}

