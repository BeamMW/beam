#include "../common.h"
#include "../app_common_impl.h"
#include "../upgradable3/app_common_impl.h"
#include "contract.h"

#define Amm_admin_schedule_upgrade(macro) Upgradable3_schedule_upgrade(macro)
#define Amm_admin_replace_admin(macro) Upgradable3_replace_admin(macro)
#define Amm_admin_set_min_approvers(macro) Upgradable3_set_min_approvers(macro)
#define Amm_admin_explicit_upgrade(macro) macro(ContractID, cid)

#define Amm_admin_deploy(macro) Upgradable3_deploy(macro)
#define Amm_admin_view(macro)
#define Amm_admin_destroy(macro) macro(ContractID, cid)
#define Amm_admin_pools_view(macro) macro(ContractID, cid)

#define Amm_poolop(macro) \
    macro(ContractID, cid) \
    macro(AssetID, aid1) \
    macro(AssetID, aid2)

#define Amm_admin_pool_view(macro) Amm_poolop(macro)

#define AmmRole_admin(macro) \
    macro(admin, view) \
    macro(admin, destroy) \
    macro(admin, deploy) \
	macro(admin, schedule_upgrade) \
	macro(admin, replace_admin) \
	macro(admin, set_min_approvers) \
	macro(admin, explicit_upgrade) \
    macro(admin, pool_view) \
    macro(admin, pools_view) \

#define Amm_user_pool_create(macro) Amm_poolop(macro)
#define Amm_user_pool_destroy(macro) Amm_poolop(macro)

#define Amm_user_add_liquidity(macro) \
    Amm_poolop(macro) \
    macro(Amount, val1) \
    macro(Amount, val2) \
    macro(uint32_t, bPredictOnly)

#define Amm_user_withdraw(macro) \
    Amm_poolop(macro) \
    macro(Amount, ctl) \
    macro(uint32_t, bPredictOnly)

#define Amm_user_trade(macro) \
    Amm_poolop(macro) \
    macro(Amount, val1_buy) \
    macro(uint32_t, bPredictOnly)

#define AmmRole_user(macro) \
    macro(user, pool_create) \
    macro(user, pool_destroy) \
    macro(user, add_liquidity) \
    macro(user, withdraw) \
    macro(user, trade) \


#define AmmRoles_All(macro) \
    macro(admin) \
    macro(user)

namespace Amm {

using MultiPrecision::Float;

BEAM_EXPORT void Method_0()
{
    // scheme
    Env::DocGroup root("");

    {   Env::DocGroup gr("roles");

#define THE_FIELD(type, name) Env::DocAddText(#name, #type);
#define THE_METHOD(role, name) { Env::DocGroup grMethod(#name);  Amm_##role##_##name(THE_FIELD) }
#define THE_ROLE(name) { Env::DocGroup grRole(#name); AmmRole_##name(THE_METHOD) }
        
        AmmRoles_All(THE_ROLE)
#undef THE_ROLE
#undef THE_METHOD
#undef THE_FIELD
    }
}

#define THE_FIELD(type, name) const type& name,
#define ON_METHOD(role, name) void On_##role##_##name(Amm_##role##_##name(THE_FIELD) int unused = 0)

void OnError(const char* sz)
{
    Env::DocAddText("error", sz);
}

const char g_szAdminSeed[] = "upgr3-amm";

struct AdminKeyID :public Env::KeyID {
    AdminKeyID() :Env::KeyID(g_szAdminSeed, sizeof(g_szAdminSeed)) {}
};

const Upgradable3::Manager::VerInfo g_VerInfo = { s_pSID, _countof(s_pSID) };

ON_METHOD(admin, view)
{
    AdminKeyID kid;
    g_VerInfo.DumpAll(&kid);
}

ON_METHOD(admin, deploy)
{
    AdminKeyID kid;
    PubKey pk;
    kid.get_Pk(pk);

    Method::Create arg;
    if (!g_VerInfo.FillDeployArgs(arg.m_Upgradable, &pk))
        return;

    const uint32_t nCharge =
        Upgradable3::Manager::get_ChargeDeploy() +
        Env::Cost::Refs +
        Env::Cost::Cycle * 20;

    Env::GenerateKernel(nullptr, arg.s_iMethod, &arg, sizeof(arg), nullptr, 0, nullptr, 0, "Deploy Amm contract", nCharge);
}

ON_METHOD(admin, destroy)
{
    Env::GenerateKernel(&cid, 1, nullptr, 0, nullptr, 0, nullptr, 0, "destroy Amm contract", 0);
}

ON_METHOD(admin, schedule_upgrade)
{
    AdminKeyID kid;
    g_VerInfo.ScheduleUpgrade(cid, kid, hTarget);
}

ON_METHOD(admin, explicit_upgrade)
{
    Upgradable3::Manager::MultiSigRitual::Perform_ExplicitUpgrade(cid);
}

ON_METHOD(admin, replace_admin)
{
    AdminKeyID kid;
    Upgradable3::Manager::MultiSigRitual::Perform_ReplaceAdmin(cid, kid, iAdmin, pk);
}

ON_METHOD(admin, set_min_approvers)
{
    AdminKeyID kid;
    Upgradable3::Manager::MultiSigRitual::Perform_SetApprovers(cid, kid, newVal);
}

bool SetKey(Pool::ID& pid, bool& bReverse, AssetID aid1, AssetID aid2)
{
    bReverse = (aid1 > aid2);
    if (bReverse)
    {
        pid.m_Aid1 = aid2;
        pid.m_Aid2 = aid1;
    }
    else
    {
        if (aid1 == aid2)
        {
            OnError("assets must be different");
            return false;
        }

        pid.m_Aid1 = aid1;
        pid.m_Aid2 = aid2;
    }

    return true;
}

bool SetKey(Pool::ID& pid, AssetID aid1, AssetID aid2)
{
    bool bDummy;
    return SetKey(pid, bDummy, aid1, aid2);
}

struct FloatTxt
{
    static const uint32_t s_DigsAfterDotMax = 18;
    static const uint32_t s_TxtLenMax = Utils::String::Decimal::DigitsMax<uint32_t>::N + s_DigsAfterDotMax + 6; // 1st dig, dot, space, E, space, minus

