#include "../common.h"
#include "../app_common_impl.h"
#include "../upgradable3/app_common_impl.h"
#include "contract.h"

#define Amm_schedule_upgrade(macro) Upgradable3_schedule_upgrade(macro)
#define Amm_replace_admin(macro) Upgradable3_replace_admin(macro)
#define Amm_set_min_approvers(macro) Upgradable3_set_min_approvers(macro)
#define Amm_explicit_upgrade(macro) macro(ContractID, cid)

#define Amm_deploy(macro) \
    Upgradable3_deploy(macro) \
    macro(ContractID, cidDaoVault)

#define Amm_view_deployed(macro)
#define Amm_destroy(macro) macro(ContractID, cid)
#define Amm_pools_view(macro) macro(ContractID, cid)
#define Amm_view_all_assets(macro)

#define Amm_poolop(macro) \
    macro(ContractID, cid) \
    macro(AssetID, aid1) \
    macro(AssetID, aid2) \
    macro(uint32_t, kind)

#define Amm_pool_view(macro) Amm_poolop(macro)

#define Amm_pool_create(macro) Amm_poolop(macro)
#define Amm_pool_destroy(macro) Amm_poolop(macro)

#define Amm_pool_add_liquidity(macro) \
    Amm_poolop(macro) \
    macro(Amount, val1) \
    macro(Amount, val2) \
    macro(uint32_t, bPredictOnly)

#define Amm_pool_withdraw(macro) \
    Amm_poolop(macro) \
    macro(Amount, ctl) \
    macro(uint32_t, bPredictOnly)

#define Amm_pool_trade(macro) \
    Amm_poolop(macro) \
    macro(Amount, val1_buy) \
    macro(Amount, val2_pay) \
    macro(uint32_t, bPredictOnly)

#define AmmActions_All(macro) \
    macro(view_deployed) \
    macro(view_all_assets) \
    macro(destroy) \
    macro(deploy) \
	macro(schedule_upgrade) \
	macro(replace_admin) \
	macro(set_min_approvers) \
	macro(explicit_upgrade) \
    macro(pools_view) \
    macro(pool_view) \
    macro(pool_create) \
    macro(pool_destroy) \
    macro(pool_add_liquidity) \
    macro(pool_withdraw) \
    macro(pool_trade) \


namespace Amm {

using MultiPrecision::Float;

BEAM_EXPORT void Method_0()
{
    // scheme
    Env::DocGroup root("");

    {   Env::DocGroup gr("actions");

#define THE_FIELD(type, name) Env::DocAddText(#name, #type);
#define THE_ACTION(name) { Env::DocGroup gr(#name);  Amm_##name(THE_FIELD) }
        
        AmmActions_All(THE_ACTION)
#undef THE_ACTION
#undef THE_FIELD
    }
}

#define THE_FIELD(type, name) const type& name,
#define ON_METHOD(name) void On_##name(Amm_##name(THE_FIELD) int unused = 0)

void OnError(const char* sz)
{
    Env::DocAddText("error", sz);
}

const char g_szAdminSeed[] = "upgr3-amm";

struct AdminKeyID :public Env::KeyID {
    AdminKeyID() :Env::KeyID(g_szAdminSeed, sizeof(g_szAdminSeed)) {}
};

const Upgradable3::Manager::VerInfo g_VerInfo = { s_pSID, _countof(s_pSID) };

ON_METHOD(view_deployed)
{
    AdminKeyID kid;
    g_VerInfo.DumpAll(&kid);
}

ON_METHOD(deploy)
{
    AdminKeyID kid;
    PubKey pk;
    kid.get_Pk(pk);

    Method::Create arg;
    if (!g_VerInfo.FillDeployArgs(arg.m_Upgradable, &pk))
        return;

    _POD_(arg.m_Settings.m_cidDaoVault) = cidDaoVault;

    const uint32_t nCharge =
        Upgradable3::Manager::get_ChargeDeploy() +
        Env::Cost::Refs +
        Env::Cost::SaveVar_For(sizeof(Settings)) +
        Env::Cost::Cycle * 50;

    Env::GenerateKernel(nullptr, arg.s_iMethod, &arg, sizeof(arg), nullptr, 0, nullptr, 0, "Deploy Amm contract", nCharge);
}

ON_METHOD(destroy)
{
    Env::GenerateKernel(&cid, 1, nullptr, 0, nullptr, 0, nullptr, 0, "destroy Amm contract", 0);
}

ON_METHOD(schedule_upgrade)
{
    AdminKeyID kid;
    g_VerInfo.ScheduleUpgrade(cid, kid, hTarget);
}

ON_METHOD(explicit_upgrade)
{
    Upgradable3::Manager::MultiSigRitual::Perform_ExplicitUpgrade(cid);
}

ON_METHOD(replace_admin)
{
    AdminKeyID kid;
    Upgradable3::Manager::MultiSigRitual::Perform_ReplaceAdmin(cid, kid, iAdmin, pk);
}

ON_METHOD(set_min_approvers)
{
    AdminKeyID kid;
    Upgradable3::Manager::MultiSigRitual::Perform_SetApprovers(cid, kid, newVal);
}

bool SetKey(Pool::ID& pid, bool& bReverse, AssetID aid1, AssetID aid2, uint32_t kind)
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

