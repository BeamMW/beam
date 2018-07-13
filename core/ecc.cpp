#include "common.h"
#include "ecc_native.h"
#include <assert.h>

#define ENABLE_MODULE_GENERATOR
#define ENABLE_MODULE_RANGEPROOF

#if defined(__clang__) || defined(__GNUC__) || defined(__GNUG__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunused-function"
#else
    #pragma warning (push, 0) // suppress warnings from secp256k1
#endif

#include "../secp256k1-zkp/src/secp256k1.c"

#if defined(__clang__) || defined(__GNUC__) || defined(__GNUG__)
    #pragma GCC diagnostic pop
#else
    #pragma warning (pop)
#endif

#ifndef WIN32
#	include <unistd.h>
#	include <fcntl.h>
#endif // WIN32

// misc
bool memis0(const void* p, size_t n)
{
	for (size_t i = 0; i < n; i++)
		if (((const uint8_t*)p)[i])
			return false;
	return true;
}

namespace ECC {

	//void* NoErase(void*, size_t) { return NULL; }

	// Pointer to the 'eraser' function. The pointer should be non-const (i.e. variable that can be changed at run-time), so that optimizer won't remove this.
	void (*g_pfnEraseFunc)(void*, size_t) = memset0/*NoErase*/;

	void SecureErase(void* p, uint32_t n)
	{
		g_pfnEraseFunc(p, n);
	}

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

	char ChFromHex(uint8_t v)
	{
		return v + ((v < 10) ? '0' : ('a' - 10));
	}

	std::ostream& operator << (std::ostream& s, const uintBig& x)
	{
		const int nDigits = 8; // truncated
		static_assert(nDigits <= _countof(x.m_pData));

		char sz[nDigits * 2 + 1];

		for (int i = 0; i < nDigits; i++)
		{
			sz[i * 2] = ChFromHex(x.m_pData[i] >> 4);
			sz[i * 2 + 1] = ChFromHex(x.m_pData[i] & 0xf);
		}

		sz[_countof(sz) - 1] = 0;
		s << sz;

		return s;
	}

	std::ostream& operator << (std::ostream& s, const Scalar& x)
	{
		return operator << (s, x.m_Value);
	}

	std::ostream& operator << (std::ostream& s, const Point& x)
	{
		return operator << (s, x.m_X);
	}

	void GenRandom(void* p, uint32_t nSize)
	{
		bool bRet = false;

		// checkpoint?

#ifdef WIN32

		HCRYPTPROV hProv;
		if (CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_SCHANNEL, CRYPT_VERIFYCONTEXT))
		{
			if (CryptGenRandom(hProv, nSize, (uint8_t*)p))
				bRet = true;
			verify(CryptReleaseContext(hProv, 0));
		}

#else // WIN32

		int hFile = open("/dev/urandom", O_RDONLY);
		if (hFile >= 0)
		{
			if (read(hFile, p, nSize) == nSize)
				bRet = true;

			close(hFile);
		}

#endif // WIN32