    static uint32_t Print_(char* szBuf, Float x, uint32_t nDigitsAfterDot)
    {
        if (x.IsZero())
        {
            szBuf[0] = '0';
            szBuf[1] = 0;
            return 1;
        }

        if (nDigitsAfterDot > s_DigsAfterDotMax)
            nDigitsAfterDot = s_DigsAfterDotMax;

        uint64_t trgLo = 1;
        for (uint32_t i = 0; i < nDigitsAfterDot; i++)
            trgLo *= 10;

        uint64_t trgHi = trgLo * 10;

        int ord = nDigitsAfterDot;

        Float one(1u);
        Float dec(10u);

        uint64_t x_ = x.Get();
        if (x_ < trgLo)
        {
            do
            {
                x = x * dec;
                ord--;
            } while ((x_ = x.Get()) < trgLo);

            if (x_ >= trgHi)
            {
                x_ /= 10;
                ord++;
            }
        }
        else
        {
            if (x_ >= trgHi)
            {
                Float dec_inv = one / dec;

                do
                {
                    x = x * dec_inv;
                    ord++;
                } while ((x_ = x.Get()) >= trgHi);

                if (x_ < trgLo)
                {
                    x_ *= 10;
                    ord--;
                }
            }
        }

        uint32_t nPos = 0;

        szBuf[nPos++] = '0' + (x_ / trgLo);
        szBuf[nPos++] = '.';

        Utils::String::Decimal::Print(szBuf + nPos, x_ - trgLo, nDigitsAfterDot);
        nPos += nDigitsAfterDot;

        if (ord)
        {
            szBuf[nPos++] = ' ';
            szBuf[nPos++] = 'E';

            if (ord > 0)
                szBuf[nPos++] = ' ';
            else
            {
                ord = -ord;
                szBuf[nPos++] = '-';
            }

            nPos += Utils::String::Decimal::Print(szBuf + nPos, ord);
        }

        szBuf[nPos] = 0;
        return nPos;
    }

    char m_sz[s_TxtLenMax + 1];
    uint32_t Print(Float f, uint32_t nDigitsAfterDot = 10)
    {
        return Print_(m_sz, f, nDigitsAfterDot);
    }
};

void DocAddRate(const char* sz, Amount v1, Amount v2)
{
    FloatTxt ftxt;
    ftxt.Print(Float(v1) / Float(v2));
    Env::DocAddText(sz, ftxt.m_sz);
}

#pragma pack (push, 1)
struct UserKeyMaterial
{
    ContractID m_Cid;
    Pool::ID m_Pid;
};
#pragma pack (pop)

struct PoolsWalker
{
    Env::VarReaderEx<true> m_R;
    Env::Key_T<Pool::Key> m_Key;
    Pool m_Pool;

