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

void User_Modify(Token::ID tid, const PubKey& pk, Amount val, bool bAdd)
{
    User::Key uk;
    uk.m_Tid = tid;
    _POD_(uk.m_pk) = pk;

    Amount val0;
    User_get(val0, uk);

    if (bAdd)
        Strict::Add(val0, val);
    else
        Strict::Sub(val0, val);

    if (val0)
        Env::SaveVar_T(uk, val0);
    else
        Env::DelVar_T(uk);

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
    t.m_Limit = r.m_Limit;
    _POD_(t.m_Mint).SetZero();
    _POD_(t.m_pkOwner) = t.m_pkOwner;

    Env::SaveVar_T(tk, t);
}

void AddSigPlus(const PubKey& pk)
{
    if (pk.Y > 1)
    {
        // contract
        ContractID cid;
        Env::get_CallerCid(1, cid);
        Env::Halt_if(_POD_(cid) != pk.X);
    }
    else
        Env::AddSig(pk);
}

BEAM_EXPORT void Method_5(Method::Mint& r)
{
    Token::Key tk;
    tk.m_ID = r.m_Tid;

    Token t;
    Env::Halt_if(!Env::LoadVar_T(tk, t));

    if (r.m_Mint)
    {
        t.m_Mint.m_Lo += r.m_Value;
        if (t.m_Mint.m_Lo < r.m_Value) // overflow
        {
            ++t.m_Mint.m_Hi;
            Env::Halt_if(!t.m_Mint.m_Hi); // overflow
        }

        // check limits
        bool bOverflow =
            (t.m_Mint.m_Hi > t.m_Limit.m_Hi) ||
            ((t.m_Mint.m_Hi == t.m_Limit.m_Hi) && (t.m_Mint.m_Lo > t.m_Limit.m_Lo));

        Env::Halt_if(bOverflow);
    }
    else
    {
        // burn
        if (t.m_Mint.m_Lo < r.m_Value)
        {
            Env::Halt_if(!t.m_Mint.m_Hi);
            --t.m_Mint.m_Hi;
        }
        t.m_Mint.m_Lo -= r.m_Value;

        if (_POD_(r.m_pkUser) != t.m_pkOwner)
            AddSigPlus(r.m_pkUser);
    }

    User_Modify(r.m_Tid, r.m_pkUser, r.m_Value, !!r.m_Mint);

    Env::SaveVar_T(tk, t);
    AddSigPlus(t.m_pkOwner);
}

BEAM_EXPORT void Method_6(Method::Transfer& r)
{
    User_Modify(r.m_Tid, r.m_pkUser, r.m_Value, false);
    User_Modify(r.m_Tid, r.m_pkDst, r.m_Value, true);

    AddSigPlus(r.m_pkUser);
}

} // namespace Mintor
