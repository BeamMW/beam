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

#include "common.h"
#include "ecc_native.h"

#if defined(__clang__) || defined(__GNUC__) || defined(__GNUG__)
#	pragma GCC diagnostic push
#	pragma GCC diagnostic ignored "-Wunused-function"
#else
#	pragma warning (push, 0) // suppress warnings from secp256k1
#	pragma warning (disable: 4706 4701) // assignment within conditional expression
#endif

#include "secp256k1-zkp/src/group_impl.h"
#include "secp256k1-zkp/src/scalar_impl.h"
#include "secp256k1-zkp/src/field_impl.h"
#include "secp256k1-zkp/src/hash_impl.h"

#if defined(__clang__) || defined(__GNUC__) || defined(__GNUG__)
#	pragma GCC diagnostic pop
#else
#	pragma warning (default: 4706 4701)
#	pragma warning (pop)
#endif

#ifdef WIN32
#	pragma comment (lib, "Bcrypt.lib")
#else // WIN32
#    include <unistd.h>
#    include <fcntl.h>
#endif // WIN32

//#ifdef __linux__
//#	include <sys/syscall.h>
//#	include <linux/random.h>
//#endif // __linux__


namespace ECC {

	//void* NoErase(void*, size_t) { return NULL; }

	// Pointer to the 'eraser' function. The pointer should be non-const (i.e. variable that can be changed at run-time), so that optimizer won't remove this.
	void (*g_pfnEraseFunc)(void*, size_t) = memset0/*NoErase*/;

	void SecureErase(void* p, uint32_t n)
	{
		g_pfnEraseFunc(p, n);
	}

	template <typename T>
	void data_cmov_as(T* pDst, const T* pSrc, int nWords, int flag)
	{
		const T mask0 = flag + ~((T)0);
		const T mask1 = ~mask0;

		for (int n = 0; n < nWords; n++)
			pDst[n] = (pDst[n] & mask0) | (pSrc[n] & mask1);
	}

	template void data_cmov_as<uint32_t>(uint32_t* pDst, const uint32_t* pSrc, int nWords, int flag);

	thread_local Mode::Enum g_Mode = Mode::Secure; // default

	Mode::Scope::Scope(Mode::Enum val)
		:m_PrevMode(g_Mode)
	{
		g_Mode = val;
	}

	Mode::Scope::~Scope()
	{
		g_Mode = m_PrevMode;
	}

	std::ostream& operator << (std::ostream& s, const Scalar& x)
	{
		return operator << (s, x.m_Value);
	}

    std::ostream& operator << (std::ostream& s, const Scalar::Native& x)
    {
        Scalar scalar;
        x.Export(scalar);
        return operator << (s, scalar);
    }

	std::ostream& operator << (std::ostream& s, const Point& x)
	{
		return operator << (s, x.m_X);
	}

    std::ostream& operator << (std::ostream& s, const Point::Native& x)
    {
        Point point;

        x.Export(point);
        return operator << (s, point);
    }

	std::ostream& operator << (std::ostream& s, const Key::IDV& x)
	{
		s
			<< "Key=" << x.m_Type
			<< "-" << x.get_Scheme()
			<< ":" << x.get_Subkey()
			<< ":" << x.m_Idx
			<< ", Value=" << x.m_Value;
		return s;
	}

