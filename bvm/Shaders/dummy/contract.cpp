#include "../common.h"
#include "../Math.h"
#include "contract.h"
#include "../vault/contract.h"
#include "../MergeSort.h"

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

namespace IndexDecoder
{
	typedef uint32_t Word;
	static const uint32_t s_WordBits = sizeof(Word) * 8;

	template <uint32_t nBitsPerIndex, uint32_t nSrcIdx, uint32_t nSrcTotal>
	struct State
	{
		static_assert(nBitsPerIndex <= s_WordBits, "");
		static_assert(nBitsPerIndex >= s_WordBits/2, "unpack should affect no more than 3 adjacent indices");

		static const Word s_Msk = (((Word) 1) << nBitsPerIndex) - 1;

		static const uint32_t s_BitsDecoded = nSrcIdx * s_WordBits;

		static const uint32_t s_iDst = s_BitsDecoded / nBitsPerIndex;
		static const uint32_t s_nDstBitsDone = s_BitsDecoded % nBitsPerIndex;
		static const uint32_t s_nDstBitsRemaining = nBitsPerIndex - s_nDstBitsDone;

		static void Do(Word* pDst, const Word* pSrc)
		{
			Word src = Utils::FromLE(pSrc[nSrcIdx]);

			if constexpr (s_nDstBitsDone > 0)
				pDst[s_iDst] |= (src << s_nDstBitsDone) & s_Msk;
			else
				pDst[s_iDst] = src & s_Msk;

			pDst[s_iDst + 1] = (src >> s_nDstBitsRemaining) & s_Msk;

			if constexpr (s_nDstBitsRemaining + nBitsPerIndex < s_WordBits)
				pDst[s_iDst + 2] = (src >> (s_nDstBitsRemaining + nBitsPerIndex)) & s_Msk;

			if constexpr (nSrcIdx + 1 < nSrcTotal)
				State<nBitsPerIndex, nSrcIdx + 1, nSrcTotal>::Do(pDst, pSrc);
		}
	};
}

namespace sipHash {
 
static uint64_t rotl(uint64_t x, uint64_t b) {
	return (x << b) | (x >> (64 - b));
}

#define sipRound() {		\
	v0 += v1; v2 += v3;	\
	v1 = rotl(v1,13);	\
	v3 = rotl(v3,16); 	\
	v1 ^= v0; v3 ^= v2;	\
	v0 = rotl(v0,32); 	\
	v2 += v1; v0 += v3;	\
	v1 = rotl(v1,17);   	\
	v3 = rotl(v3,21);	\
	v1 ^= v2; v3 ^= v0; 	\
	v2 = rotl(v2,32);	\
}

uint64_t siphash24(uint64_t state0, uint64_t state1, uint64_t state2, uint64_t state3, uint64_t nonce) {
	uint64_t v0, v1, v2, v3;

	v0 = state0; v1=state1; v2=state2; v3=state3;
	v3 ^= nonce;
	sipRound();
	sipRound();
	v0 ^= nonce;
   	v2 ^= 0xff;
	sipRound();
	sipRound();
	sipRound();
	sipRound();

	return (v0 ^ v1 ^ v2 ^ v3);	
}

} //end namespace sipHash


const uint32_t workBitSize = 448;
const uint32_t collisionBitSize = 24;
const uint32_t numRounds = 5;


struct StepElemLite
{
    static_assert(!(workBitSize % 8), "");
    static const uint32_t s_workBytes = workBitSize / 8;

    typedef uint64_t WorkWord;
    static_assert(!(s_workBytes % sizeof(WorkWord)), "");
    static const uint32_t s_workWords = s_workBytes / sizeof(WorkWord);

    WorkWord m_pWorkWords[s_workWords];

    void Init(const uint64_t* prePow, uint32_t index);
    void MergeWith(const StepElemLite& x, uint32_t remLen);
    void applyMix(uint32_t remLen, const uint32_t* pIdx, uint32_t nIdx);

    bool hasCollision(const StepElemLite& x) const;
};

void StepElemLite::Init(const uint64_t* prePow, uint32_t index)
{
    for (int32_t i = _countof(m_pWorkWords); i--; )
        m_pWorkWords[i] = sipHash::siphash24(prePow[0], prePow[1], prePow[2], prePow[3], (index << 3) + i);
}

void StepElemLite::MergeWith(const StepElemLite& x, uint32_t remLen)
{
    // Create a new rounds step element from matching two ancestors
    assert(!(remLen % 8));
    const uint32_t remBytes = remLen / 8;

    for (int32_t i = _countof(m_pWorkWords); i--; )
        m_pWorkWords[i] ^= x.m_pWorkWords[i];

    static_assert(!(collisionBitSize % 8), "");
    const uint32_t collisionBytes = collisionBitSize / 8;

    assert(s_workBytes - remBytes >= collisionBytes);

    Env::Memcpy(m_pWorkWords, ((uint8_t*)m_pWorkWords) + collisionBytes, remBytes); // it's actually memmove
    Env::Memset(((uint8_t*) m_pWorkWords) + remBytes, 0, s_workBytes - remBytes);
}

