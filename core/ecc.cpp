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

	thread_local PseudoRandomGenerator* PseudoRandomGenerator::s_pOverride = nullptr;

	PseudoRandomGenerator::PseudoRandomGenerator()
	{
		ZeroObject(*this);
	}

	void PseudoRandomGenerator::Generate(void* p, uint32_t nSize)
	{
		while (true)
		{
			if (!m_Remaining)
			{
				Hash::Processor() << m_hv >> m_hv;
				m_Remaining = m_hv.nBytes;
			}

			if (m_Remaining >= nSize)
			{
				memcpy(p, m_hv.m_pData + m_hv.nBytes - m_Remaining, nSize);
				m_Remaining -= nSize;
				break;
			}

			memcpy(p, m_hv.m_pData + m_hv.nBytes - m_Remaining, m_Remaining);

			((uint8_t*&) p) += m_Remaining;
			nSize -= m_Remaining;

			m_Remaining = 0;
		}
	}

	void GenRandom(void* p, uint32_t nSize)
	{
		// checkpoint?

		if (PseudoRandomGenerator::s_pOverride)
		{
			PseudoRandomGenerator::s_pOverride->Generate(p, nSize);
			return;
		}

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

	std::string Scalar::str() const
	{
		return m_Value.str();
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

	void Hash::Processor::FinalizeTruncated(uint8_t* p, uint32_t nSize)
	{
		assert(nSize < Value::nBytes);

		Value hv;
		Finalize(hv);

		memcpy(p, hv.m_pData, nSize);
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

	bool Point::Native::ImportNnz(const Point& v, Storage* pS /* = nullptr */)
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

		if (pS)
			pS->FromNnz(ge.V);

		return true;
	}

	void Point::Storage::FromNnz(secp256k1_ge& ge)
	{
		// normalization seems unnecessary, but it's minor
		secp256k1_fe_normalize(&ge.x);
		secp256k1_fe_normalize(&ge.y);

		secp256k1_fe_get_b32(m_X.m_pData, &ge.x);
		secp256k1_fe_get_b32(m_Y.m_pData, &ge.y);
	}

	bool Point::Native::Import(const Point& v, Storage* pS /* = nullptr */)
	{
		if (ImportNnz(v, pS))
			return true;

		*this = Zero;
		if (pS)
			ZeroObject(*pS);

		return memis0(&v, sizeof(v));
	}

	void Point::Native::ExportNnz(secp256k1_ge& ge) const
	{
		NoLeak<secp256k1_gej> dup;
		dup.V = *this;
		secp256k1_ge_set_gej(&ge, &dup.V);
	}

	bool Point::Native::Export(Point& v) const
	{
		if (*this == Zero)
		{
			ZeroObject(v);
			return false;
		}

		NoLeak<secp256k1_ge> ge;
		ExportNnz(ge.V);

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

	void Point::Native::Export(Storage& v) const
	{
		if (*this == Zero)
			ZeroObject(v);
		else
		{
			secp256k1_ge ge;
			ExportNnz(ge);
			v.FromNnz(ge);
		}
	}

	bool Point::Native::Import(const Storage& v, bool bVerify)
	{
		if (memis0(&v, sizeof(v)))
			*this = Zero;
		else
		{
			secp256k1_ge ge;
			ZeroObject(ge);

			secp256k1_fe_set_b32(&ge.x, v.m_X.m_pData);
			secp256k1_fe_set_b32(&ge.y, v.m_Y.m_pData);

			if (bVerify && !secp256k1_ge_is_valid_var(&ge))
			{
				*this = Zero;
				return false;
			}

			secp256k1_gej_set_ge(this, &ge);
		}

		return true;
	}

	void Point::Native::BatchNormalizer::Normalize()
	{
		secp256k1_fe zDenom;
		NormalizeInternal(zDenom, true);
	}

	void Point::Native::BatchNormalizer::ToCommonDenominator(secp256k1_fe& zDenom)
	{
		secp256k1_fe_set_int(&zDenom, 1);
		NormalizeInternal(zDenom, false);
	}

	void secp256k1_gej_rescale_XY(secp256k1_gej& gej, const secp256k1_fe& z)
	{
		// equivalent of secp256k1_gej_rescale, but doesn't change z coordinate
		// A bit more effective when the value of z is know in advance (such as when normalizing)
		secp256k1_fe zz;
		secp256k1_fe_sqr(&zz, &z);

		secp256k1_fe_mul(&gej.x, &gej.x, &zz);
		secp256k1_fe_mul(&gej.y, &gej.y, &zz);
		secp256k1_fe_mul(&gej.y, &gej.y, &z);
	}

	void Point::Native::BatchNormalizer::NormalizeInternal(secp256k1_fe& zDenom, bool bNormalize)
	{
		bool bEmpty = true;
		Element elPrev = { 0 }; // init not necessary, just suppress the warning
		Element el = { 0 };

		for (Reset(); ; )
		{
			if (!MoveNext(el))
				break;

			if (bEmpty)
			{
				bEmpty = false;
				*el.m_pFe = el.m_pPoint->z;
			}
			else
				secp256k1_fe_mul(el.m_pFe, elPrev.m_pFe, &el.m_pPoint->z);

			elPrev = el;
		}

		if (bEmpty)
			return;

		if (bNormalize)
			secp256k1_fe_inv(&zDenom, elPrev.m_pFe); // the only expensive call

		while (true)
		{
			bool bFetched = MovePrev(el);
			if (bFetched) 
				secp256k1_fe_mul(elPrev.m_pFe, el.m_pFe, &zDenom);
			else
				*elPrev.m_pFe = zDenom;

			secp256k1_fe_mul(&zDenom, &zDenom, &elPrev.m_pPoint->z);

			secp256k1_gej_rescale_XY(*elPrev.m_pPoint, *elPrev.m_pFe);
			elPrev.m_pPoint->z = zDenom;

			if (!bFetched)
				break;

			elPrev = el;
		}
	}

	void Point::Native::BatchNormalizer::get_As(secp256k1_ge& ge, const Point::Native& ptNormalized)
	{
		ge.x = ptNormalized.x;
		ge.y = ptNormalized.y;
		ge.infinity = ptNormalized.infinity;
	}

	void Point::Native::BatchNormalizer::get_As(secp256k1_ge_storage& ge_s, const Point::Native& ptNormalized)
	{
		secp256k1_ge ge;
		get_As(ge, ptNormalized);
		secp256k1_ge_to_storage(&ge_s, &ge);
	}

	void Point::Native::BatchNormalizer_Arr::get_At(Element& el, uint32_t iIdx)
	{
		el.m_pPoint = m_pPts + iIdx;
		el.m_pFe = m_pFes + iIdx;
	}

	void Point::Native::BatchNormalizer_Arr::Reset()
	{
		m_iIdx = 0;
	}

	bool Point::Native::BatchNormalizer_Arr::MoveNext(Element& el)
	{
		if (m_iIdx == m_Size)
			return false;

		get_At(el, m_iIdx);
		m_iIdx++;
		return true;
	}

	bool Point::Native::BatchNormalizer_Arr::MovePrev(Element& el)
	{
		if (m_iIdx < 2)
			return false;

		m_iIdx--;
		get_At(el, m_iIdx - 1);
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

    bool Point::Native::operator != (Zero_ v) const
    {
        return !(operator == (v));
    }

	bool Point::Native::operator == (const Native& v) const
	{
		Native val = -v;
		val += *this;
		return val == Zero;
	}

	bool Point::Native::operator == (const Point& v) const
	{
		// There's a fast way to compare the X coordinate (without normalizing the gej).
		// But there is no known way to check the oddity of the Y coordinate
		// Hence - we'll do the normalization.

		// Import/Export seems to be the same complexity
		ECC::Point v2;
		Export(v2);
		return v2 == v;
	}

	bool Point::operator == (const Native& pt) const
	{
		return pt == *this;
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
		mc.Init(v.x);

		MultiMac mm;
		mm.m_pCasual = &mc;
		mm.m_Casual = 1;
		mm.m_pKCasual = Cast::NotConst(&v.y);
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

	Point::Compact::Converter::Converter()
	{
		m_Batch.m_Size = 0;
	}

	void Point::Compact::Converter::Flush()
	{
		m_Batch.Normalize();

		for (uint32_t i = 0; i < m_Batch.m_Size; i++)
			m_Batch.get_As(*m_ppC[i], m_Batch.m_pPts[i]);

		m_Batch.m_Size = 0;
	}

	void Point::Compact::Converter::set_Deferred(Compact& trg, Point::Native& src)
	{
		if (m_Batch.m_Size == N)
			Flush();

		m_ppC[m_Batch.m_Size] = &trg;
		m_Batch.m_pPts[m_Batch.m_Size] = src;
		m_Batch.m_Size++;
	}

	void Point::Compact::Assign(secp256k1_ge& ge) const
	{
		secp256k1_ge_from_storage(&ge, this);
	}

	void Point::Compact::Assign(Point::Native& p, bool bSet) const
	{
		NoLeak<secp256k1_ge> ge;
		Assign(ge.V);

		if (bSet)
			secp256k1_gej_set_ge(&p.get_Raw(), &ge.V);
		else
		{
			if (Mode::Secure == ECC::g_Mode)
				secp256k1_gej_add_ge(&p.get_Raw(), &p.get_Raw(), &ge.V);
			else
				secp256k1_gej_add_ge_var(&p.get_Raw(), &p.get_Raw(), &ge.V, nullptr);
		}
	}

	/////////////////////
	// Generator
	namespace Generator
	{
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

		bool CreatePts(Point::Compact* pPts, Point::Native& gpos, uint32_t nLevels, Oracle& oracle, Point::Compact::Converter& cpc)
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

					cpc.set_Deferred(*pPts++, pt);

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

		void SetMul(Point::Native& res, bool bSet, const Point::Compact* pPts, const Scalar::Native::uint* p, int nWords)
		{
			static_assert(8 % nBitsPerLevel == 0, "");
			const int nLevelsPerWord = (sizeof(Scalar::Native::uint) << 3) / nBitsPerLevel;
			static_assert(!(nLevelsPerWord & (nLevelsPerWord - 1)), "should be power-of-2");

			NoLeak<Point::Compact> ge_s;

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

					const Point::Compact* pSel;
					if (Mode::Secure == g_Mode)
					{
						pSel = &ge_s.V;
						for (uint32_t i = 0; i < nPointsPerLevel; i++)
							object_cmov(ge_s.V, pPts[i], i == nSel);
					}
					else
						pSel = pPts + nSel;

					if (bSet)
					{
						bSet = false;
						res = *pSel;
					}
					else
						res += *pSel;
				}
			}
		}

		void SetMul(Point::Native& res, bool bSet, const Point::Compact* pPts, const Scalar::Native& k)
		{
			SetMul(res, bSet, pPts, k.get().d, _countof(k.get().d));
		}

		void GeneratePts(const Point::Native& pt, Oracle& oracle, Point::Compact* pPts, uint32_t nLevels, Point::Compact::Converter& cpc)
		{
			while (true)
			{
				Point::Native pt2 = pt;
				if (CreatePts(pPts, pt2, nLevels, oracle, cpc))
					break;
			}
		}

		void Obscured::Initialize(const Point::Native& pt, Oracle& oracle, Point::Compact::Converter& cpc)
		{
			Point::Native pt2;
			while (true)
			{
				pt2 = pt;
				if (CreatePts(m_pPts, pt2, nLevels, oracle, cpc))
					break;
			}

			oracle >> m_AddScalar;

			cpc.Flush(); // using self

			Generator::SetMul(pt2, true, m_pPts, m_AddScalar); // pt2 = G * blind
			cpc.set_Deferred(m_AddPt, pt2);

			m_AddScalar = -m_AddScalar;
		}

		void Obscured::AssignInternal(Point::Native& res, bool bSet, Scalar::Native& kTmp, const Scalar::Native& k) const
		{
			if (Mode::Secure == g_Mode)
			{
				m_AddPt.Assign(res, bSet);

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
	void MultiMac::Prepared::Initialize(Oracle& oracle, Hash::Processor& hpRes, Point::Compact::Converter& cpc)
	{
		Point::Native val;
		Generator::CreatePointNnz(val, oracle, &hpRes);
		Initialize(val, oracle, cpc);
	}

	void MultiMac::Prepared::Initialize(Point::Native& val, Oracle& oracle, Point::Compact::Converter& cpc)
	{
		Point::Native npos = val, nums = val * Two;

		for (unsigned int i = 0; i < _countof(m_Fast.m_pPt); i++)
		{
			if (i)
				npos += nums;

			cpc.set_Deferred(m_Fast.m_pPt[i], npos);
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

				cpc.set_Deferred(m_Secure.m_pPt[i], npos);

				if (++i == _countof(m_Secure.m_pPt))
					break;

				npos += val;
			}

			cpc.Flush(); // we use fast data in secure init

			assert(Mode::Fast == g_Mode);
			MultiMac mm;

			const Prepared* ppPrep[] = { this };
			mm.m_ppPrepared = ppPrep;
			mm.m_pKPrep = &m_Secure.m_Scalar;
			Prepared::Fast::Wnaf wnaf;
			mm.m_pWnafPrepared = &wnaf;
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
				cpc.set_Deferred(m_Secure.m_Compensation, npos);
				break;
			}
		}
	}

	void MultiMac::Prepared::Assign(Point::Native& out, bool bSet) const
	{
		m_Fast.m_pPt[0].Assign(out, bSet);
	}

	void MultiMac::Casual::Init(const Point::Native& p)
	{
		if (Mode::Fast == g_Mode)
		{
			Fast& f = U.F.get();
			f.m_pPt[0] = p;
		}
		else
		{
			Secure& s = U.S.get();
			s.m_pPt[0] = Context::get().m_Casual.m_Nums;

			for (unsigned int i = 1; i < Secure::nCount; i++)
			{
				s.m_pPt[i] = s.m_pPt[i - 1];
				s.m_pPt[i] += p;
			}
		}
	}

	void MultiMac::Reset()
	{
		m_Casual = 0;
		m_Prepared = 0;
		m_ReuseFlag = Reuse::None;
	}

	unsigned int GetPortion(const Scalar::Native& k, unsigned int iWord, unsigned int iBitInWord, unsigned int nBitsWnd)
	{
		const Scalar::Native::uint& n = k.get().d[iWord];

		return (n >> (iBitInWord & ~(nBitsWnd - 1))) & ((1 << nBitsWnd) - 1);
	}

	struct MultiMac::WnafBase::Context
	{
		const Scalar::Native::uint* m_p;
		unsigned int m_Flag;
		unsigned int m_iBit;
		unsigned int m_Carry;

		unsigned int NextOdd();

		void OnBit(unsigned int& res, unsigned int x)
		{
			if (x)
			{
				res |= m_Flag;
				m_Carry = 0;
			}
		}
	};

	unsigned int MultiMac::WnafBase::Context::NextOdd()
	{
		const unsigned int nWordBits = sizeof(*m_p) << 3;
		const unsigned int nMsk = nWordBits - 1;

		unsigned int res = 0;

		while (true)
		{
			if (m_iBit >= ECC::nBits)
			{
				OnBit(res, m_Carry);

				if (!res)
					break;
			}
			else
			{
				unsigned int n = m_p[m_iBit / nWordBits] >> (m_iBit & nMsk);
				OnBit(res, (1 & n) != m_Carry);
			}

			m_iBit++;
			res >>= 1;

			if (1 & res)
				break;
		}

		return res;
	}

	void MultiMac::WnafBase::Shared::Reset()
	{
		ZeroObject(m_pTable);
	}

	unsigned int MultiMac::WnafBase::Shared::Add(Entry* pTrg, const Scalar::Native& k, unsigned int nWndBits, WnafBase& wnaf, unsigned int iElement)
	{
		const unsigned int nWndConsume = nWndBits + 1;

		Context ctx;
		ctx.m_p = k.get().d;
		ctx.m_iBit = 0;
		ctx.m_Carry = 0;
		ctx.m_Flag = 1 << nWndConsume;

		unsigned int iEntry = 0;

		for ( ; ; iEntry++)
		{
			unsigned int nOdd = ctx.NextOdd();
			if (!nOdd)
				break;

			assert(!ctx.m_Carry);
			assert(1 & nOdd);
			assert(!(nOdd >> nWndConsume));

			assert(ctx.m_iBit >= nWndConsume);
			unsigned int iIdx = ctx.m_iBit - nWndConsume;
			assert(iIdx < _countof(m_pTable));

			Entry& x = pTrg[iEntry];
			x.m_iBit = static_cast<uint16_t>(iIdx);

			if (nOdd >> nWndBits)
			{
				// hi bit is ON
				nOdd ^= (1 << nWndConsume) - 2;

				assert(1 & nOdd);
				assert(!(nOdd >> nWndBits));

				x.m_Odd = static_cast<int16_t>(nOdd) | Entry::s_Negative;

				ctx.m_Carry = 1;
			}
			else
				x.m_Odd = static_cast<int16_t>(nOdd);
		}

		unsigned int ret = iEntry--;
		if (ret)
		{
			// add the highest bit
			const Entry& x = pTrg[iEntry];
			Link& lnk = m_pTable[x.m_iBit];
			
			wnaf.m_Next = lnk;
			lnk.m_iElement = iElement;
			lnk.m_iEntry = iEntry;
		}

		return ret;
	}

	unsigned int MultiMac::WnafBase::Shared::Fetch(unsigned int iBit, WnafBase& wnaf, const Entry* pE, bool& bNeg)
	{
		Link& lnkTop = m_pTable[iBit]; // alias
		Link lnkThis = lnkTop;

		const Entry& e = pE[lnkThis.m_iEntry];
		assert(e.m_iBit == iBit);

		lnkTop = wnaf.m_Next; // pop this entry

		if (lnkThis.m_iEntry)
		{
			// insert next entry
			lnkThis.m_iEntry--;

			const Entry& e2 = pE[lnkThis.m_iEntry];
			assert(e2.m_iBit < iBit);

			Link& lnkTop2 = m_pTable[e2.m_iBit];
			wnaf.m_Next = lnkTop2;
			lnkTop2 = lnkThis;
		}

		unsigned int nOdd = e.m_Odd;
		assert(1 & nOdd);

		bNeg = !!(e.m_Odd & e.s_Negative);

		if (bNeg)
			nOdd &= ~e.s_Negative;

		return nOdd;
	}

	struct MultiMac::Normalizer
		:public Point::Native::BatchNormalizer
	{
		const MultiMac& m_This;

		Normalizer(const MultiMac& mm) :m_This(mm) {}

		struct Cursor
		{
			int m_iElement;
			uint32_t m_iEntry;
		};

		Cursor m_Cursor;

		void FromCursor(Element& el, const Cursor& cu) const;
		bool MoveBkwd(Cursor& cu) const;

		virtual void Reset() override;
		virtual bool MoveNext(Element& el) override;

		virtual bool MovePrev(Element& el) override;
	};

	void MultiMac::Normalizer::FromCursor(Element& el, const Cursor& cu) const
	{
		Casual& x = m_This.m_pCasual[cu.m_iElement];
		Casual::Fast& f = x.U.F.get();

		el.m_pPoint = f.m_pPt + cu.m_iEntry;
		el.m_pFe = f.m_pFe + cu.m_iEntry;
	}

	bool MultiMac::Normalizer::MoveBkwd(Cursor& cu) const
	{
		while (true)
		{
			if (cu.m_iElement < m_This.m_Casual)
			{
				Casual& x = m_This.m_pCasual[cu.m_iElement];
				Casual::Fast& f = x.U.F.get();

				assert(cu.m_iEntry <= f.m_nNeeded);
				f; // suppress warning in release build

				if (cu.m_iEntry)
				{
					cu.m_iEntry--;
					break;
				}
			}

			if (!cu.m_iElement)
				return false;

			cu.m_iElement--;

			Casual& x = m_This.m_pCasual[cu.m_iElement];
			Casual::Fast& f = x.U.F.get();

			cu.m_iEntry = f.m_nNeeded;
		}

		return true;
	}

	void MultiMac::Normalizer::Reset()
	{
		ZeroObject(m_Cursor);
	}

	bool MultiMac::Normalizer::MoveNext(Element& el)
	{
		while (true)
		{
			if (m_Cursor.m_iElement == m_This.m_Casual)
				return false;

			Casual& x = m_This.m_pCasual[m_Cursor.m_iElement];
			Casual::Fast& f = x.U.F.get();

			if (m_Cursor.m_iEntry < f.m_nNeeded)
				break;

			m_Cursor.m_iElement++;
			m_Cursor.m_iEntry = 0;

		}

		FromCursor(el, m_Cursor);
		m_Cursor.m_iEntry++;

		return true;
	}

	bool MultiMac::Normalizer::MovePrev(Element& el)
	{
		Cursor cu1 = m_Cursor;
		if (!MoveBkwd(cu1))
			return false;

		Cursor cu2 = cu1;
		if (!MoveBkwd(cu2))
			return false;

		m_Cursor = cu1;
		FromCursor(el, cu2);
		return true;
	}

	void MultiMac::Calculate(Point::Native& res) const
	{
		const unsigned int nBitsPerWord = sizeof(Scalar::Native::uint) << 3;

		static_assert(!(nBitsPerWord % Casual::Secure::nBits), "");
		static_assert(!(nBitsPerWord % Prepared::Secure::nBits), "");

		res = Zero;

		NoLeak<secp256k1_ge> ge;
		NoLeak<Point::Compact> ge_s;
		secp256k1_fe zDenom;
		bool bDenomSet = false;

		WnafBase::Shared wsP, wsC;

		unsigned int iBit = ECC::nBits;

		if (Mode::Fast == g_Mode)
		{
			iBit++; // extra bit may be necessary because of interleaving
			assert(iBit == _countof(wsP.m_pTable));

			wsP.Reset();
			wsC.Reset();

			for (int iEntry = 0; iEntry < m_Prepared; iEntry++)
			{
				unsigned int nEntries = m_pWnafPrepared[iEntry].Init(wsP, m_pKPrep[iEntry], iEntry + 1);
				assert(nEntries <= _countof(m_pWnafPrepared[iEntry].m_pVals));
				nEntries; // suppress warning in release build
			}

			for (int iEntry = 0; iEntry < m_Casual; iEntry++)
			{
				Casual& x = m_pCasual[iEntry];
				Casual::Fast& f = x.U.F.get();

				Point::Native& pt = f.m_pPt[0];
				if (pt == Zero)
				{
					f.m_nNeeded = 0;
					continue;
				}

				unsigned int nEntries = f.m_Wnaf.Init(wsC, m_pKCasual[iEntry], iEntry + 1);
				assert(nEntries <= _countof(f.m_Wnaf.m_pVals));

				if (Reuse::UseGenerated == m_ReuseFlag)
				{
					if (!bDenomSet)
					{
						bDenomSet = true;
						zDenom = pt.get_Raw().z;
					}

					continue;
				}

				if (Reuse::Generate == m_ReuseFlag)
					f.m_nNeeded = Casual::Fast::nCount; // all
				else
				{
					// Find highest needed element, calculate all the needed ones
					f.m_nNeeded = 0;
					for (unsigned int i = 0; i < nEntries; i++)
					{
						const WnafBase::Entry& e = f.m_Wnaf.m_pVals[i];

						unsigned int nOdd = e.m_Odd & ~e.s_Negative;
						assert(nOdd & 1);

						unsigned int nElem = (nOdd >> 1);
						std::setmax(f.m_nNeeded, nElem + 1);
					}
					assert(f.m_nNeeded <= Casual::Fast::nCount);

					if (f.m_nNeeded <= 1)
						continue;
				}

				// Calculate all the needed elements
				Point::Native ptX2 = pt * Two;

				for (uint32_t nPrepared = 1; nPrepared < f.m_nNeeded; nPrepared++)
					f.m_pPt[nPrepared] = f.m_pPt[nPrepared - 1] + ptX2;
			}

			if (Reuse::UseGenerated == m_ReuseFlag)
			{
				if (!bDenomSet)
					secp256k1_fe_set_int(&zDenom, 1);
			}
			else
			{
				// Bring everything to the same denominator
				Normalizer nrm(*this);
				nrm.ToCommonDenominator(zDenom);
			}
		}
		else
		{
			for (int iEntry = 0; iEntry < m_Prepared; iEntry++)
				m_pKPrep[iEntry] += m_ppPrepared[iEntry]->m_Secure.m_Scalar;
		}

		while (iBit--)
		{
			if (!(res == Zero))
				res = res * Two;

			if (Mode::Fast == g_Mode)
			{
				WnafBase::Link& lnkC = wsC.m_pTable[iBit]; // alias
				while (lnkC.m_iElement)
				{
					Casual& x = m_pCasual[lnkC.m_iElement - 1];
					Casual::Fast& f = x.U.F.get();
					Casual::Fast::Wnaf& wnaf = f.m_Wnaf;

					bool bNeg;
					unsigned int nOdd = wnaf.Fetch(wsC, iBit, bNeg);

					unsigned int nElem = (nOdd >> 1);
					assert(nElem < f.m_nNeeded);

					Point::Native::BatchNormalizer::get_As(ge.V, f.m_pPt[nElem]);

					if (bNeg)
						secp256k1_ge_neg(&ge.V, &ge.V);

					secp256k1_gej_add_ge_var(&res.get_Raw(), &res.get_Raw(), &ge.V, nullptr);
				}

				WnafBase::Link& lnkP = wsP.m_pTable[iBit]; // alias
				while (lnkP.m_iElement)
				{
					unsigned int iElement = lnkP.m_iElement - 1;

					Prepared::Fast::Wnaf& wnaf = m_pWnafPrepared[iElement];

					bool bNeg;
					unsigned int nOdd = wnaf.Fetch(wsP, iBit, bNeg);

					unsigned int nElem = (nOdd >> 1);
					assert(nElem < Prepared::Fast::nCount);

					const Point::Compact& ptC = m_ppPrepared[iElement]->m_Fast.m_pPt[nElem];

					secp256k1_ge_from_storage(&ge.V, &ptC);

					if (bNeg)
						secp256k1_ge_neg(&ge.V, &ge.V);

					secp256k1_gej_add_zinv_var(&res.get_Raw(), &res.get_Raw(), &ge.V, &zDenom);
				}
			}
			else
			{
				unsigned int iWord = iBit / nBitsPerWord;
				unsigned int iBitInWord = iBit & (nBitsPerWord - 1);

				// secure mode
				if (!(iBit & (Casual::Secure::nBits - 1)))
				{
					for (int iEntry = 0; iEntry < m_Casual; iEntry++)
					{
						Casual& x = m_pCasual[iEntry];

						unsigned int nVal = GetPortion(m_pKCasual[iEntry], iWord, iBitInWord, Casual::Secure::nBits);

						//Point::Native ptVal;
						//for (unsigned int i = 0; i < Casual::Secure::nCount; i++)
						//	object_cmov(ptVal, x.m_pPt[nVal], i == nVal);
						//
						//res += ptVal;

						res += x.U.S.get().m_pPt[nVal]; // cmov seems not needed, since the table is relatively small, and not in global mem (less predicatble addresses)
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

						res += ge_s.V;
					}
				}
			}
		}

		if (Mode::Secure == g_Mode)
		{
			for (int iEntry = 0; iEntry < m_Prepared; iEntry++)
			{
				const Prepared::Secure& x = m_ppPrepared[iEntry]->m_Secure;

				res += x.m_Compensation;
			}

			for (int iEntry = 0; iEntry < m_Casual; iEntry++)
				res += Context::get().m_Casual.m_Compensation;
		}
		else
		{
			// fix denominator
			secp256k1_fe_mul(&res.get_Raw().z, &res.get_Raw().z, &zDenom);
		}
	}

	void MultiMac_Dyn::Prepare(uint32_t nMaxCasual, uint32_t nMaxPrepared)
	{
		if (nMaxCasual)
		{
			m_vCasual.resize(nMaxCasual);
			m_vKCasual.resize(nMaxCasual);

			m_pCasual = &m_vCasual.front();
			m_pKCasual = &m_vKCasual.front();
		}

		if (nMaxPrepared)
		{
			m_vpPrepared.resize(nMaxPrepared);
			m_vKPrepared.resize(nMaxPrepared);
			m_vWnafPrepared.resize(nMaxPrepared);

			m_ppPrepared = &m_vpPrepared.front();
			m_pKPrep = &m_vKPrepared.front();
			m_pWnafPrepared = &m_vWnafPrepared.front();
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
	AlignedBuf<Context> g_ContextBuf;

	// Currently - auto-init in global obj c'tor
	Initializer g_Initializer;

#ifndef NDEBUG
	bool g_bContextInitialized = false;
#endif // NDEBUG

	const Context& Context::get()
	{
		assert(g_bContextInitialized);
		return g_ContextBuf.get();
	}

	void InitializeContext()
	{
		Context& ctx = g_ContextBuf.get();

		Mode::Scope scope(Mode::Fast);

		Oracle oracle;
		oracle << "Let the generator generation begin!";

		Point::Compact::Converter cpc;

		// make sure we get the same G,H for different generator kinds
		Point::Native G_raw, H_raw, J_raw;

		secp256k1_gej_set_ge(&G_raw.get_Raw(), &secp256k1_ge_const_g);
		Point ptG;
		Point::Native::ExportEx(ptG, secp256k1_ge_const_g);

		Hash::Processor hpRes;
		hpRes << ptG;

		Generator::CreatePointNnz(H_raw, oracle, &hpRes);
		Generator::CreatePointNnz(J_raw, oracle, &hpRes);


		ctx.G.Initialize(G_raw, oracle, cpc);
		ctx.H.Initialize(H_raw, oracle, cpc);
		ctx.H_Big.Initialize(H_raw, oracle, cpc);
		ctx.J.Initialize(J_raw, oracle, cpc);

		cpc.Flush();

		Point::Native pt, ptAux2(Zero);

		ctx.m_Ipp.G_.Initialize(G_raw, oracle, cpc);
		ctx.m_Ipp.H_.Initialize(H_raw, oracle, cpc);

		// for historical reasons: make a temp copy of oracle to initialize what was not present earlier
		Oracle o2 = oracle;
		o2 << "v1";

		ctx.m_Ipp.J_.Initialize(J_raw, o2, cpc);

		for (uint32_t i = 0; i < InnerProduct::nDim; i++)
		{
			for (uint32_t j = 0; j < 2; j++)
			{
				MultiMac::Prepared& p = ctx.m_Ipp.m_pGen_[j][i];
				p.Initialize(oracle, hpRes, cpc);

				// fast data is already flushed

				if (1 == j)
				{
					secp256k1_ge ge;
					secp256k1_ge_from_storage(&ge, &p.m_Fast.m_pPt[0]);
					secp256k1_ge_neg(&ge, &ge);
					secp256k1_ge_to_storage(&ctx.m_Ipp.m_pGet1_Minus[i], &ge);
				}
				else
					ptAux2 += p;
			}
		}

		ptAux2 = -ptAux2;
		ctx.m_Ipp.m_Aux2_.Initialize(ptAux2, oracle, cpc);

		ctx.m_Ipp.m_GenDot_.Initialize(oracle, hpRes, cpc);

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
			cpc.set_Deferred(ctx.m_Casual.m_Compensation, pt);
		}

		ctx.m_Ipp.m_2Inv.SetInv(2U);

		cpc.Flush();

		hpRes
			<< uint32_t(2) // increment this each time we change signature formula (rangeproof and etc.)
			>> ctx.m_hvChecksum;

		ctx.m_Sig.m_GenG.m_pGen = &ctx.G;
		ctx.m_Sig.m_GenG.m_pGenPrep = &ctx.m_Ipp.G_;
		ctx.m_Sig.m_GenG.m_nBatchIdx = InnerProduct::BatchContext::s_Idx_G;

		ctx.m_Sig.m_CfgG1.m_nKeys = 1;
		ctx.m_Sig.m_CfgG1.m_nG = 1;
		ctx.m_Sig.m_CfgG1.m_pG = &ctx.m_Sig.m_GenG;

		ctx.m_Sig.m_pGenGJ[0] = ctx.m_Sig.m_GenG;

		ctx.m_Sig.m_pGenGJ[1].m_pGen = &ctx.J;
		ctx.m_Sig.m_pGenGJ[1].m_pGenPrep = &ctx.m_Ipp.J_;
		ctx.m_Sig.m_pGenGJ[1].m_nBatchIdx = InnerProduct::BatchContext::s_Idx_J;

		ctx.m_Sig.m_pGenGH[0] = ctx.m_Sig.m_GenG;

		ctx.m_Sig.m_pGenGH[1].m_pGen = &ctx.H_Big;
		ctx.m_Sig.m_pGenGH[1].m_pGenPrep = &ctx.m_Ipp.H_;
		ctx.m_Sig.m_pGenGH[1].m_nBatchIdx = InnerProduct::BatchContext::s_Idx_H;

		ctx.m_Sig.m_CfgGJ1.m_nKeys = 1;
		ctx.m_Sig.m_CfgGJ1.m_nG = 2;
		ctx.m_Sig.m_CfgGJ1.m_pG = ctx.m_Sig.m_pGenGJ;

		ctx.m_Sig.m_CfgG2.m_nKeys = 2;
		ctx.m_Sig.m_CfgG2.m_nG = 1;
		ctx.m_Sig.m_CfgG2.m_pG = &ctx.m_Sig.m_GenG;

		ctx.m_Sig.m_CfgGH2.m_nKeys = 2;
		ctx.m_Sig.m_CfgGH2.m_nG = 2;
		ctx.m_Sig.m_CfgGH2.m_pG = ctx.m_Sig.m_pGenGH;

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
			Mode::Scope scope(Mode::Fast);

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

	void HKdf::GenerateChildParallel(Key::IKdf& kdf, const Hash::Value& hv)
	{
		Scalar::Native sk;
		kdf.DerivePKey(sk, hv);

		NoLeak<Scalar> sk_;
		sk_.V = sk;
		Generate(sk_.V.m_Value);

		m_kCoFactor *= sk;
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

	uint32_t HKdf::ExportP(void* p) const
	{
		HKdfPub pkdf;
		if (p)
			pkdf.GenerateFrom(*this);
		return pkdf.ExportP(p);
	}

	uint32_t HKdf::ExportS(void* p) const
	{
		if (p)
			Export(*reinterpret_cast<Packed*>(p));
		return sizeof(Packed);
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

	uint32_t HKdfPub::ExportP(void* p) const
	{
		if (p)
			Export(*reinterpret_cast<Packed*>(p));
		return sizeof(Packed);
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

	void HKdfPub::GenerateChildParallel(Key::IPKdf& kdf, const Hash::Value& hv)
	{
		Scalar::Native sk;
		kdf.DerivePKey(sk, hv);

		NoLeak<Scalar> sk_;
		sk_.V = sk;

		HKdf hkdf;
		hkdf.Generate(sk_.V.m_Value);
		GenerateFrom(hkdf);

		m_PkG = m_PkG * sk;
		m_PkJ = m_PkJ * sk;
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
	void SignatureBase::Expose(Oracle& oracle, const Hash::Value& msg) const
	{
		oracle
			<< m_NoncePub
			<< msg;
	}

	void SignatureBase::get_Challenge(Scalar::Native& out, const Hash::Value& msg) const
	{
		Oracle oracle;
		Expose(oracle, msg);
		oracle >> out;
	}

	bool SignatureBase::IsValid(const Config& cfg, const Hash::Value& msg, const Scalar* pK, const Point::Native* pPk) const
	{
		Point::Native noncePub;
		if (!noncePub.Import(m_NoncePub))
			return false;

		return IsValidPartial(cfg, msg, pK, pPk, noncePub);
	}

	bool SignatureBase::IsValidPartial(const Config& cfg, const Hash::Value& msg, const Scalar* pK, const Point::Native* pPk, const Point::Native& noncePub) const
	{
		// TODO: remove this limitation, get adequate MultiMac instance by vcall to cfg, which would invoke us back
		const uint32_t nMaxG = 2;
		const uint32_t nMaxK = 2;
		if ((cfg.m_nG <= nMaxG) && (cfg.m_nKeys <= nMaxK))
		{
			MultiMac_WithBufs<nMaxK, nMaxG> mm;
			return IsValidPartialInternal(cfg, mm, msg, pK, pPk, noncePub);
		}

		MultiMac_Dyn mmDyn;
		if (!InnerProduct::BatchContext::s_pInstance)
			mmDyn.Prepare(cfg.m_nKeys, cfg.m_nG); // otherwise mm won't be used
		return IsValidPartialInternal(cfg, mmDyn, msg, pK, pPk, noncePub);
	}

	bool SignatureBase::IsValidPartialInternal(const Config& cfg, MultiMac& mm, const Hash::Value& msg, const Scalar* pK, const Point::Native* pPk, const Point::Native& noncePub) const
	{
		Mode::Scope scope(Mode::Fast);

		Oracle oracle;
		Expose(oracle, msg);

		InnerProduct::BatchContext* pBc = InnerProduct::BatchContext::s_pInstance;
		if (pBc)
			pBc->EquationBegin();
		else
		{
			mm.m_Prepared = cfg.m_nG;
			mm.m_Casual = cfg.m_nKeys;
		}

		for (uint32_t iK = 0; iK < cfg.m_nKeys; iK++)
		{
			Scalar::Native e;
			oracle >> e;

			if (pBc)
				pBc->AddCasual(pPk[iK], e);
			else
			{
				mm.m_pCasual[iK].Init(pPk[iK]);
				mm.m_pKCasual[iK] = e;
			}
		}

		for (uint32_t iG = 0; iG < cfg.m_nG; iG++)
		{
			if (pBc)
				pBc->AddPrepared(cfg.m_pG[iG].m_nBatchIdx, pK[iG]);
			else
			{
				mm.m_ppPrepared[iG] = cfg.m_pG[iG].m_pGenPrep;
				mm.m_pKPrep[iG] = pK[iG];
			}
		}

		if (pBc)
		{
			pBc->AddCasual(noncePub, pBc->m_Multiplier, true);
			return true;
		}

		Point::Native pt;
		mm.Calculate(pt);

		pt += noncePub;
		return pt == Zero;
	}

	void SignatureBase::SetNoncePub(const Config& cfg, const Scalar::Native* pNonce)
	{
		Point::Native pubNonce = (*cfg.m_pG[0].m_pGen) * pNonce[0];

		for (uint32_t iG = 1; iG < cfg.m_nG; iG++)
			pubNonce += (*cfg.m_pG[iG].m_pGen) * pNonce[iG];

		m_NoncePub = pubNonce;
	}

	void SignatureBase::CreateNonces(const Config& cfg, const Hash::Value& msg, const Scalar::Native* pSk, Scalar::Native* pRes)
	{
		NonceGenerator nonceGen("beam-Schnorr");

		NoLeak<Scalar> s_;

		for (uint32_t iG = 0; iG < cfg.m_nG; iG++)
		{
			s_.V = pSk[iG];
			nonceGen << s_.V.m_Value;
		}

		nonceGen << msg;

		GenRandom(s_.V.m_Value); // add extra randomness to the nonce, so it's derived from both deterministic and random parts
		nonceGen
			<< s_.V.m_Value;

		for (uint32_t iG = 0; iG < cfg.m_nG; iG++)
			nonceGen >> pRes[iG];
	}

	void SignatureBase::Sign(const Config& cfg, const Hash::Value& msg, Scalar* pK, const Scalar::Native* pSk, Scalar::Native* pRes)
	{
		CreateNonces(cfg, msg, pSk, pRes);
		SetNoncePub(cfg, pRes);
		SignRaw(cfg, msg, pK, pSk, pRes);
	}

	void SignatureBase::SignRaw(const Config& cfg, const Hash::Value& msg, Scalar* pK, const Scalar::Native* pSk, Scalar::Native* pRes) const
	{
		Oracle oracle;
		Expose(oracle, msg);

		Scalar::Native e, k;

		for (uint32_t iK = 0; iK < cfg.m_nKeys; iK++, pSk += cfg.m_nG)
		{
			oracle >> e; // challenge

			for (uint32_t iG = 0; iG < cfg.m_nG; iG++)
			{
				k = pSk[iG] * e;
				pRes[iG] += k;
			}
		}

		for (uint32_t iG = 0; iG < cfg.m_nG; iG++)
		{
			pRes[iG] = -pRes[iG];
			pK[iG] = pRes[iG];
		}
	}

	void SignatureBase::SignPartial(const Config& cfg, const Hash::Value& msg, Scalar* pK, const Scalar::Native* pSk, const Scalar::Native* pNonce, Scalar::Native* pRes) const
	{
		for (uint32_t iG = 0; iG < cfg.m_nG; iG++)
			pRes[iG] = pNonce[iG];

		SignRaw(cfg, msg, pK, pSk, pRes);
	}

	const SignatureBase::Config& Signature::get_Config()
	{
		return ECC::Context::get().m_Sig.m_CfgG1;
	}

	void Signature::SignPartial(const Hash::Value& msg, const Scalar::Native& sk, const Scalar::Native& nonce)
	{
		Scalar::Native res;
		SignatureBase::SignPartial(get_Config(), msg, &m_k, &sk, &nonce, &res);
	}

	void Signature::Sign(const Hash::Value& msg, const Scalar::Native& sk)
	{
		Scalar::Native res;
		SignatureBase::Sign(get_Config(), msg, &m_k, &sk, &res);
	}

	bool Signature::IsValidPartial(const Hash::Value& msg, const Point::Native& pubNonce, const Point::Native& pk) const
	{
		return SignatureBase::IsValidPartial(get_Config(), msg, &m_k, &pk, pubNonce);
	}

	bool Signature::IsValid(const Hash::Value& msg, const Point::Native& pk) const
	{
		return SignatureBase::IsValid(get_Config(), msg, &m_k, &pk);
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
			m_Value = cp.m_Value;
			assert(m_Value >= s_MinimumValue);

			cp.BlobSave(reinterpret_cast<uint8_t*>(&m_Recovery.m_Kid), sizeof(m_Recovery.m_Kid));
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

			if (!cp.BlobRecover(reinterpret_cast<const uint8_t*>(&kid), sizeof(kid)))
				return false;

			cp.m_Value = m_Value;
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