	void GenRandom(void* p, uint32_t nSize)
	{
		// checkpoint?

#ifdef WIN32

		NTSTATUS ntStatus = BCryptGenRandom(NULL, (PUCHAR) p, nSize, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
		if (ntStatus)
			std::ThrowSystemError(ntStatus);

#else // WIN32

		bool bRet = false;

//#	ifdef __linux__
//
//		ssize_t nRet = syscall(SYS_getrandom, p, nSize, 0);
//		bRet = (nRet == nSize);
//
//#	else // __linux__

		// use standard posix
		int hFile = open("/dev/urandom", O_RDONLY);
		if (hFile >= 0)
		{
			if (read(hFile, p, nSize) == static_cast<ssize_t>(nSize))
				bRet = true;

			close(hFile);
		}

//#	endif // __linux__

		if (!bRet)
			std::ThrowLastError();

#endif // WIN32
	}

	/////////////////////
	// Scalar
	const uintBig Scalar::s_Order = { // fffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364141
		0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFE,
		0xBA,0xAE,0xDC,0xE6,0xAF,0x48,0xA0,0x3B,0xBF,0xD2,0x5E,0x8C,0xD0,0x36,0x41,0x41
	};

	bool Scalar::IsValid() const
	{
		return m_Value < s_Order;
	}

	void Scalar::TestValid() const
	{
		if (!IsValid())
			throw std::runtime_error("invalid scalar");
	}

	Scalar& Scalar::operator = (const Native& v)
	{
		v.Export(*this);
		return *this;
	}

	Scalar& Scalar::operator = (const Zero_&)
	{
		m_Value = Zero;
		return *this;
	}

	Scalar::Native::Native()
    {
        secp256k1_scalar_clear(this);
    }

	Scalar::Native& Scalar::Native::operator = (Zero_)
	{
		secp256k1_scalar_clear(this);
		return *this;
	}

	bool Scalar::Native::operator == (Zero_) const
	{
		return secp256k1_scalar_is_zero(this) != 0; // constant time guaranteed
	}

    bool Scalar::Native::operator != (Zero_ v) const
    {
        return !(operator == (v));
    }

	bool Scalar::Native::operator == (const Native& v) const
	{
		// Used in tests only, but implemented with constant mem-time guarantee
		Native x = - *this;
		x += v;
		return x == Zero;
	}

    bool Scalar::Native::operator != (const Native& v) const
    {
        return !(operator == (v));
    }

	Scalar::Native& Scalar::Native::operator = (Minus v)
	{
		secp256k1_scalar_negate(this, &v.x);
		return *this;
	}

	bool Scalar::Native::Import(const Scalar& v)
	{
		int overflow;
		secp256k1_scalar_set_b32(this, v.m_Value.m_pData, &overflow);
		return overflow != 0;
	}

	bool Scalar::Native::ImportNnz(const Scalar& v)
	{
		return
			!Import(v) &&
			!(*this == Zero);

	}

	void Scalar::Native::GenRandomNnz()
	{
		NoLeak<Scalar> s;
		do
			GenRandom(s.V.m_Value);
		while (!ImportNnz(s.V));
	}

	Scalar::Native& Scalar::Native::operator = (const Scalar& v)
	{
		Import(v);
		return *this;
	}

	void Scalar::Native::Export(Scalar& v) const
	{
		secp256k1_scalar_get_b32(v.m_Value.m_pData, this);
	}

	Scalar::Native& Scalar::Native::operator = (uint32_t v)
	{
		secp256k1_scalar_set_int(this, v);
		return *this;
	}

	Scalar::Native& Scalar::Native::operator = (uint64_t v)
	{
		secp256k1_scalar_set_u64(this, v);
		return *this;
	}

	Scalar::Native& Scalar::Native::operator = (Plus v)
	{
		secp256k1_scalar_add(this, &v.x, &v.y);
		return *this;
	}

    Scalar::Native& Scalar::Native::operator = (Minus2 v)
    {
        Scalar::Native temp = -v.y;
        secp256k1_scalar_add(this, &v.x, &temp);
        return *this;
    }

	Scalar::Native& Scalar::Native::operator = (Mul v)
	{
		secp256k1_scalar_mul(this, &v.x, &v.y);
		return *this;
	}

	void Scalar::Native::SetSqr(const Native& v)
	{
		secp256k1_scalar_sqr(this, &v);
	}

	void Scalar::Native::Sqr()
	{
		SetSqr(*this);
	}

	void Scalar::Native::SetInv(const Native& v)
	{
		secp256k1_scalar_inverse(this, &v);
	}

	void Scalar::Native::Inv()
	{
		SetInv(*this);
	}

	/////////////////////
	// Hash
	Hash::Processor::Processor()
	{
		Reset();
	}

	Hash::Processor::~Processor()
	{
		if (m_bInitialized)
			SecureErase(*this);
	}

	void Hash::Processor::Reset()
	{
		secp256k1_sha256_initialize(this);
		m_bInitialized = true;
	}

	void Hash::Processor::Write(const void* p, uint32_t n)
	{
		assert(m_bInitialized);
		secp256k1_sha256_write(this, (const uint8_t*) p, n);
	}

	void Hash::Processor::Finalize(Value& v)
	{
		assert(m_bInitialized);
		secp256k1_sha256_finalize(this, v.m_pData);
		
		m_bInitialized = false;
	}

	void Hash::Processor::Write(const beam::Blob& v)
	{
		Write(v.p, v.n);
	}

	void Hash::Processor::Write(bool b)
	{
		uint8_t n = (false != b);
		Write(n);
	}

	void Hash::Processor::Write(uint8_t n)
	{
		Write(&n, sizeof(n));
	}

	void Hash::Processor::Write(const Scalar& v)
	{
		Write(v.m_Value);
	}

	void Hash::Processor::Write(const Scalar::Native& v)
	{
		NoLeak<Scalar> s_;
		s_.V = v;
		Write(s_.V);
	}

	void Hash::Processor::Write(const Point& v)
	{
		Write(v.m_X);
		Write(v.m_Y);
	}

	void Hash::Processor::Write(const Point::Native& v)
	{
		Write(Point(v));
	}

	void Hash::Mac::Reset(const void* pSecret, uint32_t nSecret)
	{
		secp256k1_hmac_sha256_initialize(this, (uint8_t*)pSecret, nSecret);
	}

	void Hash::Mac::Write(const void* p, uint32_t n)
	{
		secp256k1_hmac_sha256_write(this, (uint8_t*)p, n);
	}

	void Hash::Mac::Finalize(Value& hv)
	{
		secp256k1_hmac_sha256_finalize(this, hv.m_pData);
	}

	/////////////////////
	// Point
	const uintBig Point::s_FieldOrder = { // fffffffffffffffffffffffffffffffffffffffffffffffffffffffefffffc2f
		0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
		0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFE,0xFF,0xFF,0xFC,0x2F
	};

	int Point::cmp(const Point& v) const
	{
		int n = m_X.cmp(v.m_X);
		if (n)
			return n;

		if (m_Y < v.m_Y)
			return -1;
		if (m_Y > v.m_Y)
			return 1;

		return 0;
	}

	Point& Point::operator = (const Native& v)
	{
		v.Export(*this);
		return *this;
	}

	Point& Point::operator = (const Point& v)
	{
		m_X = v.m_X;
		m_Y = v.m_Y;
		return *this;
	}

	Point& Point::operator = (const Commitment& v)
	{
		return operator = (Native(v));
	}

	Point::Native::Native()
    {
        secp256k1_gej_set_infinity(this);
    }

	bool Point::Native::ImportNnz(const Point& v)
	{
		if (v.m_Y > 1)
			return false; // should always be well-formed

		NoLeak<secp256k1_fe> nx;
		if (!secp256k1_fe_set_b32(&nx.V, v.m_X.m_pData))
			return false;

		NoLeak<secp256k1_ge> ge;
		if (!secp256k1_ge_set_xo_var(&ge.V, &nx.V, v.m_Y))
			return false;

		secp256k1_gej_set_ge(this, &ge.V);

		return true;
	}

	bool Point::Native::Import(const Point& v)
	{
		if (ImportNnz(v))
			return true;

		*this = Zero;
		return memis0(&v, sizeof(v));
	}

	bool Point::Native::Export(Point& v) const
	{
		if (*this == Zero)
		{
			ZeroObject(v);
			return false;
		}

		NoLeak<secp256k1_gej> dup;
		dup.V = *this;
		NoLeak<secp256k1_ge> ge;
		secp256k1_ge_set_gej(&ge.V, &dup.V);

		// seems like normalization can be omitted (already done by secp256k1_ge_set_gej), but not guaranteed according to docs.
		// But this has a negligible impact on the performance
		secp256k1_fe_normalize(&ge.V.x);
		secp256k1_fe_normalize(&ge.V.y);

		ExportEx(v, ge.V);

		return true;
	}

	void Point::Native::ExportEx(Point& v, const secp256k1_ge& ge)
	{
		secp256k1_fe_get_b32(v.m_X.m_pData, &ge.x);
		v.m_Y = (secp256k1_fe_is_odd(&ge.y) != 0);
	}

	Point::Native& Point::Native::operator = (Zero_)
	{
		secp256k1_gej_set_infinity(this);
		return *this;
	}

	bool Point::Native::operator == (Zero_) const
	{
		return secp256k1_gej_is_infinity(this) != 0;
	}

    bool Point::Native::operator != (Zero_ v) const
    {
        return !(operator == (v));
    }

	Point::Native& Point::Native::operator = (Minus v)
	{
		secp256k1_gej_neg(this, &v.x);
		return *this;
	}

	Point::Native& Point::Native::operator = (Plus v)
	{
		secp256k1_gej_add_var(this, &v.x, &v.y, NULL);
		return *this;
	}

    Point::Native& Point::Native::operator = (Minus2 v)
    {
        Point::Native temp = -v.y;
        secp256k1_gej_add_var(this, &v.x, &temp, NULL);
        return *this;
    }

	Point::Native& Point::Native::operator = (Double v)
	{
		secp256k1_gej_double_var(this, &v.x, NULL);
		return *this;
	}

	Point::Native& Point::Native::operator = (Mul v)
	{
		MultiMac::Casual mc;
		mc.Init(v.x, v.y);

		MultiMac mm;
		mm.m_pCasual = &mc;
		mm.m_Casual = 1;
		mm.Calculate(*this);

		return *this;
	}

	Point::Native& Point::Native::operator += (Mul v)
	{
		return operator += (Native(v));
	}

    secp256k1_pubkey ConvertPointToPubkey(const Point& point)
    {
        Point::Native native;

        native.Import(point);

        NoLeak<secp256k1_gej> gej;
        NoLeak<secp256k1_ge> ge;

        gej.V = native.get_Raw();
        secp256k1_ge_set_gej(&ge.V, &gej.V);

        const size_t dataSize = 64;
        secp256k1_pubkey pubkey;

        if constexpr (sizeof(secp256k1_ge_storage) == dataSize)
        {
            secp256k1_ge_storage s;
            secp256k1_ge_to_storage(&s, &ge.V);
            memcpy(&pubkey.data[0], &s, dataSize);
        }
        else 
        {
            assert(false && "Unsupported case");
        }

        return pubkey;
    }

    std::vector<uint8_t> SerializePubkey(const secp256k1_pubkey& pubkey)
    {
        size_t dataSize = 65;
        std::vector<uint8_t> data(dataSize);

        secp256k1_context* context = secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);

        secp256k1_ec_pubkey_serialize(context, data.data(), &dataSize, &pubkey, SECP256K1_EC_UNCOMPRESSED);

        secp256k1_context_destroy(context);

        return data;
    }