void StepElemLite::applyMix(uint32_t remLen, const uint32_t* pIdx, uint32_t nIdx)
{
    WorkWord pTemp[9];
    static_assert(sizeof(pTemp) > sizeof(m_pWorkWords), "");
    Env::Memcpy(pTemp, m_pWorkWords, sizeof(m_pWorkWords));
    Env::Memset(pTemp + _countof(m_pWorkWords), 0, sizeof(pTemp) - sizeof(m_pWorkWords));

    static_assert(!(collisionBitSize % 8), "");
    const uint32_t collisionBytes = collisionBitSize / 8;

    // Add in the bits of the index tree to the end of work bits
    uint32_t padNum = ((512 - remLen) + collisionBitSize) / (collisionBitSize + 1);
    if (padNum > nIdx)
        padNum = nIdx;

    for (uint32_t i = 0; i < padNum; i++)
    {
        uint32_t nShift = remLen + i * (collisionBitSize + 1);
        uint32_t n0 = nShift / (sizeof(WorkWord) * 8);
        nShift %= (sizeof(WorkWord) * 8);

        auto idx = pIdx[i];

        pTemp[n0] |= ((WorkWord)idx) << nShift;

        if (nShift + collisionBitSize + 1 > (sizeof(WorkWord) * 8))
            pTemp[n0 + 1] |= idx >> (sizeof(WorkWord) * 8 - nShift);
    }


    // Applyin the mix from the lined up bits
    uint64_t result = 0;
    for (uint32_t i = 0; i < 8; i++)
        result += sipHash::rotl(pTemp[i], (29 * (i + 1)) & 0x3F);

    result = sipHash::rotl(result, 24);

    // Wipe out lowest 64 bits in favor of the mixed bits
    m_pWorkWords[0] = result;
}

bool StepElemLite::hasCollision(const StepElemLite& x) const
{
    auto val = m_pWorkWords[0] ^ x.m_pWorkWords[0];
    const uint32_t msk = (1U << collisionBitSize) - 1;
    return !(val & msk);
}






bool TestBeamHashIII(const HashValue& hvInp, const void* pNonce, uint32_t nNonce, const uint8_t* pSol, uint32_t nSol)
{
    if (104 != nSol)
        return false;

    uint64_t pPrePoW[4];
    static_assert(sizeof(pPrePoW) == sizeof(hvInp), "");

    {
        HashProcessor hp;

#pragma pack (push, 1)
        struct Personal {
            char m_sz[8];
            uint32_t m_WorkBits;
            uint32_t m_Rounds;
        } pers;
#pragma pack (pop)

        Env::Memcpy(pers.m_sz, "Beam-PoW", sizeof(pers.m_sz));
        pers.m_WorkBits = 448;
        pers.m_Rounds = 5;

        hp.m_p = Env::HashCreateBlake2b(&pers, sizeof(pers), 32);
        hp << hvInp;
        hp.Write(pNonce, nNonce);

        hp.Write(pSol + 100, 4); // last 4 bytes are the extra nonce
        Env::HashGetValue(hp.m_p, pPrePoW, sizeof(pPrePoW));
    }

    uint32_t pIndices[32];
    IndexDecoder::State<25, 0, 25>::Do(pIndices, (const uint32_t*) pSol);

    StepElemLite pElemLite[_countof(pIndices)];
    for (uint32_t i = 0; i < _countof(pIndices); i++)
        pElemLite[i].Init(pPrePoW, pIndices[i]);

	uint32_t round=1;
	for (uint32_t nStep = 1; nStep < _countof(pIndices); nStep <<= 1)
    {
		for (uint32_t i0 = 0; i0 < _countof(pIndices); )
        {
			uint32_t remLen = workBitSize-(round-1)*collisionBitSize;
			if (round == 5) remLen -= 64;

			pElemLite[i0].applyMix(remLen, pIndices + i0, nStep);

			uint32_t i1 = i0 + nStep;
			pElemLite[i1].applyMix(remLen, pIndices + i1, nStep);

            if (!pElemLite[i0].hasCollision(pElemLite[i1]))
                return false;

            if (pIndices[i0] >= pIndices[i1])
                return false;

			remLen = workBitSize-round*collisionBitSize;
			if (round == 4) remLen -= 64;
			if (round == 5) remLen = collisionBitSize;

			pElemLite[i0].MergeWith(pElemLite[i1], remLen);

			i0 = i1 + nStep;
		}

		round++;
	}

    if (!Env::Memis0(pElemLite[0].m_pWorkWords, sizeof(pElemLite[0].m_pWorkWords)))
        return false;

	// ensure all the indices are distinct
    static_assert(sizeof(pElemLite) >= sizeof(pIndices), "");
    auto* pSorted = MergeSort<uint32_t>::Do(pIndices, (uint32_t*) pElemLite, _countof(pIndices));

	for (uint32_t i = 0; i + 1 < _countof(pIndices); i++)
        if (pSorted[i] >= pSorted[i + 1])
            return false;

    return true;
}

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

    {
        HashProcessor hp;

#pragma pack (push, 1)
        struct Personal {
            char m_sz[8];
            uint32_t m_WorkBits;
            uint32_t m_Rounds;
        } pers;
#pragma pack (pop)

        Env::Memcpy(pers.m_sz, "Beam-PoW", sizeof(pers.m_sz));
        pers.m_WorkBits = 448;
        pers.m_Rounds = 5;

        hp.m_p = Env::HashCreateBlake2b(&pers, sizeof(pers), 32);
        hp << r.m_HashForPoW;
        hp.Write(r.m_Hdr.m_pNonce, sizeof(r.m_Hdr.m_pNonce));

        static_assert(sizeof(r.m_Hdr.m_pIndices) == 104, "");
        hp.Write(r.m_Hdr.m_pIndices + 100, 4); // last 4 bytes are the extra nonce
        hp >> r.m_PrePoW;
    }

    IndexDecoder::State<25, 0, 25>::Do(r.m_pIndices, (const uint32_t*) r.m_Hdr.m_pIndices);

    bool bOk = TestBeamHashIII(r.m_HashForPoW, r.m_Hdr.m_pNonce, sizeof(r.m_Hdr.m_pNonce), r.m_Hdr.m_pIndices, sizeof(r.m_Hdr.m_pIndices));
    Env::Halt_if(!bOk);
}