    PoolsWalker()
    {
        Env::SelectContext(1, 0); // dependent
    }

    void Enum(const ContractID& cid)
    {
        _POD_(m_Key.m_Prefix.m_Cid) = cid;
        _POD_(m_Key.m_KeyInContract.m_ID).SetZero();

        Env::Key_T<Pool::Key> key2;
        _POD_(key2.m_Prefix.m_Cid) = cid;
        _POD_(key2.m_KeyInContract.m_ID).SetObject(0xff);

        m_R.Enum_T(m_Key, key2);
    }

    void Enum(const ContractID& cid, const Pool::ID& pid)
    {
        _POD_(m_Key.m_Prefix.m_Cid) = cid;
        m_Key.m_KeyInContract.m_ID = pid;

        m_R.Enum_T(m_Key, m_Key);
    }

    bool Move()
    {
        return m_R.MoveNext_T(m_Key, m_Pool);
    }

    bool MoveMustExist()
    {
        if (Move())
            return true;
        OnError("no such a pool");
        return false;
    }

    static void PrintAmounts(const Amounts& x)
    {
        Env::DocAddNum("tok1", x.m_Tok1);
        Env::DocAddNum("tok2", x.m_Tok2);
    }

    static void PrintTotals(const Totals& x)
    {
        Env::DocAddNum("ctl", x.m_Ctl);
        PrintAmounts(x);
    }

    bool IsCreator() const
    {
        UserKeyMaterial ukm;
        _POD_(ukm.m_Cid) = m_Key.m_Prefix.m_Cid;
        _POD_(ukm.m_Pid) = m_Key.m_KeyInContract.m_ID;
        return IsCreator(ukm);

    }

    bool IsCreator(const UserKeyMaterial& ukm) const
    {
        PubKey pk;
        Env::DerivePk(pk, &ukm, sizeof(ukm));
        return _POD_(pk) == m_Pool.m_pkCreator;
    }

    void PrintPool() const
    {
        PrintTotals(m_Pool.m_Totals);
        Env::DocAddNum("tid", m_Pool.m_aidCtl);

        if (m_Pool.m_Totals.m_Ctl)
        {
            DocAddRate("k1_2", m_Pool.m_Totals.m_Tok1, m_Pool.m_Totals.m_Tok2);
            DocAddRate("k2_1", m_Pool.m_Totals.m_Tok2, m_Pool.m_Totals.m_Tok1);
            DocAddRate("k1_ctl", m_Pool.m_Totals.m_Tok1, m_Pool.m_Totals.m_Ctl);
            DocAddRate("k2_ctl", m_Pool.m_Totals.m_Tok2, m_Pool.m_Totals.m_Ctl);
        }

        if (IsCreator())
            Env::DocAddNum32("creator", 1);
    }