	/////////////////////
	// Generator
	namespace Generator
	{
		void FromPt(CompactPoint& out, Point::Native& p)
		{
#ifdef ECC_COMPACT_GEN
			secp256k1_ge ge; // used only for non-secret
			secp256k1_ge_set_gej(&ge, &p.get_Raw());
			secp256k1_ge_to_storage(&out, &ge);
#else // ECC_COMPACT_GEN
			out = p.get_Raw();
#endif // ECC_COMPACT_GEN
		}

		void ToPt(Point::Native& p, secp256k1_ge& ge, const CompactPoint& ge_s, bool bSet)
		{
#ifdef ECC_COMPACT_GEN

			secp256k1_ge_from_storage(&ge, &ge_s);

			if (bSet)
				secp256k1_gej_set_ge(&p.get_Raw(), &ge);
			else
				secp256k1_gej_add_ge(&p.get_Raw(), &p.get_Raw(), &ge);

#else // ECC_COMPACT_GEN

			static_assert(sizeof(p) == sizeof(ge_s));

			if (bSet)
				p = (const Point::Native&) ge_s;
			else
				p += (const Point::Native&) ge_s;

#endif // ECC_COMPACT_GEN
		}

		void CreatePointNnz(Point::Native& out, Oracle& oracle, Hash::Processor* phpRes)
		{
			Point pt;
			pt.m_Y = 0;

			do
				oracle >> pt.m_X;
			while (!out.ImportNnz(pt));

			if (phpRes)
				*phpRes << pt;
		}

		bool CreatePts(CompactPoint* pPts, Point::Native& gpos, uint32_t nLevels, Oracle& oracle)
		{
			Point::Native nums, npos, pt;
			CreatePointNnz(nums, oracle, NULL);

			nums += gpos;
			npos = nums;

			for (uint32_t iLev = 1; ; iLev++)
			{
				pt = npos;

				for (uint32_t iPt = 1; ; iPt++)
				{
					if (pt == Zero)
						return false;

					FromPt(*pPts++, pt);

					if (iPt == nPointsPerLevel)
						break;

					pt += gpos;
				}

				if (iLev == nLevels)
					break;

				for (uint32_t i = 0; i < nBitsPerLevel; i++)
					gpos = gpos * Two;

				npos = npos * Two;
				if (iLev + 1 == nLevels)
				{
					npos = -npos;
					npos += nums;
				}
			}

			return true;
		}

		void SetMul(Point::Native& res, bool bSet, const CompactPoint* pPts, const Scalar::Native::uint* p, int nWords)
		{
			static_assert(8 % nBitsPerLevel == 0, "");
			const int nLevelsPerWord = (sizeof(Scalar::Native::uint) << 3) / nBitsPerLevel;
			static_assert(!(nLevelsPerWord & (nLevelsPerWord - 1)), "should be power-of-2");

			NoLeak<CompactPoint> ge_s;
			NoLeak<secp256k1_ge> ge;

			// iterating in lsb to msb order
			for (int iWord = 0; iWord < nWords; iWord++)
			{
				Scalar::Native::uint n = p[iWord];

				for (int j = 0; j < nLevelsPerWord; j++, pPts += nPointsPerLevel)
				{
					uint32_t nSel = (nPointsPerLevel - 1) & n;
					n >>= nBitsPerLevel;

					/** This uses a conditional move to avoid any secret data in array indexes.
					*   _Any_ use of secret indexes has been demonstrated to result in timing
					*   sidechannels, even when the cache-line access patterns are uniform.
					*  See also:
					*   "A word of warning", CHES 2013 Rump Session, by Daniel J. Bernstein and Peter Schwabe
					*    (https://cryptojedi.org/peter/data/chesrump-20130822.pdf) and
					*   "Cache Attacks and Countermeasures: the Case of AES", RSA 2006,
					*    by Dag Arne Osvik, Adi Shamir, and Eran Tromer
					*    (http://www.tau.ac.il/~tromer/papers/cache.pdf)
					*/

					const CompactPoint* pSel;
					if (Mode::Secure == g_Mode)
					{
						pSel = &ge_s.V;
						for (uint32_t i = 0; i < nPointsPerLevel; i++)
							object_cmov(ge_s.V, pPts[i], i == nSel);
					}
					else
						pSel = pPts + nSel;

					ToPt(res, ge.V, *pSel, bSet);
					bSet = false;
				}
			}
		}

