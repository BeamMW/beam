#include "../common.h"
#include "../app_common_impl.h"
#include "contract.h"
#include "../upgradable/contract.h"
#include "../upgradable/app_common_impl.h"
#include "../Math.h"

#define DemoXdao_manager_deploy_version(macro)
#define DemoXdao_manager_view(macro)
#define DemoXdao_manager_view_params(macro) macro(ContractID, cid)
#define DemoXdao_manager_lock(macro) \
    macro(ContractID, cid) \
    macro(Amount, amount)

#define DemoXdao_manager_farm_view(macro) \
    macro(ContractID, cid)

#define DemoXdao_manager_farm_update(macro) \
    macro(ContractID, cid) \
    macro(Amount, amountBeamX) \
    macro(Amount, amountBeam) \
    macro(uint32_t, bLockOrUnlock) \

#define DemoXdao_manager_prealloc_totals(macro) \
    macro(ContractID, cid)

#define DemoXdao_manager_prealloc_view(macro) \
    macro(ContractID, cid)

#define DemoXdao_manager_prealloc_withdraw(macro) \
    macro(ContractID, cid) \
    macro(Amount, amount)

#define DemoXdao_manager_my_xid(macro)

#define DemoXdao_manager_deploy_contract(macro) \
    macro(ContractID, cidVersion) \
    macro(Height, hUpgradeDelay)

#define DemoXdao_manager_schedule_upgrade(macro) \
    macro(ContractID, cid) \
    macro(ContractID, cidVersion) \
    macro(Height, dh)

#define DemoXdao_manager_view_stake(macro) macro(ContractID, cid)

#define DemoXdaoRole_manager(macro) \
    macro(manager, deploy_version) \
    macro(manager, view) \
    macro(manager, deploy_contract) \
    macro(manager, schedule_upgrade) \
    macro(manager, view_params) \
    macro(manager, view_stake) \
    macro(manager, my_xid) \
    macro(manager, prealloc_totals) \
    macro(manager, prealloc_view) \
    macro(manager, prealloc_withdraw) \
    macro(manager, farm_view) \
    macro(manager, farm_update)

#define DemoXdaoRoles_All(macro) \
    macro(manager)

export void Method_0()
{
    // scheme
    Env::DocGroup root("");

    {   Env::DocGroup gr("roles");

#define THE_FIELD(type, name) Env::DocAddText(#name, #type);
#define THE_METHOD(role, name) { Env::DocGroup grMethod(#name);  DemoXdao_##role##_##name(THE_FIELD) }
#define THE_ROLE(name) { Env::DocGroup grRole(#name); DemoXdaoRole_##name(THE_METHOD) }
        
        DemoXdaoRoles_All(THE_ROLE)
#undef THE_ROLE
#undef THE_METHOD
#undef THE_FIELD
    }
}

#define THE_FIELD(type, name) const type& name,
#define ON_METHOD(role, name) void On_##role##_##name(DemoXdao_##role##_##name(THE_FIELD) int unused = 0)

void OnError(const char* sz)
{
    Env::DocAddText("error", sz);
}

ON_METHOD(manager, view)
{
    static const ShaderID s_pSid[] = {
        DemoXdao::s_SID_0,
        DemoXdao::s_SID_1,
        DemoXdao::s_SID_2,
        DemoXdao::s_SID_3,
        DemoXdao::s_SID // latest version
    };

    ContractID pVerCid[_countof(s_pSid)];
    Height pVerDeploy[_countof(s_pSid)];

    WalkerUpgradable wlk;
    wlk.m_VerInfo.m_Count = _countof(s_pSid);
    wlk.m_VerInfo.s_pSid = s_pSid;
    wlk.m_VerInfo.m_pCid = pVerCid;
    wlk.m_VerInfo.m_pHeight = pVerDeploy;

    wlk.m_VerInfo.Init();
    wlk.m_VerInfo.Dump();

    Env::DocArray gr("contracts");

    PubKey pk;
    Env::DerivePk(pk, &Upgradable::s_SID, sizeof(Upgradable::s_SID));

    for (wlk.Enum(); wlk.MoveNext(); )
    {
        Env::DocGroup root("");
        wlk.DumpCurrent();

        Env::DocAddNum("owner", (uint32_t) ((_POD_(wlk.m_State.m_Pk) == pk) ? 1 : 0));
    }
}

