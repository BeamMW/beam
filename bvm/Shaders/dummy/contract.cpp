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
