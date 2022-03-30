////////////////////////
#include "../common.h"
#include "../Math.h"
#include "contract.h"

namespace Mintor {


BEAM_EXPORT void Ctor(const void*)
{
}

BEAM_EXPORT void Dtor(void*)
{
}

BEAM_EXPORT void Method_2(Method::View& r)
{
    Token::Key tk;
    tk.m_ID = r.m_Tid;

    if (!Env::LoadVar_T(tk, r.m_Result))
        _POD_(r.m_Result).SetZero();
}

void AddSigPlus(const PubKey& pk)
{
    switch (pk.m_Y)
    {
    case 0:
    case 1:
        Env::AddSig(pk);
        break;

    case PubKeyFlag::s_Cid:
    {
        // contract
        ContractID cid;
        Env::get_CallerCid(1, cid);
        Env::Halt_if(_POD_(cid) != pk.m_X);
    }
    break;

    default:
        Env::Halt(); // invalid
    }
}

void User_get(Amount& res, const User::Key& uk)
{
    if (!Env::LoadVar_T(uk, res))
        res = 0;
}

void User_get(Amount& res, Token::ID tid, const PubKey& pk)
{
    User::Key uk;
    uk.m_Tid = tid;
    _POD_(uk.m_pk) = pk;

    User_get(res, uk);
}

void User_Modify(Token::ID tid, Token& t, const PubKey& pk, Amount val, bool bAdd)
{
    if (pk.m_Y == PubKeyFlag::s_CA)
    {
        if (!t.m_Aid)
        {
            Token::Key tk;
            tk.m_ID = tid;

            Env::Halt_if(!Env::LoadVar_T(tk, t) || !t.m_Aid);
        }

        Env::Halt_if(!t.m_Aid);
        if (bAdd)
        {
            Env::Halt_if(!Env::AssetEmit(t.m_Aid, val, 1));
            Env::FundsUnlock(t.m_Aid, val);
        }
        else
        {
            Env::FundsLock(t.m_Aid, val);
            Env::Halt_if(!Env::AssetEmit(t.m_Aid, val, 0));
        }
    }
    else
    {
        User::Key uk;
        uk.m_Tid = tid;
        _POD_(uk.m_pk) = pk;

        Amount val0;
        User_get(val0, uk);

        if (bAdd)
            Strict::Add(val0, val);
        else
        {
            Strict::Sub(val0, val);
            AddSigPlus(pk);
        }

        if (val0)
            Env::SaveVar_T(uk, val0);
        else
            Env::DelVar_T(uk);
    }
}

BEAM_EXPORT void Method_3(Method::ViewUser& r)
{
    User_get(r.m_Result, r.m_Tid, r.m_pkUser);
}

BEAM_EXPORT void Method_4(Method::Create& r)
{
    Global g;
    uint8_t gk = Tags::s_Global;
    if (!Env::LoadVar_T(gk, g))
        g.m_Tokens = 0;

    r.m_Tid = ++g.m_Tokens;
    Env::SaveVar_T(gk, g);

    Token::Key tk;
    tk.m_ID = g.m_Tokens;

    Token t;
    _POD_(t).SetZero();
    t.m_Limit = r.m_Limit;
    _POD_(t.m_pkOwner) = t.m_pkOwner;

    Env::SaveVar_T(tk, t);
}

BEAM_EXPORT void Method_5(Method::Mint& r)
{
    Token::Key tk;
    tk.m_ID = r.m_Tid;

    Token t;
    Env::Halt_if(!Env::LoadVar_T(tk, t));

    if (r.m_Mint)
    {
        t.m_Mint += r.m_Value; // woulf fail on overflow

        // check limits
        bool bOverflow =
            (t.m_Mint.m_Hi > t.m_Limit.m_Hi) ||
            ((t.m_Mint.m_Hi == t.m_Limit.m_Hi) && (t.m_Mint.m_Lo > t.m_Limit.m_Lo));

        Env::Halt_if(bOverflow);
    }
    else
        // burn
        t.m_Mint -= r.m_Value; // woulf fail on overflow

    User_Modify(r.m_Tid, t, r.m_pkUser, r.m_Value, !!r.m_Mint);

    Env::SaveVar_T(tk, t);

    if (r.m_Mint || (_POD_(r.m_pkUser) != t.m_pkOwner))
        AddSigPlus(t.m_pkOwner);
}

BEAM_EXPORT void Method_6(Method::Transfer& r)
{
    Token t;
    t.m_Aid = 0;

    User_Modify(r.m_Tid, t, r.m_pkUser, r.m_Value, false);
    User_Modify(r.m_Tid, t, r.m_pkDst, r.m_Value, true);
}

BEAM_EXPORT void Method_7(Method::CreateCA& r)
{
    Token::Key tk;
    tk.m_ID = r.m_Tid;

    Token t;
    Env::Halt_if(!Env::LoadVar_T(tk, t) || t.m_Aid);

    // generate unique metadata
    static const char s_szMeta[] = "STD:SCH_VER=1;N=Mintor Generic Token;SN=Generic;UN=GENERIC;NTHUN=GROTH";

#pragma pack (push, 1)
    struct Meta {
        char m_szMeta[sizeof(s_szMeta)]; // including 0-terminator
        Token::ID m_Tid;
    } md;
#pragma pack (pop)

    Env::Memcpy(md.m_szMeta, s_szMeta, sizeof(s_szMeta));
    md.m_Tid = r.m_Tid;

    t.m_Aid = Env::AssetCreate(&md, sizeof(md));
    Env::Halt_if(!t.m_Aid);

    Env::SaveVar_T(tk, t);
}

} // namespace Mintor