		void SetMul(Point::Native& res, bool bSet, const CompactPoint* pPts, const Scalar::Native& k)
		{
			SetMul(res, bSet, pPts, k.get().d, _countof(k.get().d));
		}

		void GeneratePts(const Point::Native& pt, Oracle& oracle, CompactPoint* pPts, uint32_t nLevels)
		{
			while (true)
			{
				Point::Native pt2 = pt;
				if (CreatePts(pPts, pt2, nLevels, oracle))
					break;
			}
		}

		void Obscured::Initialize(const Point::Native& pt, Oracle& oracle)
		{
			Point::Native pt2;
			while (true)
			{
				pt2 = pt;
				if (CreatePts(m_pPts, pt2, nLevels, oracle))
					break;
			}

			oracle >> m_AddScalar;

			Generator::SetMul(pt2, true, m_pPts, m_AddScalar); // pt2 = G * blind
			FromPt(m_AddPt, pt2);

			m_AddScalar = -m_AddScalar;
		}

		void Obscured::AssignInternal(Point::Native& res, bool bSet, Scalar::Native& kTmp, const Scalar::Native& k) const
		{
			if (Mode::Secure == g_Mode)
			{
				secp256k1_ge ge;
				ToPt(res, ge, m_AddPt, bSet);

				kTmp = k + m_AddScalar;

				Generator::SetMul(res, false, m_pPts, kTmp);
			}
			else
				Generator::SetMul(res, bSet, m_pPts, k);
		}

		template <>
		void Obscured::Mul<Scalar::Native>::Assign(Point::Native& res, bool bSet) const
		{
			Scalar::Native k2;
			me.AssignInternal(res, bSet, k2, k);
		}

		template <>
		void Obscured::Mul<Scalar>::Assign(Point::Native& res, bool bSet) const
		{
			Scalar::Native k2;
			k2.Import(k); // don't care if overflown (still valid operation)
			me.AssignInternal(res, bSet, k2, k2);
		}

	} // namespace Generator

	/////////////////////
	// MultiMac
	void MultiMac::Prepared::Initialize(Oracle& oracle, Hash::Processor& hpRes)
	{
		Point::Native val;
		Generator::CreatePointNnz(val, oracle, &hpRes);
		Initialize(val, oracle);
	}

	void MultiMac::Prepared::Initialize(Point::Native& val, Oracle& oracle)
	{
		Point::Native npos = val, nums = val * Two;

		for (unsigned int i = 0; i < _countof(m_Fast.m_pPt); i++)
		{
			if (i)
				npos += nums;

			Generator::FromPt(m_Fast.m_pPt[i], npos);
		}

		while (true)
		{
			Generator::CreatePointNnz(nums, oracle, NULL);
			oracle >> m_Secure.m_Scalar;

			npos = nums;
			bool bOk = true;

			for (int i = 0; ; )
			{
				if (npos == Zero)
					bOk = false;
				Generator::FromPt(m_Secure.m_pPt[i], npos);

				if (++i == _countof(m_Secure.m_pPt))
					break;

				npos += val;
			}

			assert(Mode::Fast == g_Mode);
			MultiMac mm;

			const Prepared* ppPrep[] = { this };
			mm.m_ppPrepared = ppPrep;
			mm.m_pKPrep = &m_Secure.m_Scalar;
			FastAux aux;
			mm.m_pAuxPrepared = &aux;
			mm.m_Prepared = 1;

			mm.Calculate(npos);

			npos += nums;
			for (int i = ECC::nBits / Secure::nBits; --i; )
			{
				for (int j = Secure::nBits; j--; )
					nums = nums * Two;
				npos += nums;
			}

			if (npos == Zero)
				bOk = false;

			if (bOk)
			{
				npos = -npos;
				Generator::FromPt(m_Secure.m_Compensation, npos);
				break;
			}
		}
	}

	void MultiMac::Prepared::Assign(Point::Native& out, bool bSet) const
	{
		secp256k1_ge ge; // not secret
		Generator::ToPt(out, ge, m_Fast.m_pPt[0], bSet);
	}

	void MultiMac::Casual::Init(const Point::Native& p)
	{
		if (Mode::Fast == g_Mode)
		{
			m_nPrepared = 1;
			m_pPt[1] = p;
		}
		else
		{
			secp256k1_ge ge;
			Generator::ToPt(m_pPt[0], ge, Context::get().m_Casual.m_Nums, true);

			for (unsigned int i = 1; i < Secure::nCount; i++)
			{
				m_pPt[i] = m_pPt[i - 1];
				m_pPt[i] += p;
			}
		}
	}

	void MultiMac::Casual::Init(const Point::Native& p, const Scalar::Native& k)
	{
		Init(p);
		m_K = k;
	}

	void MultiMac::Reset()
	{
		m_Casual = 0;
		m_Prepared = 0;
	}

	unsigned int GetPortion(const Scalar::Native& k, unsigned int iWord, unsigned int iBitInWord, unsigned int nBitsWnd)
	{
		const Scalar::Native::uint& n = k.get().d[iWord];

		return (n >> (iBitInWord & ~(nBitsWnd - 1))) & ((1 << nBitsWnd) - 1);
	}


	void MultiMac::FastAux::Schedule(const Scalar::Native& k, unsigned int iBitsRemaining, unsigned int nMaxOdd, unsigned int* pTbl, unsigned int iThisEntry)
	{
		const Scalar::Native::uint* p = k.get().d;
		const uint32_t nWordBits = sizeof(*p) << 3;

		assert(1 & nMaxOdd); // must be odd
		unsigned int nVal = 0, nBitTrg = 0;

		while (iBitsRemaining--)
		{
			nVal <<= 1;
			if (nVal > nMaxOdd)
				break;

			uint32_t n = p[iBitsRemaining / nWordBits] >> (iBitsRemaining & (nWordBits - 1));

			if (1 & n)
			{
				nVal |= 1;
				m_nOdd = nVal;
				nBitTrg = iBitsRemaining;
			}
		}

		if (nVal > 0)
		{
			m_nNextItem = pTbl[nBitTrg];
			pTbl[nBitTrg] = iThisEntry;
		}
	}