    if (kind >= FeeSettings::s_Kinds)
    {
        OnError("invalid kind");
        return false;
    }

    pid.m_Fees.m_Kind = static_cast<uint8_t>(kind);

    return true;
}

bool SetKey(Pool::ID& pid, AssetID aid1, AssetID aid2, uint32_t kind)
{
    bool bDummy;
    return SetKey(pid, bDummy, aid1, aid2, kind);
}

void DocAddRate(const char* sz, Amount v1, Amount v2)
{
    char szBuf[Float::Text::s_LenMax + 1];
    (Float(v1) / Float(v2)).Print(szBuf);
    Env::DocAddText(sz, szBuf);
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
        uint32_t nHft = 0;
        Env::DocGet("hft", nHft);

        Env::SelectContext(!!nHft, 0);
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
        Env::DocAddNum("lp-token", m_Pool.m_aidCtl);

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
        Env::DocAddNum32("kind", m_Key.m_KeyInContract.m_ID.m_Fees.m_Kind);
    }

};

ON_METHOD(pools_view)
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

ON_METHOD(view_all_assets)
{
    Env::DocArray gr("res");

    auto iSlot = Env::Assets_Enum(0, static_cast<Height>(-1));
    while (true)
    {
        char szMetadata[1024 * 16 + 1]; // max metadata size is 16K
        AssetInfo ai;
        uint32_t nMetadata = sizeof(szMetadata) - 1;
        auto aid = Env::Assets_MoveNext(iSlot, ai, szMetadata, nMetadata, 0);
        if (!aid)
            break;

        Env::DocGroup gr1("");

        Env::DocAddNum("aid", aid);

        Env::DocAddNum("mintedLo", ai.m_ValueLo);
        Env::DocAddNum("mintedHi", ai.m_ValueHi);

        if (_POD_(ai.m_Cid).IsZero())
            Env::DocAddBlob_T("owner_pk", ai.m_Owner);
        else
            Env::DocAddBlob_T("owner_cid", ai.m_Cid);

        szMetadata[std::min<uint32_t>(nMetadata, sizeof(szMetadata) - 1)] = 0;
        Env::DocAddText("metadata", szMetadata);
    }
    Env::Assets_Close(iSlot);
}

ON_METHOD(pool_view)
{
    Pool::ID pid;
    if (!SetKey(pid, aid1, aid2, kind))
        return;

    PoolsWalker pw;
    pw.Enum(cid, pid);
    if (pw.MoveMustExist())
    {
        Env::DocGroup gr("res");
        pw.PrintPool();
    }
}

