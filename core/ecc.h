#pragma once

#include <stdint.h>
#include <string.h> // memcmp
//#include "../secp256k1-zkp/include/secp256k1.h"

#ifndef _countof
#	define _countof(_Array) (sizeof(_Array) / sizeof(_Array[0])) 
#endif // _countof


namespace ECC
{
	void GenerateRandom(void*, uint32_t);

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

	class Context
	{
		// secp256k1_context* m_pOpaque;
		// TODO
	public:

		Context();
		~Context();

		static Context& get();
	};

	static const uint32_t nBits = 256;
	typedef uintBig_t<nBits> uintBig;

	extern const uintBig g_Prime;

	struct Scalar
	{
		uintBig m_Value; // valid range is [0 .. g_Prime)

		bool IsValid() const;
		void SetRandom();
	};

	namespace Native
	{
		struct Generator {
			// TODO
			bool Create(const char* szSeed);
		};

		struct Point {
			// TODO
			void Set(const Generator&);
			void Set(const Generator&, const uintBig&);
			void Add(const Point&);
			void Sub(const Point&);
		};
	}

	struct Point
	{
		// compressed form, suitable for serialiation and etc.

		uint8_t m_Sign; // only 1st bit (lsb) is considered. Actually specifies if the element is a quadratic residual
		uintBig m_X;

		void Import(const Native::Point&);
		void Export(Native::Point&) const;
	};

	struct Hash
	{
		typedef uintBig_t<256> Value;
		Value m_Value;

		Hash();

		void Process(const void*, uint32_t);
	};

	struct RangeProof
	{
		uint8_t m_pOpaque[700]; // TODO

		bool IsValid(const Point&, uint64_t nMinimum = 1) const;
		void Generate(const Point&, uint64_t nMinimum = 1);
	};

	struct Signature
	{
		uintBig m_NonceX;
		Scalar m_Value;

		bool IsValid(const Hash::Value& msg);
		void Create(const Hash::Value& msg);
	};
}