ON_METHOD(manager, deploy_version)
{
    Env::GenerateKernel(nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0, "Deploy demoXdao bytecode", 0);
}

static const Amount g_DepositCA = 3000 * g_Beam2Groth; // 3K beams

ON_METHOD(manager, deploy_contract)
{
    FundsChange fc;
    fc.m_Aid = 0; // asset id
    fc.m_Amount = g_DepositCA; // amount of the input or output
    fc.m_Consume = 1; // contract consumes funds (i.e input, in this case)

    Upgradable::Create arg;
    _POD_(arg.m_Cid) = cidVersion;
    arg.m_hMinUpgadeDelay = hUpgradeDelay;
    Env::DerivePk(arg.m_Pk, &Upgradable::s_SID, sizeof(Upgradable::s_SID));

    Env::GenerateKernel(nullptr, 0, &arg, sizeof(arg), &fc, 1, nullptr, 0, "Deploy demoXdao contract", 0);
}

ON_METHOD(manager, schedule_upgrade)
{
    Upgradable::ScheduleUpgrade arg;
    _POD_(arg.m_cidNext) = cidVersion;
    arg.m_hNextActivate = Env::get_Height() + dh;

    SigRequest sig;
    sig.m_pID = &Upgradable::s_SID;
    sig.m_nID = sizeof(Upgradable::s_SID);

    Env::GenerateKernel(&cid, arg.s_iMethod, &arg, sizeof(arg), nullptr, 0, &sig, 1, "Upgradfe demoXdao contract version", 0);
}

Amount get_ContractLocked(AssetID aid, const ContractID& cid)
{
    Env::Key_T<AssetID> key;
    _POD_(key.m_Prefix.m_Cid) = cid;
    key.m_Prefix.m_Tag = KeyTag::LockedAmount;
    key.m_KeyInContract = Utils::FromBE(aid);

    struct AmountBig {
        Amount m_Hi;
        Amount m_Lo;
    };

    AmountBig val;
    if (!Env::VarReader::Read_T(key, val))
        return 0;

    return Utils::FromBE(val.m_Lo);
}

AssetID get_TrgAid(const ContractID& cid)
{
    Env::Key_T<uint8_t> key;
    _POD_(key.m_Prefix.m_Cid) = cid;
    key.m_KeyInContract = 0;

    DemoXdao::State s;
    if (!Env::VarReader::Read_T(key, s))
    {
        OnError("no such contract");
        return 0;
    }

    return s.m_Aid;
}

ON_METHOD(manager, view_params)
{
    auto aid = get_TrgAid(cid);
    if (!aid)
        return;

    Env::DocGroup gr("params");
    Env::DocAddNum("aid", aid);
    Env::DocAddNum("locked_demoX", get_ContractLocked(aid, cid));
    Env::DocAddNum("locked_beams", get_ContractLocked(0, cid));
}

ON_METHOD(manager, view_stake)
{
    Env::Key_T<PubKey> key;
    _POD_(key.m_Prefix.m_Cid) = cid;
    Env::DerivePk(key.m_KeyInContract, &cid, sizeof(cid));

    Amount amount;
    Env::DocAddNum("stake", Env::VarReader::Read_T(key, amount) ? amount : 0);
}

static const char g_szXid[] = "xid-seed";

ON_METHOD(manager, my_xid)
{
    PubKey pk;
    Env::DerivePk(pk, g_szXid, sizeof(g_szXid));
    Env::DocAddBlob_T("xid", pk);
}

