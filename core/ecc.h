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

#include <stdint.h>
#include <string.h> // memcmp
#include <ostream>
#include <assert.h>

#ifndef _countof
#	define _countof(_Array) (sizeof(_Array) / sizeof(_Array[0]))
#endif // _countof

inline void memset0(void* p, size_t n) { memset(p, 0, n); }
bool memis0(const void* p, size_t n);

template <typename T>
inline void ZeroObject(T& x)
{
	memset0(&x, sizeof(x));
}

#define COMPARISON_VIA_CMP(class_name) \
	bool operator < (const class_name& x) const { return cmp(x) < 0; } \
	bool operator > (const class_name& x) const { return cmp(x) > 0; } \
	bool operator <= (const class_name& x) const { return cmp(x) <= 0; } \
	bool operator >= (const class_name& x) const { return cmp(x) >= 0; } \
	bool operator == (const class_name& x) const { return cmp(x) == 0; } \
	bool operator != (const class_name& x) const { return cmp(x) != 0; }

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
	enum Zero_ { Zero };
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

	template <uint32_t nBits_>
	struct uintBig_t
	{
		static const uint32_t nBits = nBits_;

        uintBig_t()
        {
            ZeroObject(m_pData);
        }

        uintBig_t(const uint8_t bytes[nBits_ >> 3])
        {
            memcpy(m_pData, bytes, nBits_ >> 3);
        }

        uintBig_t(std::initializer_list<uint8_t> bytes)
        {
            std::copy(bytes.begin(), bytes.end(), m_pData);
        }

		static_assert(!(7 & nBits_), "should be byte-aligned");

		// in Big-Endian representation
		uint8_t m_pData[nBits_ >> 3];

		constexpr size_t size() const
		{
			return sizeof(m_pData);
		}

		uintBig_t& operator = (Zero_)
		{
			ZeroObject(m_pData);
			return *this;
		}

		template <uint32_t nBitsOther_>
		uintBig_t& operator = (const uintBig_t<nBitsOther_>& v)
		{
			if (sizeof(v.m_pData) >= sizeof(m_pData))
				memcpy(m_pData, v.m_pData + sizeof(v.m_pData) - sizeof(m_pData), sizeof(m_pData));
			else
			{
				memset0(m_pData, sizeof(m_pData) - sizeof(v.m_pData));
				memcpy(m_pData + sizeof(m_pData) - sizeof(v.m_pData), v.m_pData, sizeof(v.m_pData));
			}
			return *this;
		}

		bool operator == (Zero_) const
		{
			return memis0(m_pData, sizeof(m_pData));
		}

		// from ordinal types (unsigned)
		template <typename T>
		uintBig_t& operator = (T x)
		{
			memset0(m_pData, sizeof(m_pData) - sizeof(x));
			AssignRange<T, 0>(x);

			return *this;
		}

		template <typename T>
		void AssignRangeAligned(T x, uint32_t nOffsetBytes, uint32_t nBytes)
		{
			assert(sizeof(m_pData) >= nBytes + nOffsetBytes);
			static_assert(T(-1) > 0, "must be unsigned");

			for (uint32_t i = 0; i < nBytes; i++, x >>= 8)
				m_pData[_countof(m_pData) - 1 - nOffsetBytes - i] = (uint8_t) x;
		}

		template <typename T, uint32_t nOffset>
		void AssignRange(T x)
		{
			static_assert(!(nOffset & 7), "offset must be on byte boundary");
			static_assert(sizeof(m_pData) >= sizeof(x) + (nOffset >> 3), "too small");

			AssignRangeAligned<T>(x, nOffset >> 3, sizeof(x));
		}

		template <typename T>
		bool AssignRangeAlignedSafe(T x, uint32_t nOffsetBytes, uint32_t nBytes) // returns false if truncated
		{
			if (sizeof(m_pData) < nOffsetBytes)
				return false;

			uint32_t n = sizeof(m_pData) - nOffsetBytes;
			bool b = (nBytes <= n);

			AssignRangeAligned<T>(x, nOffsetBytes, b ? nBytes : n);
			return b;
		}

		template <typename T>
		bool AssignSafe(T x, uint32_t nOffset) // returns false if truncated
		{
			static_assert(T(-1) > 0, "must be unsigned");

			uint32_t nOffsetBytes = nOffset >> 3;
			nOffset &= 7;

			if (!AssignRangeAlignedSafe<T>(x << nOffset, nOffsetBytes, sizeof(x)))
				return false;

			if (nOffset)
			{
				nOffsetBytes += sizeof(x);
				if (_countof(m_pData) - 1 < nOffsetBytes)
					return false;

				uint8_t resid = x >> ((sizeof(x) << 3) - nOffset);
				m_pData[_countof(m_pData) - 1 - nOffsetBytes] = resid;
			}

			return true;
		}

		void Inc()
		{
			for (size_t i = _countof(m_pData); i--; )
				if (++m_pData[i])
					break;

		}

		void GenerateNonce(const uintBig_t& sk, const uintBig_t& msg, const uintBig_t* pMsg2, uint32_t nAttempt = 0); // implemented only for nBits_ = 256 bits

		// Simple arithmetics. For casual use only (not performance-critical)
		void operator += (const uintBig_t& x)
		{
			uint16_t carry = 0;
			for (int i = _countof(m_pData); i--; )
			{
				carry += m_pData[i];
				carry += x.m_pData[i];

				m_pData[i] = (uint8_t) carry;
				carry >>= 8;
			}
		}

		uintBig_t operator * (const uintBig_t& x) const
		{
			uintBig_t res;
			res = Zero;

			for (size_t j = 0; j < _countof(x.m_pData); j++)
			{
				uint8_t y = x.m_pData[_countof(x.m_pData) - 1 - j];

				uint16_t carry = 0;
				for (size_t i = _countof(m_pData); i-- > j; )
				{
					uint16_t val = m_pData[i];
					val *= y;
					carry += val;
					carry += res.m_pData[i - j];

					res.m_pData[i - j] = (uint8_t) carry;
					carry >>= 8;
				}
			}

			return res;
		}

		void Inv()
		{
			for (int i = _countof(m_pData); i--; )
				m_pData[i] ^= 0xff;
		}

		void Negate()
		{
			Inv();
			Inc();
		}

		void operator ^= (const uintBig_t& x)
		{
			for (int i = _countof(m_pData); i--; )
				m_pData[i] ^= x.m_pData[i];
		}

		int cmp(const uintBig_t& x) const { return memcmp(m_pData, x.m_pData, sizeof(m_pData)); }
		COMPARISON_VIA_CMP(uintBig_t)
	};

	static const uint32_t nBits = 256;
	typedef uintBig_t<nBits> uintBig;

	std::ostream& operator << (std::ostream&, const uintBig&);


	class Commitment;
	class Oracle;
	struct NonceGenerator;

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
		COMPARISON_VIA_CMP(Scalar)
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
		COMPARISON_VIA_CMP(Point)


		Point& operator = (const Native&);
		Point& operator = (const Point&);
		Point& operator = (const Commitment&);
	};

	std::ostream& operator << (std::ostream&, const Point&);

	struct Hash
	{
		typedef uintBig_t<256> Value;
		Value m_Value;

		class Processor;
		class Mac;
	};

	typedef uint64_t Amount;

	struct Signature
	{
		Scalar m_e;
		Scalar m_k;

		bool IsValid(const Hash::Value& msg, const Point::Native& pk) const;

		// simple signature
		void Sign(const Hash::Value& msg, const Scalar::Native& sk);

		// multi-signature
		struct MultiSig;
		void CoSign(Scalar::Native& k, const Hash::Value& msg, const Scalar::Native& sk, const MultiSig&);

		int cmp(const Signature&) const;
		COMPARISON_VIA_CMP(Signature)

		void get_PublicNonce(Point::Native& pubNonce, const Point::Native& pk) const; // useful for verifications during multi-sig
		bool IsValidPartial(const Point::Native& pubNonce, const Point::Native& pk) const;

	private:
		static void get_Challenge(Scalar::Native&, const Point::Native&, const Hash::Value& msg);
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
		void Create(BatchContext&, Oracle&, const Scalar::Native& dotAB, const Scalar::Native* pA, const Scalar::Native* pB, const Modifier& = Modifier());
		bool IsValid(BatchContext&, Oracle&, const Scalar::Native& dotAB, const Modifier& = Modifier()) const;

	private:
		struct Calculator;

		void Create(BatchContext&, Oracle&, Point::Native* pAB, const Scalar::Native& dotAB, const Scalar::Native* pA, const Scalar::Native* pB, const Modifier&);
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

			void Create(const Scalar::Native& sk, Amount, Oracle&);
			bool IsValid(const Point::Native&, Oracle&) const;
			bool IsValid(const Point::Native&, Oracle&, InnerProduct::BatchContext&) const;

			int cmp(const Confidential&) const;
			COMPARISON_VIA_CMP(Confidential)

			// multisig
			static void CoSignPart(const Scalar::Native& sk, Amount, Oracle&, Part2&);
			static void CoSignPart(const Scalar::Native& sk, Amount, Oracle&, const Part1&, const Part2&, Part3&);

			struct Phase {
				enum Enum {
					SinglePass, // regular, no multisig
					//Step1,
					Step2,
					Finalize,
				};
			};

			bool CoSign(const Scalar::Native& sk, Amount, Oracle&, Phase::Enum); // for multi-sig use 1,2,3 for 1st-pass


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
			COMPARISON_VIA_CMP(Public)
		};
	}
}

