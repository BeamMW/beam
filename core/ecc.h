#pragma once

#include <stdint.h>
#include <string.h> // memcmp

#ifndef _countof
#	define _countof(_Array) (sizeof(_Array) / sizeof(_Array[0]))
#endif // _countof


namespace ECC
{
	void GenerateRandom(void*, uint32_t);
	void SecureErase(void*, uint32_t);
/*
	template <class T>
	class SecureEraseGuard {
		T* m_pObj;
	public:
		SecureEraseGuard(T* p = NULL) :m_pObj(p) {}
		SecureEraseGuard(T& t) :m_pObj(&t) {}
		~SecureEraseGuard() { Erase(); }

		void Erase() {
			if (m_pObj) {
				SecureErase(m_pObj, sizeof(T));
				m_pObj = NULL;
			}
		}

		void Detach() { m_pObj = NULL; }

		void Set(T* p) {
			Erase();
			m_pObj = p;
		}

		void Set(T& t) { Set(&t); }
	};
*/
	template <typename T>
	struct NoLeak
	{
		T V;
		~NoLeak() { SecureErase(&V, sizeof(T)); }
	};

	template <uint32_t nBits_>
	struct uintBig_t
	{
		static_assert(!(7 & nBits_), "should be byte-aligned");

		// in Big-Endian representation
		uint8_t m_pData[nBits_ >> 3];

		constexpr size_t size() const
		{
			return sizeof(m_pData);
		}

		void SetZero()
		{
			memset(m_pData, 0, sizeof(m_pData));
		}

		bool IsZero() const
		{
			for (int i = 0; i < _countof(m_pData); i++)
				if (m_pData[i])
					return false;
			return true;
		}

		void SetRandom()
		{
			GenerateRandom(m_pData, sizeof(m_pData));
		}

		// from ordinal types (unsigned)
		template <typename T>
		void Set(T x)
		{
			static_assert(sizeof(m_pData) >= sizeof(x), "too small");
			static_assert(T(-1) > 0, "must be unsigned");

			memset(m_pData, 0, sizeof(m_pData) - sizeof(x));

			for (int i = 0; i < sizeof(x); i++, x >>= 8)
				m_pData[_countof(m_pData) - 1 - i] = (uint8_t) x;
		}

		void Inc()
		{
			for (int i = 0; i < _countof(m_pData); i++)
				if (++m_pData[_countof(m_pData) - 1 - i])
					break;

		}

		int cmp(const uintBig_t& x) const { return memcmp(m_pData, x.m_pData, sizeof(m_pData)); }

		bool operator < (const uintBig_t& x) const { return cmp(x) < 0; }
		bool operator > (const uintBig_t& x) const { return cmp(x) > 0; }
		bool operator <= (const uintBig_t& x) const { return cmp(x) <= 0; }
		bool operator >= (const uintBig_t& x) const { return cmp(x) >= 0; }
		bool operator == (const uintBig_t& x) const { return cmp(x) == 0; }
	};

	static const uint32_t nBits = 256;
	typedef uintBig_t<nBits> uintBig;

	struct Scalar
	{
		static const uintBig s_Order;

		uintBig m_Value; // valid range is [0 .. s_Order)

		bool IsValid() const;
		void SetRandom();

		class Native;
	};

	struct Point
	{
		static const uintBig s_FieldOrder; // The field order, it's different from the group order (a little bigger).

		uintBig m_X; // valid range is [0 .. s_FieldOrder)

		bool m_bQuadraticResidue; // analogous to the sign

		int cmp(const Point&) const;

		class Native;
	};

	struct Hash
	{
		typedef uintBig_t<256> Value;
		Value m_Value;

		class Processor;
	};

	typedef uint64_t Amount;

	struct Signature
	{
		Scalar m_e;
		Scalar m_k;

		bool IsValid(const Hash::Value& msg, const Point::Native& pk) const;
		void Create(const Hash::Value& msg, const Scalar::Native& sk);

		int cmp(const Signature&) const;

	private:
		static void get_Challenge(Scalar::Native&, const Point::Native&, const Hash::Value& msg);
	};

	namespace RangeProof
	{
		static const Amount s_MinimumValue = 1;

		struct Confidential
		{
			uint8_t m_pOpaque[700]; // TODO

			void Create(const Scalar::Native& sk, Amount);
			bool IsValid(const Point&) const;
			int cmp(const Confidential&) const;
		};

		struct Public
		{
			Signature m_Signature;
			Amount m_Value;

			void Create(const Scalar::Native& sk); // amount should have been set
			bool IsValid(const Point&) const;
			int cmp(const Public&) const;

		private:
			void get_Msg(Hash::Value&) const;
		};
	}
}