template <typename TX, typename TY>
TY CalculateFraction(TY val, TX x, TX xTotal)
{
    MultiPrecision::UInt<sizeof(TY) / sizeof(MultiPrecision::Word)> res;
    res.SetDiv(MultiPrecision::From(x) * MultiPrecision::From(val), MultiPrecision::From(xTotal));

    return res.template Get<0, TY>();
}

Amount CalculatePreallocAvail(const ContractID& cid, Amount val)
{
    Env::Key_T<uint8_t> prk;
    _POD_(prk.m_Prefix.m_Cid) = cid;
    prk.m_KeyInContract = DemoXdao::Preallocated::s_Key;

    DemoXdao::Preallocated pr;
    if (!Env::VarReader::Read_T(prk, pr))
        return 0;

    Height dh = Env::get_Height() - pr.m_h0;
    if (dh >= pr.s_Duration)
        return val;

    return CalculateFraction(val, dh, pr.s_Duration);
}

ON_METHOD(manager, prealloc_totals)
{
    Amount valTotal = 0, valReceived = 0;

    Env::Key_T<DemoXdao::Preallocated::User::Key> k0, k1;
    _POD_(k0.m_Prefix.m_Cid) = cid;
    _POD_(k0.m_KeyInContract.m_Pk).SetZero();
    _POD_(k1.m_Prefix.m_Cid) = cid;
    _POD_(k1.m_KeyInContract.m_Pk).SetObject(0xff);

    for (Env::VarReader r(k0, k1); ; )
    {
        DemoXdao::Preallocated::User pu;
        if (!r.MoveNext_T(k0, pu))
            break;

        valReceived += pu.m_Received;
        valTotal += pu.m_Total;
    }

    // TODO: after the vesting period is over and the user withdraws all the preallocated - its record is erased completely.
    // Account for this. valTotal should be raised to the expected, valReceived raise by the same value as well

    Amount valAvail = CalculatePreallocAvail(cid, valTotal);

    Env::DocAddNum("total", valTotal);
    Env::DocAddNum("avail", valAvail);
    Env::DocAddNum("received", valReceived);
}

ON_METHOD(manager, prealloc_view)
{
    Env::Key_T<DemoXdao::Preallocated::User::Key> puk;
    _POD_(puk.m_Prefix.m_Cid) = cid;
    Env::DerivePk(puk.m_KeyInContract.m_Pk, g_szXid, sizeof(g_szXid));

    DemoXdao::Preallocated::User pu;
    if (Env::VarReader::Read_T(puk, pu))
    {
        Env::DocAddNum("total", pu.m_Total);
        Env::DocAddNum("received", pu.m_Received);

        // calculate the maximum available
        Amount nMaxAvail = CalculatePreallocAvail(cid, pu.m_Total);


        Env::DocAddNum("avail_total", nMaxAvail);
        Env::DocAddNum("avail_remaining", (nMaxAvail> pu.m_Received) ? (nMaxAvail - pu.m_Received) : 0);
    }
}

ON_METHOD(manager, prealloc_withdraw)
{
    auto aid = get_TrgAid(cid);
    if (!aid)
        return;

    DemoXdao::GetPreallocated arg;
    Env::DerivePk(arg.m_Pk, g_szXid, sizeof(g_szXid));
    arg.m_Amount = amount;
    
    SigRequest sig;
    sig.m_pID = g_szXid;
    sig.m_nID = sizeof(g_szXid);

    FundsChange fc;
    fc.m_Aid = aid;
    fc.m_Amount = amount;
    fc.m_Consume = 0;

    Env::GenerateKernel(&cid, arg.s_iMethod, &arg, sizeof(arg), &fc, 1, &sig, 1, "Get preallocated demoX tokens", 0);
}

