#include "../common.h"
#include "../Math.h"
#include "contract.h"

namespace Fuddle {

struct MyState
    :public State
{
    MyState() {
        Env::LoadVar_T((uint8_t) s_Key, *this);
    }

    MyState(bool) {
        // no auto-load
    }

    void Save() {
        Env::SaveVar_T((uint8_t) s_Key, *this);
    }

    void AddSigAdmin() {
        Env::AddSig(m_Config.m_pkAdmin);
    }
};


BEAM_EXPORT void Ctor(const Method::Init& r)
{
    if (Env::get_CallDepth() > 1)
    {
        MyState s(false);
        _POD_(s.m_Config) = r.m_Config;
        s.m_Goals = 0;

        s.Save();
    }
}

BEAM_EXPORT void Dtor(void*)
{
    // ignore
}

void PayoutMove(const PubKey& pkUser, const AmountWithAsset& x, bool bAdd)
{
    if (!x.m_Amount)
        return;

    Payout::Key pk;
    _POD_(pk.m_pkUser) = pkUser;
    pk.m_Aid = x.m_Aid;

    if (bAdd)
        Env::FundsLock(x.m_Aid, x.m_Amount);
    else
        Env::FundsUnlock(x.m_Aid, x.m_Amount);

    Payout po;
    if (Env::LoadVar_T(pk, po))
    {
        if (bAdd)
            Strict::Add(po.m_Amount, x.m_Amount);
        else
        {
            Strict::Sub(po.m_Amount, x.m_Amount);

            if (!po.m_Amount)
            {
                Env::DelVar_T(pk);
                return;
            }
        }
    }
    else
    {
        Env::Halt_if(!bAdd);
        po.m_Amount = x.m_Amount;
    }

    Env::SaveVar_T(pk, po);
}

void AddLetter(const PubKey& pkUser, Letter::Char ch, uint32_t n = 1)
{
    if (!n)
        return;

    Letter::Key lk;
    _POD_(lk.m_Raw.m_pkUser) = pkUser;
    lk.m_Raw.m_Char = ch;

    Letter let;
    if (Env::LoadVar_T(lk, let))
        Strict::Add(let.m_Count, n);
    else
    {
        _POD_(let).SetZero();
        let.m_Count = n;
    }

    Env::SaveVar_T(lk, let);
}

BEAM_EXPORT void Method_2(void*)
{
    // called on upgrade
}

BEAM_EXPORT void Method_3(const Method::Withdraw& r)
{
    PayoutMove(r.m_pkUser, r.m_Val, false);
    Env::AddSig(r.m_pkUser);
}

BEAM_EXPORT void Method_4(const Method::SetPrice& r)
{
    Letter::Key lk;
    _POD_(lk.m_Raw) = r.m_Key;
    Letter let;
    Env::Halt_if(!Env::LoadVar_T(lk, let));

    _POD_(let.m_Price) = r.m_Price;
    Env::SaveVar_T(lk, let);

    Env::AddSig(r.m_Key.m_pkUser);
}

void DecAndSave(const Letter::Key& lk, Letter& let)
{
    if (--let.m_Count)
        Env::SaveVar_T(lk, let);
    else
        Env::DelVar_T(lk);
}

BEAM_EXPORT void Method_5(const Method::Buy& r)
{
    Letter::Key lk;
    _POD_(lk.m_Raw) = r.m_Key;
    Letter let;
    Env::Halt_if(!Env::LoadVar_T(lk, let));

    assert(let.m_Count);

    if (let.m_Price.m_Amount)
    {
        PayoutMove(r.m_Key.m_pkUser, let.m_Price, true);
        _POD_(let.m_Price).SetZero();
    }
    else
        // not for sale, must be just minted. Claim it.
        Env::Halt_if(!_POD_(r.m_Key.m_pkUser).IsZero());

    DecAndSave(lk, let);

    AddLetter(r.m_pkNewOwner, r.m_Key.m_Char);
}

BEAM_EXPORT void Method_6(const Method::Mint& r)
{
    PubKey pk;
    _POD_(pk) = r.m_Key.m_pkUser;
    AddLetter(pk, r.m_Key.m_Char, r.m_Count);

    MyState s;
    s.AddSigAdmin();
}

BEAM_EXPORT void Method_7(const Method::SetGoal& r)
{
    Env::Halt_if(!r.m_Prize.m_Amount);
    Env::FundsLock(r.m_Prize.m_Aid, r.m_Prize.m_Amount);

    MyState s;
    s.AddSigAdmin();

    Goal::Key qk;
    qk.m_ID = ++s.m_Goals;
    s.Save();

    Env::Halt_if(r.m_Len > Goal::s_MaxLen);
    uint32_t nSizeChars = sizeof(Letter::Char) * r.m_Len;
    uint32_t nSize = sizeof(Goal) + nSizeChars;

    auto* pQ = (Goal*) Env::StackAlloc(nSize);
    pQ->m_Prize = r.m_Prize;
    Env::Memcpy(pQ + 1, &r + 1, nSizeChars);

    Env::SaveVar(&qk, sizeof(qk), pQ, nSize, KeyTag::Internal);
}

BEAM_EXPORT void Method_8(const Method::SolveGoal& r)
{
    Goal::Key qk;
    qk.m_ID = r.m_iGoal;

    GoalMax q;
    auto nSize = Env::LoadVar(&qk, sizeof(qk), &q, sizeof(q), KeyTag::Internal);
    Env::Halt_if(nSize < sizeof(Goal));

    auto nLen = (nSize - sizeof(Goal)) / sizeof(Letter::Char);

    Letter::Key lk;
    _POD_(lk.m_Raw.m_pkUser) = r.m_pkUser;

    for (uint32_t i = 0; i < nLen; i++)
    {
        lk.m_Raw.m_Char = q.m_pCh[i];

        Letter let;
        Env::Halt_if(!Env::LoadVar_T(lk, let));
        DecAndSave(lk, let);
    }

    Env::FundsUnlock(q.m_Prize.m_Aid, q.m_Prize.m_Amount);

    Env::DelVar_T(qk);

    Env::AddSig(r.m_pkUser);
}


} // namespace Fuddle
