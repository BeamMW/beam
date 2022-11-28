#include "../common.h"
#include "../Math.h"
#include "contract.h"
#include "../vault/contract.h"
#include "../BeamHeader.h"
#include "../Ethash.h"
#include "../Eth.h"

// Demonstration of the inter-shader interaction.

BEAM_EXPORT void Ctor(void*)
{
    uint8_t ok = Env::RefAdd(Vault::s_CID);
    Env::Halt_if(!ok); // if the target shader doesn't exist the VM still gives a chance to run, but we don't need it.
}
BEAM_EXPORT void Dtor(void*)
{
    Env::RefRelease(Vault::s_CID);
}

BEAM_EXPORT void Method_2(Dummy::TestFarCall& r)
{
    Vault::Deposit r_Stack;
    _POD_(r_Stack).SetZero();
    r_Stack.m_Amount = 318;

    Env::Heap_Alloc(700); // make sure the next heap address is not the heap start address, this is better for testing

    Vault::Deposit* pR_Heap = (Vault::Deposit*) Env::Heap_Alloc(sizeof(Vault::Deposit));
    _POD_(*pR_Heap) = r_Stack;

    uint8_t* pR = reinterpret_cast<uint8_t*>(&r_Stack);

    switch (r.m_Variant)
    {
    case 1:
        pR += 1000;
        break;

    case 2:
        pR -= 100;
        break;

    case 3:
        pR = reinterpret_cast<uint8_t*>(pR_Heap);
        break;

    case 4:
        pR = reinterpret_cast<uint8_t*>(pR_Heap);
        pR += 16;
        break;

    case 5:
        pR = reinterpret_cast<uint8_t*>(pR_Heap);
        pR--;
        break;

    case 6:
        {
            static const uint8_t s_pReq[sizeof(Vault::Deposit)] = { 0 };
            pR = Cast::NotConst(s_pReq);
        }
        break;

    case 7:
        pR = nullptr;
    }

    Env::CallFar_T(Vault::s_CID, *reinterpret_cast<Vault::Deposit*>(pR), r.m_Flags);
}

BEAM_EXPORT void Method_3(Dummy::MathTest1& r)
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

BEAM_EXPORT void Method_4(Dummy::DivTest1& r)
{
    r.m_Denom = r.m_Nom / r.m_Denom;
}

BEAM_EXPORT void Method_5(Dummy::InfCycle&)
{
    for (uint32_t i = 0; i < 20000000; i++)
        Env::get_Height();
}

BEAM_EXPORT void Method_6(Dummy::Hash1& r)
{
    HashObj* pHash = Env::HashCreateSha256();
    Env::Halt_if(!pHash);

    Env::HashWrite(pHash, r.m_pInp, sizeof(r.m_pInp));
    Env::HashGetValue(pHash, r.m_pRes, sizeof(r.m_pRes));

    Env::HashFree(pHash);
}

BEAM_EXPORT void Method_7(Dummy::Hash2& r)
{
    static const char szPers[] = "abcd";

    HashObj* pHash = Env::HashCreateBlake2b(szPers, sizeof(szPers)-1, sizeof(r.m_pRes));
    Env::Halt_if(!pHash);

    Env::HashWrite(pHash, r.m_pInp, sizeof(r.m_pInp));
    Env::HashGetValue(pHash, r.m_pRes, sizeof(r.m_pRes));

    Env::HashFree(pHash);
}

BEAM_EXPORT void Method_8(Dummy::Hash3& r)
{
    HashObj* pHash = Env::HashCreateKeccak(r.m_Bits);
    Env::Halt_if(!pHash);

    for (uint32_t nDone = 0; nDone < r.m_Inp; )
    {
        auto nPortion = std::min(r.m_NaggleBytes, r.m_Inp - nDone);
        Env::HashWrite(pHash, r.m_pInp + nDone, nPortion);
        nDone += nPortion;
    }

    Env::HashGetValue(pHash, r.m_pRes, r.m_Bits / 8);

    Env::HashFree(pHash);
}

BEAM_EXPORT void Method_9(Dummy::VerifyBeamHeader& r)
{
    r.m_Hdr.get_Hash(r.m_Hash, &r.m_RulesCfg);
    Env::Halt_if(!r.m_Hdr.IsValid<true>(&r.m_RulesCfg));
    Env::Halt_if(!r.m_Hdr.IsValid<false>(&r.m_RulesCfg));

    BeamDifficulty::Raw w0, w1;
    BeamDifficulty::Unpack(w1, r.m_Hdr.m_PoW.m_Difficulty);
    w0.FromBE_T(r.m_Hdr.m_ChainWork);
    w0 -= w1;
    w0.ToBE_T(r.m_ChainWork0);
}