    void PrintKey() const
    {
        Env::DocAddNum("aid1", m_Key.m_KeyInContract.m_ID.m_Aid1);
        Env::DocAddNum("aid2", m_Key.m_KeyInContract.m_ID.m_Aid2);
    }

};

ON_METHOD(admin, pools_view)
{
    Env::DocArray gr("res");

    PoolsWalker pw;
    for (pw.Enum(cid); pw.Move(); )
    {
        Env::DocGroup gr1("");

        pw.PrintKey();
        pw.PrintPool();
    }
}

ON_METHOD(admin, pool_view)
{
    Pool::ID pid;
    if (!SetKey(pid, aid1, aid2))
        return;

    PoolsWalker pw;
    pw.Enum(cid, pid);
    if (pw.MoveMustExist())
    {
        Env::DocGroup gr("res");
        pw.PrintPool();
    }
}

ON_METHOD(user, pool_create)
{
    Method::PoolCreate arg;
    if (!SetKey(arg.m_Pid, aid1, aid2))
        return;

    PoolsWalker pw;
    pw.Enum(cid, arg.m_Pid);
    if (pw.Move())
        return OnError("pool already exists");

    UserKeyMaterial ukm;
    _POD_(ukm.m_Cid) = cid;
    _POD_(ukm.m_Pid) = arg.m_Pid;
    Env::DerivePk(arg.m_pkCreator, &ukm, sizeof(ukm));

    FundsChange fc;
    fc.m_Aid = 0;
    fc.m_Consume = 1;
    fc.m_Amount = g_Beam2Groth * 10;

    const uint32_t nCharge =
        Env::Cost::CallFar +
        Env::Cost::AssetManage +
        Env::Cost::SaveVar_For(sizeof(Pool)) +
        Env::Cost::Cycle * 200;

    Env::GenerateKernel(&cid, arg.s_iMethod, &arg, sizeof(arg), &fc, 1, nullptr, 0, "Amm create pool", nCharge);
}


ON_METHOD(user, pool_destroy)
{
    Method::PoolDestroy arg;
    if (!SetKey(arg.m_Pid, aid1, aid2))
        return;

    PoolsWalker pw;
    pw.Enum(cid, arg.m_Pid);
    if (!pw.MoveMustExist())
        return;

    UserKeyMaterial ukm;

    if (!pw.IsCreator(ukm))
        return OnError("not creator");

    if (pw.m_Pool.m_Totals.m_Ctl)
        return OnError("poo not empty");

    FundsChange fc;
    fc.m_Aid = 0;
    fc.m_Consume = 0;
    fc.m_Amount = g_Beam2Groth;

    Env::KeyID kid(ukm);

    const uint32_t nCharge =
        Env::Cost::CallFar +
        Env::Cost::AssetManage +
        Env::Cost::LoadVar_For(sizeof(Pool)) +
        Env::Cost::SaveVar +
        Env::Cost::AddSig +
        Env::Cost::Cycle * 200;

    Env::GenerateKernel(&cid, arg.s_iMethod, &arg, sizeof(arg), &fc, 1, &kid, 1, "Amm destroy pool", nCharge);
}

ON_METHOD(user, add_liquidity)
{
    Method::AddLiquidity arg;
    bool bReverse;
    if (!SetKey(arg.m_Pid, bReverse, aid1, aid2))
        return;

    arg.m_Amounts.m_Tok1 = val1;
    arg.m_Amounts.m_Tok2 = val2;

    PoolsWalker pw;
    pw.Enum(cid, arg.m_Pid);
    if (!pw.MoveMustExist())
        return;

    pw.Enum(cid, arg.m_Pid);
    auto& t = pw.m_Pool.m_Totals; // alias
    Amount dCtl;

    if (!t.m_Ctl)
    {
        if (!(val1 && val2))
            return OnError("pool empty - both tokens must be specified");

        t.AddInitial(arg.m_Amounts);
        dCtl = t.m_Ctl;
    }
    else
    {
        if (bReverse)
            t.Swap();

        int iAdjustDir = 0;
        Amount threshold = 0;
        if (val1)
        {
            if (!val2)
            {
                arg.m_Amounts.m_Tok2 = Float(t.m_Tok1) * Float(t.m_Tok2) / Float(val1);
                iAdjustDir = -1;
            }
        }
        else
        {
            if (!val2)
                return OnError("at least 1 token must be specified");

            arg.m_Amounts.m_Tok1 = Float(t.m_Tok1) * Float(t.m_Tok2) / Float(val2);
            iAdjustDir = 1;
        }

        int n = t.TestAdd(arg.m_Amounts);
        if (n)
            // TODO: auto-adjust values
            return OnError((n > 0) ? "val1 too large" : "val2 too large");

        dCtl = t.m_Ctl;
        t.Add(arg.m_Amounts);
        dCtl = t.m_Ctl - dCtl;
    }

    if (bPredictOnly)
    {
        Env::DocGroup gr("res");
        pw.PrintAmounts(arg.m_Amounts);
        Env::DocAddNum("ctl", dCtl);
    }
    else
    {
        if (bReverse)
            arg.m_Amounts.Swap();

        FundsChange pFc[3];
        pFc[0].m_Amount = arg.m_Amounts.m_Tok1;
        pFc[0].m_Aid = arg.m_Pid.m_Aid1;
        pFc[0].m_Consume = 1;
        pFc[1].m_Amount = arg.m_Amounts.m_Tok2;
        pFc[1].m_Aid = arg.m_Pid.m_Aid2;
        pFc[1].m_Consume = 1;
        pFc[2].m_Amount = dCtl;
        pFc[2].m_Aid = pw.m_Pool.m_aidCtl;
        pFc[2].m_Consume = 0;

        Env::GenerateKernel(&cid, arg.s_iMethod, &arg, sizeof(arg), pFc, _countof(pFc), nullptr, 0, "Amm add", 0);
    }
}

ON_METHOD(user, withdraw)
{
    Method::Withdraw arg;
    if (!SetKey(arg.m_Pid, aid1, aid2))
        return;

    if (!ctl)
        return OnError("withdraw ctl not specified");

    PoolsWalker pw;
    pw.Enum(cid, arg.m_Pid);
    if (!pw.MoveMustExist())
        return;
    

    Totals x;
    x.m_Ctl = ctl;
    Cast::Down<Amounts>(x) = pw.m_Pool.m_Totals.Remove(x.m_Ctl);

    if (bPredictOnly)
    {
        Env::DocGroup gr("res");
        pw.PrintTotals(x);
    }
    else
    {
        arg.m_Ctl = x.m_Ctl;

        FundsChange pFc[3];
        pFc[0].m_Amount = x.m_Tok1;
        pFc[0].m_Aid = arg.m_Pid.m_Aid1;
        pFc[0].m_Consume = 0;
        pFc[1].m_Amount = x.m_Tok2;
        pFc[1].m_Aid = arg.m_Pid.m_Aid2;
        pFc[1].m_Consume = 0;
        pFc[2].m_Amount = x.m_Ctl;
        pFc[2].m_Aid = pw.m_Pool.m_aidCtl;
        pFc[2].m_Consume = 1;

        Env::GenerateKernel(&cid, arg.s_iMethod, &arg, sizeof(arg), pFc, _countof(pFc), nullptr, 0, "Amm withdraw", 0);
    }
}

ON_METHOD(user, trade)
{
    Pool::ID pid;
    bool bReverse;
    if (!SetKey(pid, bReverse, aid1, aid2))
        return;

    if (!val1_buy)
        return OnError("buy amount not specified");

    PoolsWalker pw;
    pw.Enum(cid, pid);
    if (!pw.MoveMustExist())
        return;

    Pool& p = pw.m_Pool; // alias
    if (bReverse)
        p.m_Totals.Swap();

    Method::Trade arg;
    arg.m_Buy1 = val1_buy;

    if (arg.m_Buy1 >= p.m_Totals.m_Tok1)
    {
        if (p.m_Totals.m_Tok1 <= 1)
            return OnError("no liquidity");

        arg.m_Buy1 = p.m_Totals.m_Tok1 - 1; // truncate
    }

    Amount valPay = p.m_Totals.Trade(arg.m_Buy1);

    if (bPredictOnly)
    {
        Env::DocGroup gr("res");
        Env::DocAddNum("buy", arg.m_Buy1);
        Env::DocAddNum("pay", valPay);
    }
    else
    {
        arg.m_Pid.m_Aid1 = aid1; // order as specified, not normalized
        arg.m_Pid.m_Aid2 = aid2;

        FundsChange pFc[2];
        pFc[0].m_Amount = arg.m_Buy1;
        pFc[0].m_Aid = aid1;
        pFc[0].m_Consume = 0;
        pFc[1].m_Amount = valPay;
        pFc[1].m_Aid = aid2;
        pFc[1].m_Consume = 1;

        Env::GenerateKernel(&cid, arg.s_iMethod, &arg, sizeof(arg), pFc, _countof(pFc), nullptr, 0, "Amm trade", 0);
    }
}

#undef ON_METHOD
#undef THE_FIELD

BEAM_EXPORT void Method_1() 
{
    Env::DocGroup root("");

    char szRole[0x10], szAction[0x10];

    if (!Env::DocGetText("role", szRole, sizeof(szRole)))
        return OnError("Role not specified");

    if (!Env::DocGetText("action", szAction, sizeof(szAction)))
        return OnError("Action not specified");

    const char* szErr = nullptr;

#define PAR_READ(type, name) type arg_##name; Env::DocGet(#name, arg_##name);
#define PAR_PASS(type, name) arg_##name,

#define THE_METHOD(role, name) \
        if (!Env::Strcmp(szAction, #name)) { \
            Amm_##role##_##name(PAR_READ) \
            On_##role##_##name(Amm_##role##_##name(PAR_PASS) 0); \
            return; \
        }

#define THE_ROLE(name) \
    if (!Env::Strcmp(szRole, #name)) { \
        AmmRole_##name(THE_METHOD) \
        return OnError("invalid Action"); \
    }

    AmmRoles_All(THE_ROLE)

#undef THE_ROLE
#undef THE_METHOD
#undef PAR_PASS
#undef PAR_READ

    OnError("unknown Role");
}

} // namespace Amm