	void MultiMac::Calculate(Point::Native& res) const
	{
		const unsigned int nBitsPerWord = sizeof(Scalar::Native::uint) << 3;

		static_assert(!(nBitsPerWord % Casual::Secure::nBits), "");
		static_assert(!(nBitsPerWord % Prepared::Secure::nBits), "");

		res = Zero;

		unsigned int pTblCasual[nBits];
		unsigned int pTblPrepared[nBits];

		if (Mode::Fast == g_Mode)
		{
			ZeroObject(pTblCasual);
			ZeroObject(pTblPrepared);

			for (int iEntry = 0; iEntry < m_Prepared; iEntry++)
				m_pAuxPrepared[iEntry].Schedule(m_pKPrep[iEntry], nBits, Prepared::Fast::nMaxOdd, pTblPrepared, iEntry + 1);

			for (int iEntry = 0; iEntry < m_Casual; iEntry++)
			{
				Casual& x = m_pCasual[iEntry];
				x.m_Aux.Schedule(x.m_K, nBits, Casual::Fast::nMaxOdd, pTblCasual, iEntry + 1);
			}

		}

		NoLeak<secp256k1_ge> ge;
		NoLeak<CompactPoint> ge_s;

		if (Mode::Secure == g_Mode)
			for (int iEntry = 0; iEntry < m_Prepared; iEntry++)
				m_pKPrep[iEntry] += m_ppPrepared[iEntry]->m_Secure.m_Scalar;

		for (unsigned int iBit = ECC::nBits; iBit--; )
		{
			if (!(res == Zero))
				res = res * Two;

			unsigned int iWord = iBit / nBitsPerWord;
			unsigned int iBitInWord = iBit & (nBitsPerWord - 1);

			if (Mode::Fast == g_Mode)
			{
				while (pTblCasual[iBit])
				{
					unsigned int iEntry = pTblCasual[iBit];
					Casual& x = m_pCasual[iEntry - 1];
					pTblCasual[iBit] = x.m_Aux.m_nNextItem;

					assert(1 & x.m_Aux.m_nOdd);
					unsigned int nElem = (x.m_Aux.m_nOdd >> 1) + 1;
					assert(nElem < Casual::Fast::nCount);

					for (; x.m_nPrepared < nElem; x.m_nPrepared++)
					{
						if (1 == x.m_nPrepared)
							x.m_pPt[0] = x.m_pPt[1] * Two;

						x.m_pPt[x.m_nPrepared + 1] = x.m_pPt[x.m_nPrepared] + x.m_pPt[0];
					}

					res += x.m_pPt[nElem];

					x.m_Aux.Schedule(x.m_K, iBit, Casual::Fast::nMaxOdd, pTblCasual, iEntry);
				}


				while (pTblPrepared[iBit])
				{
					unsigned int iEntry = pTblPrepared[iBit];
					FastAux& x = m_pAuxPrepared[iEntry - 1];
					pTblPrepared[iBit] = x.m_nNextItem;

					assert(1 & x.m_nOdd);
					unsigned int nElem = (x.m_nOdd >> 1);
					assert(nElem < Prepared::Fast::nCount);

					Generator::ToPt(res, ge.V, m_ppPrepared[iEntry - 1]->m_Fast.m_pPt[nElem], false);

					x.Schedule(m_pKPrep[iEntry - 1], iBit, Prepared::Fast::nMaxOdd, pTblPrepared, iEntry);
				}
			}
			else
			{
				// secure mode
				if (!(iBit & (Casual::Secure::nBits - 1)))
				{
					for (int iEntry = 0; iEntry < m_Casual; iEntry++)
					{
						Casual& x = m_pCasual[iEntry];

						unsigned int nVal = GetPortion(x.m_K, iWord, iBitInWord, Casual::Secure::nBits);

						//Point::Native ptVal;
						//for (unsigned int i = 0; i < Casual::Secure::nCount; i++)
						//	object_cmov(ptVal, x.m_pPt[nVal], i == nVal);
						//
						//res += ptVal;

						res += x.m_pPt[nVal]; // cmov seems not needed, since the table is relatively small, and not in global mem (less predicatble addresses)
						// The version with cmov (commented-out above) is ~15% slower
					}
				}

				if (!(iBit & (Prepared::Secure::nBits - 1)))
				{
					for (int iEntry = 0; iEntry < m_Prepared; iEntry++)
					{
						const Prepared::Secure& x = m_ppPrepared[iEntry]->m_Secure;

						unsigned int nVal = GetPortion(m_pKPrep[iEntry], iWord, iBitInWord, Prepared::Secure::nBits);

						for (unsigned int i = 0; i < _countof(x.m_pPt); i++)
							object_cmov(ge_s.V, x.m_pPt[i], i == nVal);

						Generator::ToPt(res, ge.V, ge_s.V, false);
					}
				}
			}
		}

		if (Mode::Secure == g_Mode)
		{
			for (int iEntry = 0; iEntry < m_Prepared; iEntry++)
			{
				const Prepared::Secure& x = m_ppPrepared[iEntry]->m_Secure;

				Generator::ToPt(res, ge.V, x.m_Compensation, false);
			}

			for (int iEntry = 0; iEntry < m_Casual; iEntry++)
				Generator::ToPt(res, ge.V, Context::get().m_Casual.m_Compensation, false);

		}
	}

	/////////////////////
	// ScalarGenerator
	void ScalarGenerator::Initialize(const Scalar::Native& x)
	{
		Scalar::Native pos = x;
		for (uint32_t iLevel = 0; ; )
		{
			PerLevel& lev = m_pLevel[iLevel];

			lev.m_pVal[0] = pos;
			for (uint32_t i = 1; i < _countof(lev.m_pVal); i++)
				lev.m_pVal[i] = lev.m_pVal[i - 1] * pos;

			if (++iLevel == nLevels)
				break;

			for (uint32_t i = 0; i < nBitsPerLevel; i++)
				secp256k1_scalar_sqr(&pos.get_Raw(), &pos.get());
		}
	}