BEAM_EXPORT void Method_10(Dummy::TestFarCallStack& r)
{
    Env::get_CallerCid(r.m_iCaller, r.m_Cid);
}

bool TestRingSignature(const HashValue& msg, uint32_t nRing, const PubKey* pPk, const Secp_scalar_data& e0, const Secp_scalar_data* pK)
{
    if (!nRing)
        return true;

    Secp_point* pP0 = Env::Secp_Point_alloc();
    Secp_point* pP1 = Env::Secp_Point_alloc();

    Secp_scalar* pE = Env::Secp_Scalar_alloc();
    Env::Halt_if(!Env::Secp_Scalar_import(*pE, e0));

    Secp_scalar* pS = Env::Secp_Scalar_alloc();
    Secp_scalar_data ed;

    for (uint32_t i = 0; i < nRing; i++)
    {
        Env::Halt_if(!Env::Secp_Scalar_import(*pS, pK[i]));
        Env::Secp_Point_mul_G(*pP0, *pS);

        Env::Halt_if(!Env::Secp_Point_Import(*pP1, pPk[i]));
        Env::Secp_Point_mul(*pP1, *pP1, *pE);

        Env::Secp_Point_add(*pP0, *pP0, *pP1); // k[i]*G + e*P[i]

        // derive next challenge
        Secp_point_data pd;
        Env::Secp_Point_Export(*pP0, pd);

        HashProcessor::Sha256 hp;
        hp
            << pd
            << msg;

        while (true)
        {
            hp >> ed;
            if (!_POD_(ed).IsZero() && Env::Secp_Scalar_import(*pE, ed))
                break;
            hp.Write(&ed, sizeof(ed));
        }
    }

    Env::Secp_Point_free(*pP0);
    Env::Secp_Point_free(*pP1);

    Env::Secp_Scalar_free(*pE);
    Env::Secp_Scalar_free(*pS);

    return _POD_(ed) == e0;
}

BEAM_EXPORT void Method_11(Dummy::TestRingSig& r)
{
    Env::Halt_if(!TestRingSignature(r.m_Msg, r.s_Ring, r.m_pPks, r.m_e, r.m_pK));
}

//BEAM_EXPORT void Method_12(Dummy::TestEthash& r)
//{
    //// 1. derive pow seed
    //HashValue512 hvSeed;
    //{
    //    HashProcessor::Base hp;
    //    hp.m_p = Env::HashCreateKeccak(512);
    //    hp << r.m_HeaderHash;

    //    auto val = Utils::FromLE(r.m_Nonce);
    //    hp.Write(&val, sizeof(val));

    //    hp >> hvSeed;
    //}

    //// 2. get mix hash
    //HashValue hvMix;
    //Env::Halt_if(!Env::get_EthMixHash(hvMix, r.m_BlockNumber / 30000, hvSeed));

    //// 3. 'final' hash
    //{
    //    HashProcessor::Base hp;
    //    hp.m_p = Env::HashCreateKeccak(256);
    //    hp
    //        << hvSeed
    //        << hvMix
    //        >> hvMix;
    //}

    //// 4. Test difficulty
    //MultiPrecision::UInt<sizeof(HashValue) / sizeof(MultiPrecision::Word)> val1; // 32 bytes, 8 words
    //val1.FromBE_T(hvMix);

    //MultiPrecision::UInt<sizeof(r.m_Difficulty) / sizeof(MultiPrecision::Word)> val2; // 8 bytes, 2 words
    //val2 = r.m_Difficulty;

    //auto val3 = val1 * val2; // 40 bytes, 10 words
    //
    //// check that 2 most significant words are 0
    //Env::Halt_if(val3.get_Val<val3.nWords>() || val3.get_Val<val3.nWords - 1>());
//}

BEAM_EXPORT void Method_12(Dummy::TestEthHeader& r)
{
    const auto& hdr = r.m_Header;

    Ethash::Hash512 hvSeed;
    hdr.get_SeedForPoW(hvSeed);

    Ethash::VerifyHdr(hdr.get_Epoch(), r.m_EpochDatasetSize, hvSeed, hdr.m_Nonce, hdr.m_Difficulty, &r + 1, static_cast<uint32_t>(-1));
}

BEAM_EXPORT void Method_13(Dummy::MathTest2& r)
{
    r.m_Resid = r.m_Nom;
    r.m_Quotient.SetDivResid(r.m_Resid, r.m_Denom);
}