ON_METHOD(manager, farm_view)
{
    auto aid = get_TrgAid(cid);
    if (!aid)
        return;

    Height h = Env::get_Height();

    DemoXdao::Farming::State fs;
    {
        Env::Key_T<uint8_t> fsk;
        _POD_(fsk.m_Prefix.m_Cid) = cid;
        fsk.m_KeyInContract = DemoXdao::Farming::s_Key;

        if (!Env::VarReader::Read_T(fsk, fs))
            _POD_(fs).SetZero();
        else
            fs.Update(h);

        fs.m_hLast = h;

        Env::DocGroup gr("farming");
        Env::DocAddNum("duation", fs.m_hTotal);
        Env::DocAddNum("emission", fs.get_EmissionSoFar());
    }

    DemoXdao::Farming::UserPos fup;
    {
        Env::Key_T<DemoXdao::Farming::UserPos::Key> fupk;
        _POD_(fupk.m_Prefix.m_Cid) = cid;
        Env::DerivePk(fupk.m_KeyInContract.m_Pk, &cid, sizeof(cid));

        if (!Env::VarReader::Read_T(fupk, fup))
        {
            _POD_(fup).SetZero();
            fup.m_SigmaLast = fs.m_Sigma;
        }
    }

    {
        Env::DocGroup gr("user");
        Env::DocAddNum("beams_locked", fup.m_Beam);
        Env::DocAddNum("beamX_old", fup.m_BeamX);

        Amount val = fs.RemoveFraction(fup);
        Env::DocAddNum("beamX_recent", val);
        Env::DocAddNum("beamX", fup.m_BeamX + val);
    }
}

ON_METHOD(manager, farm_update)
{
    auto aid = get_TrgAid(cid);
    if (!aid)
        return;

    DemoXdao::UpdPosFarming arg;
    arg.m_BeamLock = bLockOrUnlock;
    arg.m_Beam = amountBeam;
    arg.m_WithdrawBeamX = amountBeamX;

    Env::DerivePk(arg.m_Pk, &cid, sizeof(cid));

    SigRequest sig;
    sig.m_pID = &cid;
    sig.m_nID = sizeof(cid);

    FundsChange pFc[2];
    pFc[0].m_Aid = aid;
    pFc[0].m_Amount = amountBeamX;
    pFc[0].m_Consume = 0;
    pFc[1].m_Aid = 0;
    pFc[1].m_Amount = amountBeam;
    pFc[1].m_Consume = bLockOrUnlock;

    Env::GenerateKernel(&cid, arg.s_iMethod, &arg, sizeof(arg), pFc, _countof(pFc), &sig, 1, "Lock/Unlock and get farmed demoX tokens", 0);

}

#undef ON_METHOD
#undef THE_FIELD

export void Method_1() 
{
    Env::DocGroup root("");

    char szRole[0x20], szAction[0x20];

    if (!Env::DocGetText("role", szRole, sizeof(szRole)))
        return OnError("Role not specified");

    if (!Env::DocGetText("action", szAction, sizeof(szAction)))
        return OnError("Action not specified");

    const char* szErr = nullptr;

#define PAR_READ(type, name) type arg_##name; Env::DocGet(#name, arg_##name);
#define PAR_PASS(type, name) arg_##name,

#define THE_METHOD(role, name) \
        if (!Env::Strcmp(szAction, #name)) { \
            DemoXdao_##role##_##name(PAR_READ) \
            On_##role##_##name(DemoXdao_##role##_##name(PAR_PASS) 0); \
            return; \
        }

#define THE_ROLE(name) \
    if (!Env::Strcmp(szRole, #name)) { \
        DemoXdaoRole_##name(THE_METHOD) \
        return OnError("invalid Action"); \
    }

    DemoXdaoRoles_All(THE_ROLE)

#undef THE_ROLE
#undef THE_METHOD
#undef PAR_PASS
#undef PAR_READ

    OnError("unknown Role");
}