	void ScalarGenerator::Calculate(Scalar::Native& trg, const Scalar& pwr) const
	{
		trg = 1U;

		const uint32_t nWordBits = sizeof(pwr.m_Value.m_pData[0]) << 3;
		const uint32_t nLevelsPerWord = nWordBits / nBitsPerLevel;
		static_assert(nLevelsPerWord * nBitsPerLevel == nWordBits, "");

		uint32_t iLevel = 0;
		for (uint32_t iWord = _countof(pwr.m_Value.m_pData); iWord--; )
		{
			unsigned int val = pwr.m_Value.m_pData[iWord];

			for (uint32_t j = 0; ; )
			{
				const PerLevel& lev = m_pLevel[iLevel++];

				unsigned int idx = val & _countof(lev.m_pVal);
				if (idx)
					trg *= lev.m_pVal[idx - 1];

				if (++j == nLevelsPerWord)
					break;

				val >>= nBitsPerLevel;
			}
		}
	}

	/////////////////////
	// Context
	alignas(64) char g_pContextBuf[sizeof(Context)];

	// Currently - auto-init in global obj c'tor
	Initializer g_Initializer;

#ifndef NDEBUG
	bool g_bContextInitialized = false;
#endif // NDEBUG

	const Context& Context::get()
	{
		assert(g_bContextInitialized);
		return *reinterpret_cast<Context*>(g_pContextBuf);
	}

	void InitializeContext()
	{
		Context& ctx = *reinterpret_cast<Context*>(g_pContextBuf);

		Mode::Scope scope(Mode::Fast);

		Oracle oracle;
		oracle << "Let the generator generation begin!";

		// make sure we get the same G,H for different generator kinds
		Point::Native G_raw, H_raw, J_raw;

		secp256k1_gej_set_ge(&G_raw.get_Raw(), &secp256k1_ge_const_g);
		Point ptG;
		Point::Native::ExportEx(ptG, secp256k1_ge_const_g);

		Hash::Processor hpRes;
		hpRes << ptG;

		Generator::CreatePointNnz(H_raw, oracle, &hpRes);
		Generator::CreatePointNnz(J_raw, oracle, &hpRes);


		ctx.G.Initialize(G_raw, oracle);
		ctx.H.Initialize(H_raw, oracle);
		ctx.H_Big.Initialize(H_raw, oracle);
		ctx.J.Initialize(J_raw, oracle);

		Point::Native pt, ptAux2(Zero);

		ctx.m_Ipp.G_.Initialize(G_raw, oracle);
		ctx.m_Ipp.H_.Initialize(H_raw, oracle);

		for (uint32_t i = 0; i < InnerProduct::nDim; i++)
		{
			for (uint32_t j = 0; j < 2; j++)
			{
				MultiMac::Prepared& p = ctx.m_Ipp.m_pGen_[j][i];
				p.Initialize(oracle, hpRes);

				if (1 == j)
				{
#ifdef ECC_COMPACT_GEN

					secp256k1_ge ge;
					secp256k1_ge_from_storage(&ge, &p.m_Fast.m_pPt[0]);
					secp256k1_ge_neg(&ge, &ge);
					secp256k1_ge_to_storage(&ctx.m_Ipp.m_pGet1_Minus[i], &ge);

#else // ECC_COMPACT_GEN

					pt = p;
					pt = -pt;
					Generator::FromPt(ctx.m_Ipp.m_pGet1_Minus[i], pt);

#endif // ECC_COMPACT_GEN
				}
				else
					ptAux2 += p;
			}
		}

		ptAux2 = -ptAux2;
		ctx.m_Ipp.m_Aux2_.Initialize(ptAux2, oracle);

		ctx.m_Ipp.m_GenDot_.Initialize(oracle, hpRes);

		const MultiMac::Prepared& genericNums = ctx.m_Ipp.m_GenDot_;
		ctx.m_Casual.m_Nums = genericNums.m_Fast.m_pPt[0]; // whatever

		{
			MultiMac_WithBufs<1, 1> mm;
			Scalar::Native& k = mm.m_Bufs.m_pKPrep[0];
			k = Zero;
			for (int i = ECC::nBits; i--; )
			{
				k = k + k;
				if (!(i % MultiMac::Casual::Secure::nBits))
					k = k + 1U;
			}

			k = -k;

			mm.m_Bufs.m_ppPrepared[0] = &ctx.m_Ipp.m_GenDot_;
			mm.m_Prepared = 1;

			mm.Calculate(pt);
			Generator::FromPt(ctx.m_Casual.m_Compensation, pt);
		}

		hpRes
			<< uint32_t(2) // increment this each time we change signature formula (rangeproof and etc.)
			>> ctx.m_hvChecksum;

#ifndef NDEBUG
		g_bContextInitialized = true;
#endif // NDEBUG
	}

	/////////////////////
	// Commitment
	void Commitment::Assign(Point::Native& res, bool bSet) const
	{
		(Context::get().G * k).Assign(res, bSet);
		res += Context::get().H * val;
	}

	/////////////////////
	// Tag
	namespace Tag {

		bool IsCustom(const Point::Native* pHGen)
		{
			return pHGen && !(*pHGen == Zero);
		}

		void AddValue(Point::Native& out, const Point::Native* pHGen, Amount v)
		{
			if (IsCustom(pHGen))
				out += *pHGen * v;
			else
				out += Context::get().H * v;
		}

	} // namespace Tag

	/////////////////////
	// Key::ID
	void Key::ID::get_Hash(Hash::Value& hv) const
	{
		Hash::Processor()
			<< "kid"
			<< m_Idx
			<< m_Type.V
			<< m_SubIdx
			>> hv;
	}

	void Key::ID::operator = (const Packed& x)
	{
		x.m_Idx.Export(m_Idx);
		x.m_Type.Export(m_Type.V);
		x.m_SubIdx.Export(m_SubIdx);
	}

	void Key::ID::Packed::operator = (const ID& x)
	{
		m_Idx = x.m_Idx;
		m_Type = x.m_Type.V;
		m_SubIdx = x.m_SubIdx;
	}

	void Key::IDV::operator = (const Packed& x)
	{
		ID::operator = (x);
		x.m_Value.Export(m_Value);
	}

	void Key::IDV::Packed::operator = (const IDV& x)
	{
		ID::Packed::operator = (x);
		m_Value = x.m_Value;
	}

	void Key::IKdf::DeriveKey(Scalar::Native& out, const Key::ID& kid)
	{
		Hash::Value hv; // the key hash is not secret
		kid.get_Hash(hv);
		DeriveKey(out, hv);
	}