ON_METHOD(pool_create)
{
    Method::PoolCreate arg;
    if (!SetKey(arg.m_Pid, aid1, aid2, kind))
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


ON_METHOD(pool_destroy)
{
    Method::PoolDestroy arg;
    if (!SetKey(arg.m_Pid, aid1, aid2, kind))
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
    fc.m_Amount = g_Beam2Groth * 10;

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

int AdjustAddedValues(const Totals& t, Amounts& d, int iAdjustDir)
{
    int cmp = t.TestAdd(d);
    if (!cmp || !iAdjustDir)
        return cmp;

    Amount& v = (iAdjustDir > 0) ? d.m_Tok1 : d.m_Tok2;
    Amount v0 = v;

    // 1st phase: walk, while incrementing search radius, until the cmp sign changes
    for (Amount rad = 1; ; )
    {
        if (iAdjustDir == cmp)
        {
            v = v0 - rad;
            if (v > v0)
                v = 0;
        }
        else
        {
            v = v0 + rad;
            if (v < v0)
                v = static_cast<Amount>(-1);
        }

        int cmp2 = t.TestAdd(d);
        if (cmp2 != cmp)
        {
            if (!cmp2)
                return 0;
            break;
        }

        rad <<= 1;
        if (!rad)
            return cmp; // failed
    }

    // 2nd phase: median search
    Amount v1 = v;
    if (v1 < v0)
    {
        std::swap(v0, v1);
        cmp = -cmp;
    }

    while (true)
    {
        v = v0 + ((v1 - v0) >> 1); // overflow-resistant
        if (v == v0)
            return cmp; // oops

        int cmp2 = t.TestAdd(d);
        if (!cmp2)
            break;

        ((cmp2 == cmp) ? v0 : v1) = v;
    }

    return 0;
}

ON_METHOD(pool_add_liquidity)
{
    Method::AddLiquidity arg;
    bool bReverse;
    if (!SetKey(arg.m_Pid, bReverse, aid1, aid2, kind))
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
                (Float(val1) * Float(t.m_Tok2) / Float(t.m_Tok1)).Round(arg.m_Amounts.m_Tok2);
                iAdjustDir = -1;
            }
        }
        else
        {
            if (!val2)
                return OnError("at least 1 token must be specified");

            (Float(t.m_Tok1) * Float(val2) / Float(t.m_Tok2)).Round(arg.m_Amounts.m_Tok1);
            iAdjustDir = 1;
        }

        int n = AdjustAddedValues(t, arg.m_Amounts, iAdjustDir);
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

ON_METHOD(pool_withdraw)
{
    Method::Withdraw arg;
    if (!SetKey(arg.m_Pid, aid1, aid2, kind))
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

ON_METHOD(pool_trade)
{
    Pool::ID pid;
    bool bReverse;
    if (!SetKey(pid, bReverse, aid1, aid2, kind))
        return;

    PoolsWalker pw;
    pw.Enum(cid, pid);
    if (!pw.MoveMustExist())
        return;

    Pool& p = pw.m_Pool; // alias
    if (bReverse)
        p.m_Totals.Swap();

    if (p.m_Totals.m_Tok1 <= 1)
        return OnError("no liquidity");

    Method::Trade arg;
    Amount valPay, rawPay;
    TradeRes res;

    if (val1_buy)
    {
        arg.m_Buy1 = val1_buy;
        if (arg.m_Buy1 >= p.m_Totals.m_Tok1)
            arg.m_Buy1 = p.m_Totals.m_Tok1 - 1; // truncate
    }
    else
    {
        if (!val2_pay)
            return OnError("buy or pay amount must be specified");

        Amount thrLo, thrHi;
        {
            const Amount refVal = 0x20000000;
            pid.m_Fees.Get(res, refVal);
            Amount rawPay = Totals::ToAmount(Float(val2_pay) * Float(refVal) / Float(res.m_DaoFee + res.m_PayPool)); // reduced by fee proportion

            // init guess
            Amount guess = p.m_Totals.m_Tok1 - Totals::ToAmount(Float(p.m_Totals.m_Tok1) * Float(p.m_Totals.m_Tok2) / Float(p.m_Totals.m_Tok2 + rawPay));

            thrLo = guess - guess / 10000;
            if (thrLo)
                thrLo--;

            thrHi = guess + guess / 1000 + 1;
        }

        if (thrHi >= p.m_Totals.m_Tok1)
            thrHi = p.m_Totals.m_Tok1 - 1;

        if (thrLo >= thrHi)
            thrLo = thrHi - 1;

        // median search
        arg.m_Buy1 = 0;
        Amount val2_Best = 0;

        while (true)
        {
            Amount mid = thrLo + (thrHi - thrLo) / 2;

            Totals t2 = p.m_Totals;
            rawPay = t2.Trade(res, mid, pid.m_Fees);
            valPay = res.m_PayPool + res.m_DaoFee;

            // due to precision loss it could be impossible to find the exact solution. So we select the upper bound
            if ((valPay <= val2_pay) && (valPay >= val2_Best))
            {
                arg.m_Buy1 = mid; // don't stop the search even if exact match. Maybe we can get a little more for the same pay amount
                val2_Best = valPay;
            }

            if (thrLo >= thrHi)
                break;

            if (valPay > val2_pay)
                thrHi = mid;
            else
                thrLo = mid + 1;
        }
    }

    rawPay = p.m_Totals.Trade(res, arg.m_Buy1, pid.m_Fees);
    valPay = res.m_PayPool + res.m_DaoFee;


    if (bPredictOnly)
    {
        Env::DocGroup gr("res");
        Env::DocAddNum("buy", arg.m_Buy1);
        Env::DocAddNum("pay", res.m_PayPool + res.m_DaoFee);
        Env::DocAddNum("pay_raw", rawPay);
        Env::DocAddNum("fee_pool", res.m_PayPool - rawPay);
        Env::DocAddNum("fee_dao", res.m_DaoFee);
    }
    else
    {
        arg.m_Pid.m_Aid1 = aid1; // order as specified, not normalized
        arg.m_Pid.m_Aid2 = aid2;
        arg.m_Pid.m_Fees = pid.m_Fees;

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

    char szAction[0x20];

    if (!Env::DocGetText("action", szAction, sizeof(szAction)))
        return OnError("Action not specified");

#define PAR_READ(type, name) type arg_##name; Env::DocGet(#name, arg_##name);
#define PAR_PASS(type, name) arg_##name,

#define THE_METHOD(name) \
        static_assert(sizeof(szAction) >= sizeof(#name)); \
        if (!Env::Strcmp(szAction, #name)) { \
            Amm_##name(PAR_READ) \
            On_##name(Amm_##name(PAR_PASS) 0); \
            return; \
        }

    AmmActions_All(THE_METHOD)

#undef THE_METHOD
#undef PAR_PASS
#undef PAR_READ

    OnError("unknown action");
}

} // namespace Amm