		if (!bRet)
			std::ThrowIoError();
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
		return secp256k1_scalar_is_zero(this) != 0;
	}

	bool Scalar::Native::operator == (const Native& v) const
	{
		for (size_t i = 0; i < _countof(d); i++)
			if (d[i] != v.d[i])
				return false;
		return true;
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

	void Hash::Processor::Reset()
	{
		secp256k1_sha256_initialize(this);
	}

	void Hash::Processor::Write(const void* p, uint32_t n)
	{
		secp256k1_sha256_write(this, (const uint8_t*) p, n);
	}

	void Hash::Processor::Finalize(Value& v)
	{
		secp256k1_sha256_finalize(this, v.m_pData);
		*this << v;
	}

	void Hash::Processor::Write(const char* sz)
	{
		Write(sz, (uint32_t) (strlen(sz) + 1));
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

	void Hash::Processor::Write(const uintBig& v)
	{
		Write(v.m_pData, sizeof(v.m_pData));
	}

	void Hash::Processor::Write(const Scalar& v)
	{
		Write(v.m_Value);
	}

	void Hash::Processor::Write(const Scalar::Native& v)
	{
		NoLeak<Scalar> s;
		s.V = v;
		Write(s.V);
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

	bool Point::Native::ImportInternal(const Point& v)
	{
		NoLeak<secp256k1_fe> nx;
		if (!secp256k1_fe_set_b32(&nx.V, v.m_X.m_pData))
			return false;

		NoLeak<secp256k1_ge> ge;
		if (!secp256k1_ge_set_xo_var(&ge.V, &nx.V, false != v.m_Y))
			return false;

		secp256k1_gej_set_ge(this, &ge.V);

		return true;
	}

	bool Point::Native::Import(const Point& v)
	{
		if (ImportInternal(v))
			return true;

		*this = Zero;
		return memis0(&v, sizeof(v));
	}

	Point::Native& Point::Native::operator = (const Point& v)
	{
		Import(v);
		return *this;
	}

	bool Point::Native::Export(Point& v) const
	{
		if (*this == Zero)
		{
			v.m_X = Zero;
			v.m_Y = false;
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

		secp256k1_fe_get_b32(v.m_X.m_pData, &ge.V.x);
		v.m_Y = (secp256k1_fe_is_odd(&ge.V.y) != 0);

		return true;
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

		bool CreatePointNnz(Point::Native& out, const uintBig& x)
		{
			Point pt;
			pt.m_X = x;
			pt.m_Y = false;

			return out.Import(pt) && !(out == Zero);
		}

		bool CreatePointNnz(Point::Native& out, Hash::Processor& hp)
		{
			Hash::Value hv;
			hp >> hv;
			return CreatePointNnz(out, hv);
		}

		void CreatePointNnzFromSeed(Point::Native& out, const char* szSeed, Hash::Processor& hp)
		{
			for (hp << szSeed; ; )
				if (CreatePointNnz(out, hp))
					break;
		}

		bool CreatePts(CompactPoint* pPts, Point::Native& gpos, uint32_t nLevels, Hash::Processor& hp)
		{
			Point::Native nums, npos, pt;

			hp << "nums";
			if (!CreatePointNnz(nums, hp))
				return false;

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

		template <typename T>
		void data_cmov_as(T* pDst, const T* pSrc, int nWords, int flag)
		{
			const T mask0 = flag + ~((T)0);
			const T mask1 = ~mask0;

			for (int n = 0; n < nWords; n++)
				pDst[n] = (pDst[n] & mask0) | (pSrc[n] & mask1);
		}

		template <typename T>
		void object_cmov(T& dst, const T& src, int flag)
		{
			typedef uint32_t TOrd;
			static_assert(sizeof(T) % sizeof(TOrd) == 0, "");
			data_cmov_as<TOrd>((TOrd*) &dst, (TOrd*) &src, sizeof(T) / sizeof(TOrd), flag);
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

		void GeneratePts(const Point::Native& pt, Hash::Processor& hp, CompactPoint* pPts, uint32_t nLevels)
		{
			while (true)
			{
				Point::Native pt2 = pt;
				if (CreatePts(pPts, pt2, nLevels, hp))
					break;
			}
		}

		void Obscured::Initialize(const Point::Native& pt, Hash::Processor& hp)
		{
			while (true)
			{
				Point::Native pt2 = pt;
				if (!CreatePts(m_pPts, pt2, nLevels, hp))
					continue;

				hp << "blind-scalar";
				Scalar s0;
				hp >> s0.m_Value;
				if (m_AddScalar.Import(s0))
					continue;

				Generator::SetMul(pt2, true, m_pPts, m_AddScalar); // pt2 = G * blind
				FromPt(m_AddPt, pt2);

				m_AddScalar = -m_AddScalar;

				break;
			}
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
	void MultiMac::Prepared::Initialize(const char* szSeed, Hash::Processor& hp)
	{
		Point::Native val;

		for (hp << szSeed; ; )
			if (Generator::CreatePointNnz(val, hp))
			{
				Initialize(val, hp);
				break;
			}
	}

	void MultiMac::Prepared::Initialize(Point::Native& val, Hash::Processor& hp)
	{
		Generator::FromPt(m_Fast.m_pPt[0], val);
		Point::Native npos, nums = val;

		for (size_t i = 1; i < _countof(m_Fast.m_pPt); i++)
		{
			if (i & (i + 1))
				npos += val;
			else
			{
				nums = nums * Two;
				npos = nums;
			}
			Generator::FromPt(m_Fast.m_pPt[i], npos);
		}

		while (true)
		{
			Hash::Value hv;
			hp << "nums" >> hv;

			if (!Generator::CreatePointNnz(nums, hp))
				continue;

			hp << "blind-scalar";
			Scalar s0;
			hp >> s0.m_Value;
			if (m_Secure.m_Scalar.Import(s0))
				continue;

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

	void MultiMac::Casual::Init(const Point::Native& p)
	{
		if (Mode::Fast == g_Mode)
		{
			m_nPrepared = 2;
			m_pPt[1] = p;
		}
		else
		{
			secp256k1_ge ge;
			Generator::ToPt(m_pPt[0], ge, Context::get().m_Casual.m_Nums, true);

			for (size_t i = 1; i < _countof(m_pPt); i++)
			{
				m_pPt[i] = m_pPt[i - 1];
				m_pPt[i] += p;
			}

			m_nPrepared = _countof(m_pPt);
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

	void MultiMac::Calculate(Point::Native& res) const
	{
		const int nBitsPerWord = sizeof(Scalar::Native::uint) << 3;

		static_assert(Casual::nBits <= Prepared::Fast::nBits, "");
		static_assert(Casual::nBits <= Prepared::Secure::nBits, "");
		static_assert(!(nBitsPerWord % Casual::nBits), "");
		static_assert(!(nBitsPerWord % Prepared::Fast::nBits), "");
		static_assert(!(nBitsPerWord % Prepared::Secure::nBits), "");

		res = Zero;

		NoLeak<secp256k1_ge> ge;
		NoLeak<CompactPoint> ge_s;

		if (Mode::Secure == g_Mode)
			for (int iEntry = 0; iEntry < m_Prepared; iEntry++)
				m_pKPrep[iEntry] += m_ppPrepared[iEntry]->m_Secure.m_Scalar;

		for (int iWord = _countof(Scalar::Native().get().d); iWord--; )
		{
			for (int iLayer = nBitsPerWord / Casual::nBits; iLayer--; )
			{
				if (!(res == Zero))
					for (int i = 0; i < Casual::nBits; i++)
						res = res * Two;

				for (int iEntry = 0; iEntry < m_Casual; iEntry++)
				{
					Casual& x = m_pCasual[iEntry];
					const Scalar::Native::uint n = x.m_K.get().d[iWord];

					int nVal = (n >> (iLayer * Casual::nBits)) & (_countof(x.m_pPt) - 1);
					if (!nVal && (Mode::Fast == g_Mode))
						continue; // skip zero

					for (; x.m_nPrepared <= nVal; x.m_nPrepared++)
						if (x.m_nPrepared & (x.m_nPrepared - 1))
							x.m_pPt[x.m_nPrepared] = x.m_pPt[x.m_nPrepared - 1] + x.m_pPt[1];
						else
							x.m_pPt[x.m_nPrepared] = x.m_pPt[x.m_nPrepared >> 1] * Two;

					res += x.m_pPt[nVal];
				}

				if (Mode::Fast == g_Mode)
				{
					if (iLayer & (Prepared::Fast::nBits / Casual::nBits - 1))
						continue;

					int iLayerPrep = iLayer / (Prepared::Fast::nBits / Casual::nBits);

					for (int iEntry = 0; iEntry < m_Prepared; iEntry++)
					{
						const Prepared::Fast& x = m_ppPrepared[iEntry]->m_Fast;
						const Scalar::Native::uint n = m_pKPrep[iEntry].get().d[iWord];

						int nVal = (n >> (iLayerPrep * Prepared::Fast::nBits)) & ((1 << Prepared::Fast::nBits) - 1);
						if (nVal--)
							Generator::ToPt(res, ge.V, x.m_pPt[nVal], false);
					}
				}
				else
				{
					if (iLayer & (Prepared::Secure::nBits / Casual::nBits - 1))
						continue;

					int iLayerPrep = iLayer / (Prepared::Secure::nBits / Casual::nBits);

					for (int iEntry = 0; iEntry < m_Prepared; iEntry++)
					{
						const Prepared::Secure& x = m_ppPrepared[iEntry]->m_Secure;
						const Scalar::Native::uint n = m_pKPrep[iEntry].get().d[iWord];

						int nVal = (n >> (iLayerPrep * Prepared::Secure::nBits)) & ((1 << Prepared::Secure::nBits) - 1);

						for (size_t i = 0; i < _countof(x.m_pPt); i++)
							Generator::object_cmov(ge_s.V, x.m_pPt[i], i == nVal);

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
	// Context
	uint64_t g_pContextBuf[(sizeof(Context) + sizeof(uint64_t) - 1) / sizeof(uint64_t)];

#ifndef NDEBUG
	bool g_bContextInitialized = false;
#endif // NDEBUG

	const Context& Context::get()
	{
		assert(g_bContextInitialized);
		return *(Context*) g_pContextBuf;
	}

	void InitializeContext()
	{
		Context& ctx = *(Context*) g_pContextBuf;

		Mode::Scope scope(Mode::Fast);

		Hash::Processor hp;

		// make sure we get the same G,H for different generator kinds
		Point::Native G_raw, H_raw;
		Generator::CreatePointNnzFromSeed(G_raw, "G-gen", hp);
		Generator::CreatePointNnzFromSeed(H_raw, "H-gen", hp);


		ctx.G.Initialize(G_raw, hp);
		ctx.H.Initialize(H_raw, hp);
		ctx.H_Big.Initialize(H_raw, hp);

		Point::Native pt, ptAux2(Zero);

		ctx.m_Ipp.G_.Initialize(G_raw, hp);
		ctx.m_Ipp.H_.Initialize(H_raw, hp);

#define STR_GEN_PREFIX "ip-"
		char szStr[0x20] = STR_GEN_PREFIX;
		szStr[_countof(STR_GEN_PREFIX) + 2] = 0;

		for (uint32_t i = 0; i < InnerProduct::nDim; i++)
		{
			szStr[_countof(STR_GEN_PREFIX) - 1]	= '0' + (i / 10);
			szStr[_countof(STR_GEN_PREFIX)]		= '0' + (i % 10);

			for (uint32_t j = 0; j < 2; j++)
			{
				szStr[_countof(STR_GEN_PREFIX) + 1] = '0' + j;
				ctx.m_Ipp.m_pGen_[j][i].Initialize(szStr, hp);

				secp256k1_ge ge;

				if (1 == j)
				{
					Generator::ToPt(pt, ge, ctx.m_Ipp.m_pGen_[j][i].m_Fast.m_pPt[0], true);
					pt = -pt;
					Generator::FromPt(ctx.m_Ipp.m_pGet1_Minus[i], pt);
				} else
					Generator::ToPt(ptAux2, ge, ctx.m_Ipp.m_pGen_[j][i].m_Fast.m_pPt[0], false);
			}
		}

		ptAux2 = -ptAux2;
		hp << "aux2";
		ctx.m_Ipp.m_Aux2_.Initialize(ptAux2, hp);

		ctx.m_Ipp.m_GenDot_.Initialize("ip-dot", hp);

		const MultiMac::Prepared& genericNums = ctx.m_Ipp.m_GenDot_;
		ctx.m_Casual.m_Nums = genericNums.m_Fast.m_pPt[0]; // whatever

		{
			MultiMac_WithBufs<1, 1> mm;
			Scalar::Native& k = mm.m_Bufs.m_pKPrep[0];
			k = Zero;
			for (int i = ECC::nBits; i--; )
			{
				k = k + k;
				if (!(i % MultiMac::Casual::nBits))
					k = k + 1U;
			}

			k = -k;

			mm.m_Bufs.m_ppPrepared[0] = &ctx.m_Ipp.m_GenDot_;
			mm.m_Prepared = 1;

			mm.Calculate(pt);
			Generator::FromPt(ctx.m_Casual.m_Compensation, pt);
		}

		hp << uint32_t(0); // increment this each time we change signature formula (rangeproof and etc.)

		hp >> ctx.m_hvChecksum;

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
	// Nonce and key generation
	template <>
	void uintBig::GenerateNonce(const uintBig& sk, const uintBig& msg, const uintBig* pMsg2, uint32_t nAttempt /* = 0 */)
	{
		for (uint32_t i = 0; ; i++)
		{
			if (!nonce_function_rfc6979(m_pData, msg.m_pData, sk.m_pData, NULL, pMsg2 ? (void*) pMsg2->m_pData : NULL, i))
				continue;

			if (!nAttempt--)
				break;
		}
	}

	void Scalar::Native::GenerateNonce(const uintBig& sk, const uintBig& msg, const uintBig* pMsg2, uint32_t nAttempt /* = 0 */)
	{
		NoLeak<Scalar> s;

		for (uint32_t i = 0; ; i++)
		{
			s.V.m_Value.GenerateNonce(sk, msg, pMsg2, i);
			if (Import(s.V))
				continue;

			if (!nAttempt--)
				break;
		}
	}

	void Kdf::DeriveKey(Scalar::Native& out, uint64_t nKeyIndex, uint32_t nFlags, uint32_t nExtra) const
	{
		// the msg hash is not secret
		Hash::Value hv;
		Hash::Processor() << nKeyIndex << nFlags << nExtra >> hv;
		out.GenerateNonce(m_Secret.V, hv, NULL);
	}

	/////////////////////
	// Oracle
	void Oracle::Reset()
	{
		m_hp.Reset();
	}

	void Oracle::operator >> (Scalar::Native& out)
	{
		Scalar s; // not secret

		do
			m_hp >> s.m_Value;
		while (out.Import(s));
	}

	/////////////////////
	// Signature
	void Signature::get_Challenge(Scalar::Native& out, const Point::Native& pt, const Hash::Value& msg)
	{
		Oracle() << pt << msg >> out;
	}

	void Signature::MultiSig::GenerateNonce(const Hash::Value& msg, const Scalar::Native& sk)
	{
		NoLeak<Scalar> sk_;
		sk_.V = sk;

		m_Nonce.GenerateNonce(sk_.V.m_Value, msg, NULL);
	}

	void Signature::CoSign(Scalar::Native& k, const Hash::Value& msg, const Scalar::Native& sk, const MultiSig& msig)
	{
		get_Challenge(k, msig.m_NoncePub, msg);
		m_e = k;

		k *= sk;
		k = -k;
		k += msig.m_Nonce;
	}

	void Signature::Sign(const Hash::Value& msg, const Scalar::Native& sk)
	{
		MultiSig msig;
		msig.GenerateNonce(msg, sk);
		msig.m_NoncePub = Context::get().G * msig.m_Nonce;

		Scalar::Native k;
		CoSign(k, msg, sk, msig);
		m_k = k;
	}

	void Signature::get_PublicNonce(Point::Native& pubNonce, const Point::Native& pk) const
	{
		Mode::Scope scope(Mode::Fast);

		pubNonce = Context::get().G * m_k;
		pubNonce += pk * m_e;
	}

	bool Signature::IsValidPartial(const Point::Native& pubNonce, const Point::Native& pk) const
	{
		Point::Native pubN;
		get_PublicNonce(pubN, pk);

		pubN = -pubN;
		pubN += pubNonce;
		return pubN == Zero;
	}

	bool Signature::IsValid(const Hash::Value& msg, const Point::Native& pk) const
	{
		Point::Native pubNonce;
		get_PublicNonce(pubNonce, pk);

		Scalar::Native e2;

		get_Challenge(e2, pubNonce, msg);

		return m_e == Scalar(e2);
	}

	int Signature::cmp(const Signature& x) const
	{
		int n = m_e.cmp(x.m_e);
		if (n)
			return n;

		return m_k.cmp(x.m_k);
	}

	/////////////////////
	// RangeProof
	namespace RangeProof
	{
		void get_PtMinusVal(Point::Native& out, const Point::Native& comm, Amount val)
		{
			out = comm;

			Point::Native ptAmount = Context::get().H * val;

			ptAmount = -ptAmount;
			out += ptAmount;
		}

		// Public
		bool Public::IsValid(const Point::Native& comm, Oracle& oracle) const
		{
			Mode::Scope scope(Mode::Fast);

			if (m_Value < s_MinimumValue)
				return false;

			Point::Native pk;
			get_PtMinusVal(pk, comm, m_Value);

			Hash::Value hv;
			oracle << m_Value >> hv;

			return m_Signature.IsValid(hv, pk);
		}

		void Public::Create(const Scalar::Native& sk, Oracle& oracle)
		{
			assert(m_Value >= s_MinimumValue);
			Hash::Value hv;
			oracle << m_Value >> hv;

			m_Signature.Sign(hv, sk);
		}

		int Public::cmp(const Public& x) const
		{
			int n = m_Signature.cmp(x.m_Signature);
			if (n)
				return n;

			if (m_Value < x.m_Value)
				return -1;
			if (m_Value > x.m_Value)
				return 1;

			return 0;
		}


	} // namespace RangeProof


	/////////////////////
	// InnerProduct
	struct InnerProduct::Calculator
	{
		struct ChallengeSet {
			Scalar::Native m_DotMultiplier;
			Scalar::Native m_Val[nCycles][2];
		};

		struct ModifierExpanded
		{
			Scalar::Native m_pPwr[2][nDim];
			bool m_pUse[2];

			void Init(const Modifier& mod)
			{
				for (size_t j = 0; j < _countof(mod.m_pMultiplier); j++)
				{
					m_pUse[j] = (NULL != mod.m_pMultiplier[j]);
					if (m_pUse[j])
					{
						m_pPwr[j][0] = 1U;
						for (uint32_t i = 1; i < nDim; i++)
							m_pPwr[j][i] = m_pPwr[j][i - 1] * *(mod.m_pMultiplier[j]);
					}
				}
			}

			void Set(Scalar::Native& dst, const Scalar::Native& src, int i, int j) const
			{
				if (m_pUse[j])
					dst = src * m_pPwr[j][i];
				else
					dst = src;
			}
		};

		static void get_Challenge(Scalar::Native* pX, Oracle&);

		struct Aggregator
		{
			MultiMac& m_Mm;
			const ChallengeSet& m_cs;
			const ModifierExpanded& m_Mod;
			const Calculator* m_pCalc; // set if source are already condensed points
			const int m_j;
			const int m_iCycleTrg;

			Aggregator(MultiMac& mm, const ChallengeSet& cs, const ModifierExpanded& mod, int j, int iCycleTrg)
				:m_Mm(mm)
				,m_cs(cs)
				,m_Mod(mod)
				,m_pCalc(NULL)
				,m_j(j)
				,m_iCycleTrg(iCycleTrg)
			{
			}

			void Proceed(uint32_t iPos, uint32_t iCycle, const Scalar::Native& k);
		};

		static const uint32_t s_iCycle0 = 2; // condense source generators into points (after 3 iterations, 8 points)

		Point::Native m_pGen[2][nDim >> (1 + s_iCycle0)];
		Scalar::Native m_pVal[2][nDim >> 1];

		const Scalar::Native* m_ppSrc[2];

		ModifierExpanded m_Mod;
		ChallengeSet m_Cs;

		MultiMac_WithBufs<(nDim >> (s_iCycle0 + 1)), nDim * 2> m_Mm;

		uint32_t m_iCycle;
		uint32_t m_n;
		uint32_t m_GenOrder;

		void Condense();
		void ExtractLR(int j);
	};


	void InnerProduct::Calculator::get_Challenge(Scalar::Native* pX, Oracle& oracle)
	{
		do
			oracle >> pX[0];
		while (pX[0] == Zero);

		pX[1].SetInv(pX[0]);
	}

	void InnerProduct::Calculator::Condense()
	{
		// Vectors
		for (int j = 0; j < 2; j++)
			for (uint32_t i = 0; i < m_n; i++)
			{
				// dst and src need not to be distinct
				m_pVal[j][i] = m_ppSrc[j][i] * m_Cs.m_Val[m_iCycle][j];
				m_pVal[j][i] += m_ppSrc[j][m_n + i] * m_Cs.m_Val[m_iCycle][!j];
			}

		// Points
		switch (m_iCycle)
		{
		case s_iCycle0:
			// further compression points (casual)
			// Currently according to benchmarks - not necessary
			break;

		case nCycles - 1: // last iteration - no need to condense points
		default:
			return;
		}

		for (int j = 0; j < 2; j++)
			for (uint32_t i = 0; i < m_n; i++)
			{
				m_Mm.Reset();

				Point::Native& g0 = m_pGen[j][i];

				Aggregator aggr(m_Mm, m_Cs, m_Mod, j, nCycles - m_iCycle - 1);

				if (m_iCycle > s_iCycle0)
					aggr.m_pCalc = this;

				aggr.Proceed(i, m_GenOrder, 1U);

				m_Mm.Calculate(g0);
			}

		m_GenOrder = nCycles - m_iCycle - 1;
	}

	void InnerProduct::Calculator::ExtractLR(int j)
	{
		m_Mm.Reset();

		// Cross-term
		Scalar::Native& crossTrm = m_Mm.m_pKPrep[m_Mm.m_Prepared];
		m_Mm.m_ppPrepared[m_Mm.m_Prepared++] = &Context::get().m_Ipp.m_GenDot_;

		crossTrm = Zero;

		for (uint32_t i = 0; i < m_n; i++)
			crossTrm += m_ppSrc[j][i] * m_ppSrc[!j][m_n + i];

		crossTrm *= m_Cs.m_DotMultiplier;

		// other
		for (int jSrc = 0; jSrc < 2; jSrc++)
		{
			uint32_t off0 = (jSrc == j) ? 0 : m_n;
			uint32_t off1 = (jSrc == j) ? m_n : 0;

			for (uint32_t i = 0; i < m_n; i++)
			{
				const Scalar::Native& v = m_ppSrc[jSrc][i + off0];

				Aggregator aggr(m_Mm, m_Cs, m_Mod, jSrc, nCycles - m_iCycle);

				if (m_iCycle > s_iCycle0)
					aggr.m_pCalc = this;

				aggr.Proceed(i + off1, m_GenOrder, v);
			}
		}
	}

	void InnerProduct::Calculator::Aggregator::Proceed(uint32_t iPos, uint32_t iCycle, const Scalar::Native& k)
	{
		if (iCycle != m_iCycleTrg)
		{
			assert(iCycle <= nCycles);
			Scalar::Native k0 = k;
			k0 *= m_cs.m_Val[nCycles - iCycle][!m_j];

			Proceed(iPos, iCycle - 1, k0);

			k0 = k;
			k0 *= m_cs.m_Val[nCycles - iCycle][m_j];

			uint32_t nStep = 1 << (iCycle - 1);

			Proceed(iPos + nStep, iCycle - 1, k0);

		} else
		{
			if (m_pCalc)
			{
				assert(iPos < _countof(m_pCalc->m_pGen[m_j]));
				m_Mm.m_pCasual[m_Mm.m_Casual++].Init(m_pCalc->m_pGen[m_j][iPos], k);
			}
			else
			{
				assert(iPos < nDim);

				m_Mod.Set(m_Mm.m_pKPrep[m_Mm.m_Prepared], k, iPos, m_j);
				m_Mm.m_ppPrepared[m_Mm.m_Prepared++] = &Context::get().m_Ipp.m_pGen_[m_j][iPos];
			}
		}
	}

	void InnerProduct::get_Dot(Scalar::Native& res, const Scalar::Native* pA, const Scalar::Native* pB)
	{
		static_assert(nDim, "");
		res = pA[0];
		res *= pB[0];

		Scalar::Native tmp;

		for (uint32_t i = 1; i < nDim; i++)
		{
			tmp = pA[i];
			tmp *= pB[i];
			res += tmp;
		}
	}

	void InnerProduct::Create(Point::Native& commAB, const Scalar::Native& dotAB, const Scalar::Native* pA, const Scalar::Native* pB, const Modifier& mod)
	{
		Mode::Scope scope(Mode::Fast);

		Calculator c;
		c.m_Mod.Init(mod);
		c.m_GenOrder = nCycles;
		c.m_ppSrc[0] = pA;
		c.m_ppSrc[1] = pB;

		for (int j = 0; j < 2; j++)
			for (uint32_t i = 0; i < nDim; i++, c.m_Mm.m_Prepared++)
			{
				c.m_Mm.m_ppPrepared[c.m_Mm.m_Prepared] = &Context::get().m_Ipp.m_pGen_[j][i];
				c.m_Mod.Set(c.m_Mm.m_pKPrep[c.m_Mm.m_Prepared], c.m_ppSrc[j][i], i, j);
			}

		Point::Native comm;
		c.m_Mm.Calculate(commAB);

		Oracle oracle;
		oracle << commAB << dotAB >> c.m_Cs.m_DotMultiplier;

		for (c.m_iCycle = 0; c.m_iCycle < nCycles; c.m_iCycle++)
		{
			c.m_n = nDim >> (c.m_iCycle + 1);

			Calculator::get_Challenge(c.m_Cs.m_Val[c.m_iCycle], oracle);

			for (int j = 0; j < 2; j++)
			{
				c.ExtractLR(j);

				c.m_Mm.Calculate(comm);
				m_pLR[c.m_iCycle][j] = comm;
				oracle << m_pLR[c.m_iCycle][j];
			}

			c.Condense();

			if (!c.m_iCycle)
				for (int j = 0; j < 2; j++)
					c.m_ppSrc[j] = c.m_pVal[j];
		}

		for (int i = 0; i < 2; i++)
			m_pCondensed[i] = c.m_pVal[i][0];
	}

	bool InnerProduct::IsValid(const Point::Native& commAB, const Scalar::Native& dotAB, const Modifier& mod) const
	{
		Mode::Scope scope(Mode::Fast);

		Oracle oracle;
		Calculator::ChallengeSet cs;

		oracle << commAB << dotAB >> cs.m_DotMultiplier;

		// Calculate the aggregated sum, consisting of sum of multiplications at once.
		// The expression we're calculating is:
		//
		// - sum( LR[iCycle][0] * k[iCycle]^2 + LR[iCycle][0] * k[iCycle]^-2 )

		Calculator::ModifierExpanded modExp;
		modExp.Init(mod);

		MultiMac_WithBufs<nCycles * 2, nDim * 2 + 1> mm;
		Point::Native p;

		uint32_t n = nDim;
		for (uint32_t iCycle = 0; iCycle < nCycles; iCycle++, n >>= 1)
		{
			Calculator::get_Challenge(cs.m_Val[iCycle], oracle);

			const Point* pLR = m_pLR[iCycle];
			for (int j = 0; j < 2; j++)
			{
				MultiMac::Casual& x = mm.m_pCasual[mm.m_Casual++];
				if (!p.Import(pLR[j]))
					return false;
				x.Init(p);

				x.m_K = cs.m_Val[iCycle][j];
				x.m_K *= x.m_K;
				x.m_K = -x.m_K;
			}

			oracle << pLR[0] << pLR[1];
		}

		assert(1 == n);

		// The expression we're calculating is: the transformed generator
		//
		// sum( G_Condensed[j] * pCondensed[j] )
		// whereas G_Condensed[j] = Gen[j] * sum (k[iCycle]^(+/-)2 ), i.e. transformed (condensed) generators

		for (int j = 0; j < 2; j++)
		{
			Calculator::Aggregator aggr(mm, cs, modExp, j, 0);
			aggr.Proceed(0, nCycles, m_pCondensed[j]);
		}

		// add the new (mutated) dot product, substract the original (claimed)
		mm.m_pKPrep[mm.m_Prepared] = m_pCondensed[0];
		mm.m_pKPrep[mm.m_Prepared] *= m_pCondensed[1];
		mm.m_pKPrep[mm.m_Prepared] += -dotAB;
		mm.m_pKPrep[mm.m_Prepared] *= cs.m_DotMultiplier;

		mm.m_ppPrepared[mm.m_Prepared++] = &Context::get().m_Ipp.m_GenDot_;

		// Calculate all at-once
		Point::Native res;
		mm.Calculate(res);

		// verify. Should be equal to AB
		res = -res;
		res += commAB;

		return res == Zero;
	}

	struct NonceGenerator
	{
		NoLeak<Oracle> m_Oracle;
		NoLeak<Scalar> m_sk;

		void operator >> (Scalar::Native& k)
		{
			NoLeak<Hash::Value> hv;
			m_Oracle.V >> hv.V;

			k.GenerateNonce(m_sk.V.m_Value, hv.V, NULL);
		}
	};

	/////////////////////
	// Bulletproof
	void RangeProof::Confidential::Create(const Scalar::Native& sk, Amount v, Oracle& oracle)
	{
		verify(CoSign(sk, v, oracle, Phase::SinglePass));
	}

	struct RangeProof::Confidential::MultiSig
	{
		Scalar::Native m_tau1;
		Scalar::Native m_tau2;

		void Init(NonceGenerator&);
		void Init(const Scalar::Native& sk, Amount v);

		void AddInfo1(Point::Native& ptT1, Point::Native& ptT2) const;
		void AddInfo2(Scalar::Native& taux, const Scalar::Native& sk, const ChallengeSet&) const;
	};

	struct RangeProof::Confidential::ChallengeSet
	{
		Scalar::Native x, y, z, zz;
		void Init(const Part1&, Oracle&);
		void Init(const Part2&, Oracle&);
	};

	bool RangeProof::Confidential::CoSign(const Scalar::Native& sk, Amount v, Oracle& oracle, Phase::Enum ePhase)
	{
		NonceGenerator nonceGen;
		nonceGen.m_sk.V = sk;
		nonceGen.m_Oracle.V << v;

		// A = G*alpha + vec(aL)*vec(G) + vec(aR)*vec(H)
		Scalar::Native alpha;
		nonceGen >> alpha;

		Point::Native comm = Context::get().G * alpha;

		{
			NoLeak<secp256k1_ge> ge;
			NoLeak<CompactPoint> ge_s;

			for (uint32_t i = 0; i < InnerProduct::nDim; i++)
			{
				uint32_t iBit = 1 & (v >> i);

				// protection against side-channel attacks
				Generator::object_cmov(ge_s.V, Context::get().m_Ipp.m_pGet1_Minus[i], 0 == iBit);
				Generator::object_cmov(ge_s.V, Context::get().m_Ipp.m_pGen_[0][i].m_Fast.m_pPt[0], 1 == iBit);

				Generator::ToPt(comm, ge.V, ge_s.V, false);
			}
		}

		m_Part1.m_A = comm;

		// S = G*ro + vec(sL)*vec(G) + vec(sR)*vec(H)
		Scalar::Native ro;
		nonceGen >> ro;

		MultiMac_WithBufs<1, InnerProduct::nDim * 2 + 1> mm;
		mm.m_pKPrep[mm.m_Prepared] = ro;
		mm.m_ppPrepared[mm.m_Prepared++] = &Context::get().m_Ipp.G_;

		Scalar::Native pS[2][InnerProduct::nDim];

		for (int j = 0; j < 2; j++)
			for (uint32_t i = 0; i < InnerProduct::nDim; i++)
			{
				nonceGen >> pS[j][i];

				mm.m_pKPrep[mm.m_Prepared] = pS[j][i];
				mm.m_ppPrepared[mm.m_Prepared++] = &Context::get().m_Ipp.m_pGen_[j][i];
			}

		mm.Calculate(comm);

		m_Part1.m_S = comm;

		//if (Phase::Step1 == ePhase)
		//	return; // stop after A,S calculated

		// get challenges
		ChallengeSet cs;
		cs.Init(m_Part1, oracle);

		// calculate t1, t2 - parts of vec(L)*vec(R) which depend on (future) x and x^2.
		Scalar::Native t0(Zero), t1(Zero), t2(Zero);

		Scalar::Native l0, r0, rx, one(1U), two(2U), yPwr, zz_twoPwr;

		yPwr = one;
		zz_twoPwr = cs.zz;

		for (uint32_t i = 0; i < InnerProduct::nDim; i++)
		{
			uint32_t bit = 1 & (v >> i);

			l0 = -cs.z;
			if (bit)
				l0 += one;

			const Scalar::Native& lx = pS[0][i];

			r0 = cs.z;
			if (!bit)
				r0 += -one;

			r0 *= yPwr;
			r0 += zz_twoPwr;

			rx = yPwr;
			rx *= pS[1][i];

			zz_twoPwr *= two;
			yPwr *= cs.y;

			t0 += l0 * r0;
			t1 += l0 * rx;
			t1 += lx * r0;
			t2 += lx * rx;
		}

		MultiSig msig;
		msig.Init(nonceGen);

		if (Phase::Finalize != ePhase) // otherwise m_Part2 already contains the whole aggregate
		{
			Point::Native comm2;
			msig.AddInfo1(comm, comm2);

			comm += Context::get().H_Big * t1;
			comm2 += Context::get().H_Big * t2;

			if (Phase::SinglePass != ePhase)
			{
				Point::Native p;
				if (!p.Import(m_Part2.m_T1))
					return false;
				comm += p;

				if (!p.Import(m_Part2.m_T2))
					return false;
				comm2 += p;
			}

			m_Part2.m_T1 = comm;
			m_Part2.m_T2 = comm2;
		}

		if (Phase::Step2 == ePhase)
			return true; // stop after T1,T2 calculated

		cs.Init(m_Part2, oracle); // get challenge 

		// m_TauX = tau2*x^2 + tau1*x + sk*z^2
		msig.AddInfo2(l0, sk, cs);

		if (Phase::SinglePass != ePhase)
			l0 += m_Part3.m_TauX;

		m_Part3.m_TauX = l0;

		// m_Mu = alpha + ro*x
		l0 = ro;
		l0 *= cs.x;
		l0 += alpha;
		m_Mu = l0;

		// m_tDot
		l0 = t0;

		r0 = t1;
		r0 *= cs.x;
		l0 += r0;

		r0 = t2;
		r0 *= cs.x;
		r0 *= cs.x;
		l0 += r0;

		m_tDot = l0;

		// construct vectors l,r, use buffers pS
		// P - m_Mu*G
		yPwr = one;
		zz_twoPwr = cs.zz;

		for (uint32_t i = 0; i < InnerProduct::nDim; i++)
		{
			uint32_t bit = 1 & (v >> i);

			pS[0][i] *= cs.x;

			pS[0][i] += -cs.z;
			if (bit)
				pS[0][i] += one;

			pS[1][i] *= cs.x;
			pS[1][i] *= yPwr;

			r0 = cs.z;
			if (!bit)
				r0 += -one;

			r0 *= yPwr;
			r0 += zz_twoPwr;

			pS[1][i] += r0;

			zz_twoPwr *= two;
			yPwr *= cs.y;
		}

		yPwr.SetInv(cs.y);

		InnerProduct::Modifier mod;
		mod.m_pMultiplier[1] = &yPwr;

		m_P_Tag.Create(comm, l0, pS[0], pS[1], mod);

		return true;
	}

	void RangeProof::Confidential::MultiSig::Init(const Scalar::Native& sk, Amount v)
	{
		NonceGenerator nonceGen;
		nonceGen.m_sk.V = sk;
		nonceGen.m_Oracle.V << v;

		Init(nonceGen);
	}

	void RangeProof::Confidential::MultiSig::Init(NonceGenerator& nonceGen)
	{
		nonceGen >> m_tau1;
		nonceGen >> m_tau2;
	}

	void RangeProof::Confidential::MultiSig::AddInfo1(Point::Native& ptT1, Point::Native& ptT2) const
	{
		ptT1 = Context::get().G * m_tau1;
		ptT2 = Context::get().G * m_tau2;
	}

	void RangeProof::Confidential::MultiSig::AddInfo2(Scalar::Native& taux, const Scalar::Native& sk, const ChallengeSet& cs) const
	{
		// m_TauX = tau2*x^2 + tau1*x + sk*z^2
		taux = m_tau2;
		taux *= cs.x;
		taux *= cs.x;

		Scalar::Native t1 = m_tau1;
		t1 *= cs.x;
		taux += t1;

		t1 = cs.zz;
		t1 *= sk; // UTXO blinding factor (or part of it in case of multi-sig)
		taux += t1;
	}

	void RangeProof::Confidential::CoSignPart(const Scalar::Native& sk, Amount v, Oracle&, Part2& p2)
	{
		MultiSig msig;
		msig.Init(sk, v);

		Point::Native ptT1, ptT2;
		msig.AddInfo1(ptT1, ptT2);
		p2.m_T1 = ptT1;
		p2.m_T2 = ptT2;
	}

	void RangeProof::Confidential::CoSignPart(const Scalar::Native& sk, Amount v, Oracle& oracle, const Part1& p1, const Part2& p2, Part3& p3)
	{
		MultiSig msig;
		msig.Init(sk, v);

		ChallengeSet cs;
		cs.Init(p1, oracle);
		cs.Init(p2, oracle);

		Scalar::Native taux;
		msig.AddInfo2(taux, sk, cs);

		p3.m_TauX = taux;
	}

	void RangeProof::Confidential::ChallengeSet::Init(const Part1& p1, Oracle& oracle)
	{
		oracle << p1.m_A << p1.m_S;
		oracle >> y;
		oracle >> z;

		zz = z;
		zz *= z;
	}

	void RangeProof::Confidential::ChallengeSet::Init(const Part2& p2, Oracle& oracle)
	{
		oracle << p2.m_T1 << p2.m_T2;
		oracle >> x;
	}

	bool RangeProof::Confidential::IsValid(const Point::Native& commitment, Oracle& oracle) const
	{
		Mode::Scope scope(Mode::Fast);

		ChallengeSet cs;
		cs.Init(m_Part1, oracle);
		cs.Init(m_Part2, oracle);

		Scalar::Native xx, zz, tDot;

		// calculate delta(y,z) = (z - z^2) * sumY - z^3 * sum2
		Scalar::Native delta, sum2, sumY;


		sum2 = 1U;
		sumY = Zero;
		for (uint32_t i = 0; i < InnerProduct::nDim; i++)
		{
			sumY += sum2;
			sum2 *= cs.y;
		}

		sum2 = Amount(-1);

		zz = cs.z * cs.z;

		delta = cs.z;
		delta += -zz;
		delta *= sumY;

		sum2 *= zz;
		sum2 *= cs.z;
		delta += -sum2;

		// H_Big * m_tDot + G * m_TauX =?= commitment * z^2 + H_Big * delta(y,z) + m_T1*x + m_T2*x^2
		// H_Big * (m_tDot - delta(y,z)) + G * m_TauX =?= commitment * z^2 + m_T1*x + m_T2*x^2


		xx = cs.x * cs.x;

		MultiMac_WithBufs<3, InnerProduct::nDim + 2> mm;

		mm.m_pCasual[mm.m_Casual++].Init(commitment, -zz);

		Point::Native p;
		if (!p.Import(m_Part2.m_T1))
			return false;
		mm.m_pCasual[mm.m_Casual++].Init(p, -cs.x);

		if (!p.Import(m_Part2.m_T2))
			return false;
		mm.m_pCasual[mm.m_Casual++].Init(p, -xx);

		tDot = m_tDot;
		sumY = tDot;
		sumY += -delta;

		mm.m_pKPrep[mm.m_Prepared] = m_Part3.m_TauX;
		mm.m_ppPrepared[mm.m_Prepared++] = &Context::get().m_Ipp.G_;

		mm.m_pKPrep[mm.m_Prepared] = sumY;
		mm.m_ppPrepared[mm.m_Prepared++] = &Context::get().m_Ipp.H_;

		Point::Native ptVal;
		mm.Calculate(ptVal);

		if (!(ptVal == Zero))
			return false;

		mm.Reset();

		// (P - m_Mu*G) + m_Mu*G =?= m_A + m_S*x - vec(G)*vec(z) + vec(H)*( vec(z) + vec(z^2*2^n*y^-n) )
		mm.m_pKPrep[mm.m_Prepared] = cs.z;
		mm.m_ppPrepared[mm.m_Prepared++] = &Context::get().m_Ipp.m_Aux2_;

		mm.m_pKPrep[mm.m_Prepared] = m_Mu;
		mm.m_pKPrep[mm.m_Prepared] = -mm.m_pKPrep[mm.m_Prepared];
		mm.m_ppPrepared[mm.m_Prepared++] = &Context::get().m_Ipp.G_;

		if (!p.Import(m_Part1.m_S))
			return false;
		mm.m_pCasual[mm.m_Casual++].Init(p, cs.x);

		Scalar::Native yInv, pwr, mul;
		yInv.SetInv(cs.y);

		mul = 2U;
		mul *= yInv;
		pwr = zz;

		for (uint32_t i = 0; i < InnerProduct::nDim; i++)
		{
			sum2 = pwr;
			sum2 += cs.z;

			mm.m_pKPrep[mm.m_Prepared] = sum2;
			mm.m_ppPrepared[mm.m_Prepared++] = &Context::get().m_Ipp.m_pGen_[1][i];

			pwr *= mul;
		}

		mm.Calculate(ptVal);

		if (!p.Import(m_Part1.m_A))
			return false;
		ptVal += p;

		// By now the ptVal should be equal to the commAB
		// finally check the inner product
		InnerProduct::Modifier mod;
		mod.m_pMultiplier[1] = &yInv;
		if (!m_P_Tag.IsValid(ptVal, tDot, mod))
			return false;

		return true;
	}

	int RangeProof::Confidential::cmp(const Confidential& x) const
	{
		// don't care
		return memcmp(this, &x, sizeof(*this));
	}

} // namespace ECC

// Needed for test
void secp256k1_ecmult_gen(const secp256k1_context* pCtx, secp256k1_gej *r, const secp256k1_scalar *a)
{
	secp256k1_ecmult_gen(&pCtx->ecmult_gen_ctx, r, a);
}
