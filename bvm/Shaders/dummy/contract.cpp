#include "../common.h"
#include "../Math.h"
#include "contract.h"
#include "../vault/contract.h"

// Demonstration of the inter-shader interaction.

export void Ctor(void*)
{
    uint8_t ok = Env::RefAdd(Vault::s_CID);
    Env::Halt_if(!ok); // if the target shader doesn't exist the VM still gives a chance to run, but we don't need it.
}
export void Dtor(void*)
{
    Env::RefRelease(Vault::s_CID);
}

export void Method_2(void*)
{
    Vault::Deposit r;
    Utils::ZeroObject(r);
    r.m_Amount = 318;
    Env::CallFar_T(Vault::s_CID, r);
}

export void Method_3(Dummy::MathTest1& r)
{
    auto res =
        MultiPrecision::From(r.m_Value) *
        MultiPrecision::From(r.m_Rate) *
        MultiPrecision::From(r.m_Factor);

    //    Env::Trace("res", &res);

    auto trg = MultiPrecision::FromEx<2>(r.m_Try);

    //	Env::Trace("trg", &trg);

    r.m_IsOk = (trg <= res);
}

export void Method_4(Dummy::DivTest1& r)
{
    r.m_Denom = r.m_Nom / r.m_Denom;
}

export void Method_5(Dummy::InfCycle&)
{
    for (uint32_t i = 0; i < 20000000; i++)
        Env::get_Height();
}

export void Method_6(Dummy::Hash1& r)
{
    HashObj* pHash = Env::HashCreateSha256();
    Env::Halt_if(!pHash);

    Env::HashWrite(pHash, r.m_pInp, sizeof(r.m_pInp));
    Env::HashGetValue(pHash, r.m_pRes, sizeof(r.m_pRes));

    Env::HashFree(pHash);
}

export void Method_7(Dummy::Hash2& r)
{
    static const char szPers[] = "abcd";

    HashObj* pHash = Env::HashCreateBlake2b(szPers, sizeof(szPers)-1, sizeof(r.m_pRes));
    Env::Halt_if(!pHash);

    Env::HashWrite(pHash, r.m_pInp, sizeof(r.m_pInp));
    Env::HashGetValue(pHash, r.m_pRes, sizeof(r.m_pRes));

    Env::HashFree(pHash);
}

export void Method_8(Dummy::Hash3& r)
{
    HashObj* pHash = Env::HashCreateKeccak256();
    Env::Halt_if(!pHash);

    Env::HashWrite(pHash, r.m_pInp, sizeof(r.m_pInp));
    Env::HashGetValue(pHash, r.m_pRes, sizeof(r.m_pRes));

    Env::HashFree(pHash);
}

struct HashProcessor
{
    HashObj* m_p;

    HashProcessor()
        :m_p(nullptr)
    {
    }

    ~HashProcessor()
    {
        if (m_p)
            Env::HashFree(m_p);
    }

    template <typename T>
    HashProcessor& operator << (const T& x)
    {
        Write(x);
        return *this;
    }

    void Write(uint8_t x) {
        Env::HashWrite(m_p, &x, sizeof(x));
    }

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

    void Write(const void* p, uint32_t n)
    {
        Env::HashWrite(m_p, p, n);
    }

    void Write(const HashValue& hv)
    {
        Write(&hv, sizeof(hv));
    }

    template <typename T>
    void operator >> (T& res)
    {
        Env::HashGetValue(m_p, &res, sizeof(res));
    }
};

void get_HdrHash(HashValue& out, const Dummy::VerifyBeamHeader::Hdr& hdr, bool bFull, const HashValue* pRules)
{
    HashProcessor hp;
    hp.m_p = Env::HashCreateSha256();

    hp
        << hdr.m_Height
        << hdr.m_Prev
        << hdr.m_ChainWork
        << hdr.m_Kernels
        << hdr.m_Definition
        << hdr.m_TimeStamp
        << hdr.m_DifficultyPacked;

    if (pRules)
        hp << *pRules;

    if (bFull)
    {
        hp.Write(hdr.m_pIndices, sizeof(hdr.m_pIndices));
        hp.Write(hdr.m_pNonce, sizeof(hdr.m_pNonce));
    }

    hp >> out;
}

template <typename TDst, typename TSrc>
void BSwap(TDst& dst, const TSrc& src)
{
    static_assert(sizeof(dst) == sizeof(src), "");
    for (uint32_t i = 0; i < sizeof(src); i++)
        ((uint8_t*) &dst)[i] = ((uint8_t*) &src)[sizeof(src) - 1 - i];
}

struct Difficulty
{
    typedef MultiPrecision::UInt<8> Raw;

    static const uint32_t s_MantissaBits = 24;

    static void Unpack(Raw& res, uint32_t nPacked)
    {
        // unpack difficulty
        const uint32_t nLeadingBit = 1U << s_MantissaBits;
        uint32_t order = (nPacked >> s_MantissaBits);

        if (order > 231)
            Utils::SetObject(res, static_cast<uint8_t>(-1)); // inf
        else
        {
            MultiPrecision::UInt<1> mantissa = nLeadingBit | (nPacked & (nLeadingBit - 1));
            res.Set(mantissa, order);
        }
    }
};

export void Method_9(Dummy::VerifyBeamHeader& r)
{
    get_HdrHash(r.m_HashForPoW, r.m_Hdr, false, &r.m_RulesCfg);
    get_HdrHash(r.m_Hash, r.m_Hdr, true, &r.m_RulesCfg);

    // test difficulty
    {
        HashProcessor hp;
        hp.m_p = Env::HashCreateSha256();
        hp.Write(r.m_Hdr.m_pIndices, sizeof(r.m_Hdr.m_pIndices));
        hp >> r.m_DiffRes;
    }

    Difficulty::Raw diff, src;
    Difficulty::Unpack(diff, r.m_Hdr.m_DifficultyPacked);
    BSwap(r.m_DiffUnpacked, diff);

    BSwap(src, r.m_DiffRes);
    auto a = diff * src; // would be 512 bits
    BSwap(r.m_pDiffMultiplied, a);

    static_assert(!(Difficulty::s_MantissaBits & 7), ""); // fix the following code lines to support non-byte-aligned mantissa size
    r.m_DiffTestOk = Env::Memis0(r.m_pDiffMultiplied, sizeof(src) - (Difficulty::s_MantissaBits >> 3));
}
