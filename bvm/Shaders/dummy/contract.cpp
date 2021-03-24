#include "../common.h"
#include "../Math.h"
#include "contract.h"
#include "../vault/contract.h"
#include "../BeamHeader.h"

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

export void Method_2(Dummy::TestFarCall& r)
{
    Vault::Deposit r_Stack;
    _POD_(r_Stack).SetZero();
    r_Stack.m_Amount = 318;

    Env::Heap_Alloc(700); // make sure the next heap address is not the heap start address, this is better for testing

    Vault::Deposit* pR_Heap = (Vault::Deposit*) Env::Heap_Alloc(sizeof(Vault::Deposit));
    _POD_(*pR_Heap) = r_Stack;

    auto* pR = &r_Stack;

    switch (r.m_Variant)
    {
    case 1:
        ((uint8_t*&) pR) += 1000;
        break;

    case 2:
        ((uint8_t*&) pR) -= 100;
        break;

    case 3:
        pR = pR_Heap;
        break;

    case 4:
        pR = pR_Heap;
        ((uint8_t*&) pR) += 16;
        break;

    case 5:
        pR = pR_Heap;
        ((uint8_t*&) pR)--;
        break;

    case 6:
        {
            static const uint8_t s_pReq[sizeof(Vault::Deposit)] = { 0 };
            pR = (Vault::Deposit*) s_pReq;
        }
        break;

    case 7:
        pR = nullptr;
    }

    Env::CallFar_T(Vault::s_CID, *pR);
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

export void Method_9(Dummy::VerifyBeamHeader& r)
{
    r.m_Hdr.get_Hash(r.m_Hash, &r.m_RulesCfg);
    Env::Halt_if(!r.m_Hdr.IsValid(&r.m_RulesCfg));

    BeamDifficulty::Raw w0, w1;
    BeamDifficulty::Unpack(w1, r.m_Hdr.m_PoW.m_Difficulty);
    w0.FromBE_T(r.m_Hdr.m_ChainWork);
    w0 -= w1;
    w0.ToBE_T(r.m_ChainWork0);
}

export void Method_10(Dummy::TestFarCallStack& r)
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

export void Method_11(Dummy::TestRingSig& r)
{
    Env::Halt_if(!TestRingSignature(r.m_Msg, r.s_Ring, r.m_pPks, r.m_e, r.m_pK));
}