	bool Key::IPKdf::IsSame(IPKdf& x)
	{
		Hash::Value hv = Zero;

		Scalar::Native k0, k1;
		DerivePKey(k0, hv);
		x.DerivePKey(k1, hv);

		k0 += -k1;
		return (k0 == Zero); // not secret, constant-time guarantee isn't requied
	}

	int Key::ID::cmp(const ID& x) const
	{
		if (m_Type < x.m_Type)
			return -1;
		if (m_Type > x.m_Type)
			return 1;
		if (m_SubIdx < x.m_SubIdx)
			return -1;
		if (m_SubIdx > x.m_SubIdx)
			return 1;
		if (m_Idx < x.m_Idx)
			return -1;
		if (m_Idx > x.m_Idx)
			return 1;
		return 0;
	}

	int Key::IDV::cmp(const IDV& x) const
	{
		int n = ID::cmp(x);
		if (n)
			return n;

		if (m_Value < x.m_Value)
			return -1;
		if (m_Value > x.m_Value)
			return 1;
		return 0;
	}

	/////////////////////
	// NonceGenerator
	void NonceGenerator::Reset()
	{
		ZeroObject(m_Context);
		m_bFirstTime = true;
		m_Counter = Zero;
	}

	void NonceGenerator::WriteIkm(const beam::Blob& b)
	{
		assert(m_bFirstTime);
		m_HMac.Write(b.p, b.n);
	}

	const Hash::Value& NonceGenerator::get_Okm()
	{
		if (m_bFirstTime)
			// Extract
			m_HMac >> m_Prk;

		// Expand
		m_HMac.Reset(m_Prk.m_pData, m_Prk.nBytes);

		if (m_bFirstTime)
			m_bFirstTime = false;
		else
			m_HMac.Write(m_Okm.m_pData, m_Okm.nBytes);

		m_HMac.Write(m_Context.p, m_Context.n);

		m_Counter.Inc();
		m_HMac.Write(m_Counter.m_pData, m_Counter.nBytes);

		m_HMac >> m_Okm;
		return m_Okm;
	}

	NonceGenerator& NonceGenerator::operator >> (Hash::Value& hv)
	{
		hv = get_Okm();
		return *this;
	}

	NonceGenerator& NonceGenerator::operator >> (Scalar::Native& sk)
	{
		static_assert(sizeof(Scalar) == sizeof(m_Okm), "");
		const Scalar& s = reinterpret_cast<const Scalar&>(m_Okm);

		do
			get_Okm();
		while (!sk.ImportNnz(s));

		return *this;
	}

	/////////////////////
	// HKdf
	HKdf::Generator::Generator()
	{
		m_Secret.V = Zero;
	}

	void HKdf::Generator::Generate(Scalar::Native& out, const Hash::Value& hv) const
	{
		NonceGenerator("beam-Key")
			<< m_Secret.V
			<< hv
			>> out;
	}

	HKdf::HKdf()
	{
		m_kCoFactor = 1U; // by default
	}

	HKdf::~HKdf()
	{
	}

	void HKdf::Generate(const Hash::Value& hv)
	{
		NonceGenerator nonceGen1("beam-HKdf");
		nonceGen1 << hv;
		NonceGenerator nonceGen2 = nonceGen1;

		nonceGen1.SetContext("gen") >> m_Generator.m_Secret.V;
		nonceGen2.SetContext("coF") >> m_kCoFactor;
	}

	void HKdf::Create(Ptr& pRes, const Hash::Value& hv)
	{
		std::shared_ptr<HKdf> pVal = std::make_shared<HKdf>();
		pVal->Generate(hv);
		pRes = std::move(pVal);
	}

	void HKdf::GenerateChild(Key::IKdf& kdf, Key::Index iKdf)
	{
		Scalar::Native sk;
		kdf.DeriveKey(sk, Key::ID(iKdf, Key::Type::ChildKey));

		NoLeak<Scalar> sk_;
		sk_.V = sk;
		Generate(sk_.V.m_Value);
	}

	void HKdf::CreateChild(Ptr& pRes, Key::IKdf& kdf, Key::Index iKdf)
	{
		std::shared_ptr<HKdf> pVal = std::make_shared<HKdf>();
		pVal->GenerateChild(kdf, iKdf);
		pRes = std::move(pVal);
	}

	void HKdf::DeriveKey(Scalar::Native& out, const Hash::Value& hv)
	{
		m_Generator.Generate(out, hv);
		out *= m_kCoFactor;
	}

	void HKdf::DerivePKey(Scalar::Native& out, const Hash::Value& hv)
	{
		m_Generator.Generate(out, hv);
	}

	void Key::IKdf::DerivePKeyG(Point::Native& out, const Hash::Value& hv)
	{
		Scalar::Native sk;
		DeriveKey(sk, hv);
		out = Context::get().G * sk;
	}

	void Key::IKdf::DerivePKeyJ(Point::Native& out, const Hash::Value& hv)
	{
		Scalar::Native sk;
		DeriveKey(sk, hv);
		out = Context::get().J * sk;
	}

	HKdfPub::HKdfPub()
	{
	}

	HKdfPub::~HKdfPub()
	{
	}

	void HKdfPub::DerivePKey(Scalar::Native& out, const Hash::Value& hv)
	{
		m_Generator.Generate(out, hv);
	}

	void HKdfPub::DerivePKeyG(Point::Native& out, const Hash::Value& hv)
	{
		Scalar::Native sk;
		m_Generator.Generate(sk, hv);
		out = m_PkG * sk;
	}

	void HKdfPub::DerivePKeyJ(Point::Native& out, const Hash::Value& hv)
	{
		Scalar::Native sk;
		m_Generator.Generate(sk, hv);
		out = m_PkJ * sk;
	}

	void HKdf::Export(Packed& v) const
	{
		v.m_Secret = m_Generator.m_Secret.V;
		v.m_kCoFactor = m_kCoFactor;
	}

	bool HKdf::Import(const Packed& v)
	{
		m_Generator.m_Secret.V = v.m_Secret;
		return !m_kCoFactor.Import(v.m_kCoFactor);
	}

	void HKdfPub::Export(Packed& v) const
	{
		v.m_Secret = m_Generator.m_Secret.V;
		v.m_PkG = m_PkG;
		v.m_PkJ = m_PkJ;
	}