BEAM_EXPORT void Method_14(Dummy::FindVarTest& r)
{
    Env::SaveVar_T((uint8_t) 1, (uint8_t) 1);
    Env::SaveVar_T((uint8_t) 3, (uint8_t) 3);
    Env::SaveVar_T((uint8_t) 5, (uint8_t) 5);

    uint8_t nKey, nVal;
    uint32_t nKeySize, nValSize;


    Env::LoadVarEx(&(nKey = 3), (nKeySize = sizeof(nKey)), sizeof(nKey), &nVal, (nValSize = sizeof(nVal)), KeyTag::Internal, KeySearchFlags::Exact | KeySearchFlags::Bigger);
    Env::Halt_if((nKeySize != sizeof(nKey)) || (nValSize != sizeof(nVal)) || (nKey != 3) || (nVal != 3));

    Env::LoadVarEx(&(nKey = 3), (nKeySize = sizeof(nKey)), sizeof(nKey), &nVal, (nValSize = sizeof(nVal)), KeyTag::Internal, KeySearchFlags::Bigger);
    Env::Halt_if((nKeySize != sizeof(nKey)) || (nValSize != sizeof(nVal)) || (nKey != 5) || (nVal != 5));

    Env::LoadVarEx(&(nKey = 3), (nKeySize = sizeof(nKey)), sizeof(nKey), &nVal, (nValSize = sizeof(nVal)), KeyTag::Internal, KeySearchFlags::Exact);
    Env::Halt_if((nKeySize != sizeof(nKey)) || (nValSize != sizeof(nVal)) || (nKey != 3) || (nVal != 3));

    Env::LoadVarEx(&(nKey = 3), (nKeySize = sizeof(nKey)), sizeof(nKey), &nVal, (nValSize = sizeof(nVal)), KeyTag::Internal, 0);
    Env::Halt_if((nKeySize != sizeof(nKey)) || (nValSize != sizeof(nVal)) || (nKey != 1) || (nVal != 1));

    Env::LoadVarEx(&(nKey = 2), (nKeySize = sizeof(nKey)), sizeof(nKey), &nVal, (nValSize = sizeof(nVal)), KeyTag::Internal, KeySearchFlags::Exact | KeySearchFlags::Bigger);
    Env::Halt_if((nKeySize != sizeof(nKey)) || (nValSize != sizeof(nVal)) || (nKey != 3) || (nVal != 3));

    Env::LoadVarEx(&(nKey = 2), (nKeySize = sizeof(nKey)), sizeof(nKey), &nVal, (nValSize = sizeof(nVal)), KeyTag::Internal, KeySearchFlags::Bigger);
    Env::Halt_if((nKeySize != sizeof(nKey)) || (nValSize != sizeof(nVal)) || (nKey != 3) || (nVal != 3));

    Env::LoadVarEx(&(nKey = 2), (nKeySize = sizeof(nKey)), sizeof(nKey), &nVal, (nValSize = sizeof(nVal)), KeyTag::Internal, KeySearchFlags::Exact);
    Env::Halt_if((nKeySize != sizeof(nKey)) || (nValSize != sizeof(nVal)) || (nKey != 1) || (nVal != 1));

    Env::LoadVarEx(&(nKey = 2), (nKeySize = sizeof(nKey)), sizeof(nKey), &nVal, (nValSize = sizeof(nVal)), KeyTag::Internal, 0);
    Env::Halt_if((nKeySize != sizeof(nKey)) || (nValSize != sizeof(nVal)) || (nKey != 1) || (nVal != 1));

    Env::LoadVarEx(&(nKey = 1), (nKeySize = sizeof(nKey)), sizeof(nKey), &nVal, (nValSize = sizeof(nVal)), KeyTag::Internal, KeySearchFlags::Exact | KeySearchFlags::Bigger);
    Env::Halt_if((nKeySize != sizeof(nKey)) || (nValSize != sizeof(nVal)) || (nKey != 1) || (nVal != 1));

    Env::LoadVarEx(&(nKey = 1), (nKeySize = sizeof(nKey)), sizeof(nKey), &nVal, (nValSize = sizeof(nVal)), KeyTag::Internal, KeySearchFlags::Bigger);
    Env::Halt_if((nKeySize != sizeof(nKey)) || (nValSize != sizeof(nVal)) || (nKey != 3) || (nVal != 3));

    Env::LoadVarEx(&(nKey = 1), (nKeySize = sizeof(nKey)), sizeof(nKey), &nVal, (nValSize = sizeof(nVal)), KeyTag::Internal, KeySearchFlags::Exact);
    Env::Halt_if((nKeySize != sizeof(nKey)) || (nValSize != sizeof(nVal)) || (nKey != 1) || (nVal != 1));

    Env::LoadVarEx(&(nKey = 1), (nKeySize = sizeof(nKey)), sizeof(nKey), &nVal, (nValSize = sizeof(nVal)), KeyTag::Internal, 0);
    Env::Halt_if(nKeySize || nValSize);

    Env::LoadVarEx(&(nKey = 0), (nKeySize = sizeof(nKey)), sizeof(nKey), &nVal, (nValSize = sizeof(nVal)), KeyTag::Internal, KeySearchFlags::Exact | KeySearchFlags::Bigger);
    Env::Halt_if((nKeySize != sizeof(nKey)) || (nValSize != sizeof(nVal)) || (nKey != 1) || (nVal != 1));

    Env::LoadVarEx(&(nKey = 0), (nKeySize = sizeof(nKey)), sizeof(nKey), &nVal, (nValSize = sizeof(nVal)), KeyTag::Internal, KeySearchFlags::Bigger);
    Env::Halt_if((nKeySize != sizeof(nKey)) || (nValSize != sizeof(nVal)) || (nKey != 1) || (nVal != 1));

    Env::LoadVarEx(&(nKey = 0), (nKeySize = sizeof(nKey)), sizeof(nKey), &nVal, (nValSize = sizeof(nVal)), KeyTag::Internal, KeySearchFlags::Exact);
    Env::Halt_if(nKeySize || nValSize);

    Env::LoadVarEx(&(nKey = 0), (nKeySize = sizeof(nKey)), sizeof(nKey), &nVal, (nValSize = sizeof(nVal)), KeyTag::Internal, 0);
    Env::Halt_if(nKeySize || nValSize);

    Env::LoadVarEx(&(nKey = 5), (nKeySize = sizeof(nKey)), sizeof(nKey), &nVal, (nValSize = sizeof(nVal)), KeyTag::Internal, KeySearchFlags::Exact | KeySearchFlags::Bigger);
    Env::Halt_if((nKeySize != sizeof(nKey)) || (nValSize != sizeof(nVal)) || (nKey != 5) || (nVal != 5));

    Env::LoadVarEx(&(nKey = 5), (nKeySize = sizeof(nKey)), sizeof(nKey), &nVal, (nValSize = sizeof(nVal)), KeyTag::Internal, KeySearchFlags::Bigger);
    Env::Halt_if(nKeySize || nValSize);

    Env::LoadVarEx(&(nKey = 5), (nKeySize = sizeof(nKey)), sizeof(nKey), &nVal, (nValSize = sizeof(nVal)), KeyTag::Internal, KeySearchFlags::Exact);
    Env::Halt_if((nKeySize != sizeof(nKey)) || (nValSize != sizeof(nVal)) || (nKey != 5) || (nVal != 5));

    Env::LoadVarEx(&(nKey = 5), (nKeySize = sizeof(nKey)), sizeof(nKey), &nVal, (nValSize = sizeof(nVal)), KeyTag::Internal, 0);
    Env::Halt_if((nKeySize != sizeof(nKey)) || (nValSize != sizeof(nVal)) || (nKey != 3) || (nVal != 3));

    Env::LoadVarEx(&(nKey = 6), (nKeySize = sizeof(nKey)), sizeof(nKey), &nVal, (nValSize = sizeof(nVal)), KeyTag::Internal, KeySearchFlags::Exact | KeySearchFlags::Bigger);
    Env::Halt_if(nKeySize || nValSize);

    Env::LoadVarEx(&(nKey = 6), (nKeySize = sizeof(nKey)), sizeof(nKey), &nVal, (nValSize = sizeof(nVal)), KeyTag::Internal, KeySearchFlags::Bigger);
    Env::Halt_if(nKeySize || nValSize);

    Env::LoadVarEx(&(nKey = 6), (nKeySize = sizeof(nKey)), sizeof(nKey), &nVal, (nValSize = sizeof(nVal)), KeyTag::Internal, KeySearchFlags::Exact);
    Env::Halt_if((nKeySize != sizeof(nKey)) || (nValSize != sizeof(nVal)) || (nKey != 5) || (nVal != 5));

    Env::LoadVarEx(&(nKey = 6), (nKeySize = sizeof(nKey)), sizeof(nKey), &nVal, (nValSize = sizeof(nVal)), KeyTag::Internal, 0);
    Env::Halt_if((nKeySize != sizeof(nKey)) || (nValSize != sizeof(nVal)) || (nKey != 5) || (nVal != 5));
}

BEAM_EXPORT void Method_15(Dummy::TestFarCallFlags& r)
{
    if (r.m_DepthRemaining)
    {
        r.m_DepthRemaining--;
        uint32_t nFlags = r.m_Flags;
        r.m_Flags = 0; // flags must have cumulative effect, it must stay though we remove it for consecutive calls

        ContractID cid;
        Env::get_CallerCid(0, cid);
        Env::CallFar_T(cid, r, nFlags);

        Env::Halt_if(r.m_DepthRemaining != 0);
    }
    else
    {
        if (r.m_TryWrite)
            Env::SaveVar_T((uint8_t) 1, (uint8_t) 2);
    }
}