	bool HKdfPub::Import(const Packed& v)
	{
		m_Generator.m_Secret.V = v.m_Secret;
		return
			m_PkG.ImportNnz(v.m_PkG) &&
			m_PkJ.ImportNnz(v.m_PkJ);
	}

	void HKdfPub::GenerateFrom(const HKdf& v)
	{
		m_Generator = v.m_Generator;
		m_PkG = Context::get().G * v.m_kCoFactor;
		m_PkJ = Context::get().J * v.m_kCoFactor;
	}

	/////////////////////
	// Oracle
	void Oracle::Reset()
	{
		m_hp.Reset();
	}

	void Oracle::operator >> (Hash::Value& out)
	{
		Hash::Processor(m_hp) >> out;
		operator << (out);
	}

	void Oracle::operator >> (Scalar::Native& out)
	{
		NoLeak<Scalar> s;

		do
			operator >> (s.V.m_Value);
		while (!out.ImportNnz(s.V));
	}

	/////////////////////
	// Signature
	void Signature::get_Challenge(Scalar::Native& out, const Point& pt, const Hash::Value& msg)
	{
		Oracle() << pt << msg >> out;
	}

	void Signature::MultiSig::SignPartial(Scalar::Native& k, const Hash::Value& msg, const Scalar::Native& sk) const
	{
		get_Challenge(k, m_NoncePub, msg);

		k *= sk;
		k += m_Nonce;
		k = -k;
	}

	void Signature::Sign(const Hash::Value& msg, const Scalar::Native& sk)
	{
		NonceGenerator nonceGen("beam-Schnorr");

		NoLeak<Scalar> s_;
		s_.V = sk;
		nonceGen << s_.V.m_Value;

		GenRandom(s_.V.m_Value); // add extra randomness to the nonce, so it's derived from both deterministic and random parts
		nonceGen << s_.V.m_Value;

		MultiSig msig;
		nonceGen >> msig.m_Nonce;
		msig.m_NoncePub = Context::get().G * msig.m_Nonce;

		Scalar::Native k;
		msig.SignPartial(k, msg, sk);

		m_NoncePub = msig.m_NoncePub;
		m_k = k;
	}

	bool Signature::IsValidPartial(const Hash::Value& msg, const Point::Native& pubNonce, const Point::Native& pk) const
	{
		Mode::Scope scope(Mode::Fast);

		Scalar::Native e;
		get_Challenge(e, m_NoncePub, msg);

		InnerProduct::BatchContext* pBc = InnerProduct::BatchContext::s_pInstance;
		if (pBc)
		{
			pBc->EquationBegin();

			pBc->AddPrepared(InnerProduct::BatchContext::s_Idx_G, m_k);
			pBc->AddCasual(pk, e);
			pBc->AddCasual(pubNonce, 1U);

			return true;
		}

		Point::Native pt = Context::get().G * m_k;

		pt += pk * e;
		pt += pubNonce;

		return pt == Zero;
	}

	bool Signature::IsValid(const Hash::Value& msg, const Point::Native& pk) const
	{
		Point::Native pubNonce;
		if (!pubNonce.Import(m_NoncePub))
			return false;

		return IsValidPartial(msg, pubNonce, pk);
	}

	int Signature::cmp(const Signature& x) const
	{
		int n = m_NoncePub.cmp(x.m_NoncePub);
		if (n)
			return n;

		return m_k.cmp(x.m_k);
	}

	/////////////////////
	// RangeProof
	namespace RangeProof
	{
		void get_PtMinusVal(Point::Native& out, const Point::Native& comm, Amount val, const Point::Native* pHGen)
		{
			out = comm;

			Point::Native ptAmount;
			Tag::AddValue(ptAmount, pHGen, val);

			ptAmount = -ptAmount;
			out += ptAmount;
		}

		// Public
		bool Public::IsValid(const Point::Native& comm, Oracle& oracle, const Point::Native* pHGen /* = nullptr */) const
		{
			Mode::Scope scope(Mode::Fast);

			if (m_Value < s_MinimumValue)
				return false;

			Point::Native pk;
			get_PtMinusVal(pk, comm, m_Value, pHGen);

			Hash::Value hv;
			get_Msg(hv, oracle);

			return m_Signature.IsValid(hv, pk);
		}

		void Public::Create(const Scalar::Native& sk, const CreatorParams& cp, Oracle& oracle)
		{
			m_Value = cp.m_Kidv.m_Value;
			assert(m_Value >= s_MinimumValue);

			m_Recovery.m_Kid = cp.m_Kidv;
			XCryptKid(m_Recovery.m_Kid, cp, m_Recovery.m_Checksum);

			Hash::Value hv;
			get_Msg(hv, oracle);

			m_Signature.Sign(hv, sk);
		}

		bool Public::Recover(CreatorParams& cp) const
		{
			Key::ID::Packed kid = m_Recovery.m_Kid;
			Hash::Value hvChecksum;
			XCryptKid(kid, cp, hvChecksum);

			if (!(m_Recovery.m_Checksum == hvChecksum))
				return false;

			Cast::Down<Key::ID>(cp.m_Kidv) = kid;
			cp.m_Kidv.m_Value = m_Value;
			return true;
		}

		int Public::cmp(const Public& x) const
		{
			int n = m_Signature.cmp(x.m_Signature);
			if (n)
				return n;

			n = memcmp(&m_Recovery, &x.m_Recovery, sizeof(m_Recovery));
			if (n)
				return n;

			if (m_Value < x.m_Value)
				return -1;
			if (m_Value > x.m_Value)
				return 1;

			return 0;
		}

		void Public::XCryptKid(Key::ID::Packed& kid, const CreatorParams& cp, Hash::Value& hvChecksum)
		{
			NonceGenerator nonceGen("beam-psig");
			nonceGen << cp.m_Seed.V;

			const Hash::Value& okm = nonceGen.get_Okm();
			static_assert(sizeof(okm) >= sizeof(kid), "");
			memxor(reinterpret_cast<uint8_t*>(&kid), okm.m_pData, sizeof(kid));

			nonceGen >> hvChecksum;
		}

		void Public::get_Msg(Hash::Value& hv, Oracle& oracle) const
		{
			oracle
				<< m_Value
				<< beam::Blob(&m_Recovery, sizeof(m_Recovery))
				>> hv;
		}

	} // namespace RangeProof

} // namespace ECC
