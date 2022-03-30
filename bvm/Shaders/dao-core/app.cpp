#include "../common.h"
#include "../app_common_impl.h"
#include "contract.h"
#include "../upgradable2/contract.h"
#include "../upgradable2/app_common_impl.h"

#define DaoCore_manager_view(macro)
#define DaoCore_manager_view_params(macro) macro(ContractID, cid)

#define DaoCore_manager_farm_view(macro) \
    macro(ContractID, cid)

#define DaoCore_manager_farm_get_yield(macro) \
    macro(ContractID, cid) \
    macro(Amount, amount) \
    macro(Height, hPeriod)

#define DaoCore_manager_farm_totals(macro) \
    macro(ContractID, cid)

#define DaoCore_manager_farm_update(macro) \
    macro(ContractID, cid) \
    macro(Amount, amountBeamX) \
    macro(Amount, amountBeam) \
    macro(uint32_t, bLockOrUnlock) \

#define DaoCore_manager_prealloc_totals(macro) \
    macro(ContractID, cid)

#define DaoCore_manager_prealloc_view(macro) \
    macro(ContractID, cid)

#define DaoCore_manager_prealloc_withdraw(macro) \
    macro(ContractID, cid) \
    macro(Amount, amount)

#define DaoCore_manager_my_xid(macro)

#define DaoCore_manager_my_admin_key(macro)

#define DaoCore_manager_explicit_upgrade(macro) macro(ContractID, cid)

#define DaoCoreRole_manager(macro) \
    macro(manager, view) \
    macro(manager, explicit_upgrade) \
    macro(manager, view_params) \
    macro(manager, my_xid) \
    macro(manager, my_admin_key) \
    macro(manager, prealloc_totals) \
    macro(manager, prealloc_view) \
    macro(manager, prealloc_withdraw) \
    macro(manager, farm_view) \
    macro(manager, farm_get_yield) \
    macro(manager, farm_totals) \
    macro(manager, farm_update)

#define DaoCoreRoles_All(macro) \
    macro(manager)

BEAM_EXPORT void Method_0()
{
    // scheme
    Env::DocGroup root("");

    {   Env::DocGroup gr("roles");

#define THE_FIELD(type, name) Env::DocAddText(#name, #type);
#define THE_METHOD(role, name) { Env::DocGroup grMethod(#name);  DaoCore_##role##_##name(THE_FIELD) }
#define THE_ROLE(name) { Env::DocGroup grRole(#name); DaoCoreRole_##name(THE_METHOD) }
        
        DaoCoreRoles_All(THE_ROLE)
#undef THE_ROLE
#undef THE_METHOD
#undef THE_FIELD
    }
}

#define THE_FIELD(type, name) const type& name,
#define ON_METHOD(role, name) void On_##role##_##name(DaoCore_##role##_##name(THE_FIELD) int unused = 0)

void OnError(const char* sz)
{
    Env::DocAddText("error", sz);
}

const char g_szAdminSeed[] = "upgr2-dao-core";

struct MyKeyID :public Env::KeyID {
    MyKeyID() :Env::KeyID(&g_szAdminSeed, sizeof(g_szAdminSeed)) {}
};

ON_METHOD(manager, view)
{
    static const ShaderID s_pSid[] = {
        DaoCore::s_SID,
    };

    ContractID pVerCid[_countof(s_pSid)];
    Height pVerDeploy[_countof(s_pSid)];

    ManagerUpgadable2::Walker wlk;
    wlk.m_VerInfo.m_Count = _countof(s_pSid);
    wlk.m_VerInfo.s_pSid = s_pSid;
    wlk.m_VerInfo.m_pCid = pVerCid;
    wlk.m_VerInfo.m_pHeight = pVerDeploy;

    MyKeyID kid;
    wlk.ViewAll(&kid);
}

ON_METHOD(manager, explicit_upgrade)
{
    ManagerUpgadable2::MultiSigRitual::Perform_ExplicitUpgrade(cid);
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

    DaoCore::State s;
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
    Env::DocAddNum("locked_beamX", get_ContractLocked(aid, cid));
    Env::DocAddNum("locked_beams", get_ContractLocked(0, cid));
}

static const char g_szXid[] = "xid-seed";

ON_METHOD(manager, my_xid)
{
    PubKey pk;
    Env::DerivePk(pk, g_szXid, sizeof(g_szXid));
    Env::DocAddBlob_T("xid", pk);
}

ON_METHOD(manager, my_admin_key)
{
    PubKey pk;
    MyKeyID kid;
    kid.get_Pk(pk);
    Env::DocAddBlob_T("admin_key", pk);
}

template <typename TX, typename TY>
TY CalculateFraction(TY val, TX x, TX xTotal)
{
    MultiPrecision::UInt<sizeof(TY) / sizeof(MultiPrecision::Word)> res;
    res.SetDiv(MultiPrecision::From(x) * MultiPrecision::From(val), MultiPrecision::From(xTotal));

    return res.template Get<0, TY>();
}

Amount CalculatePreallocAvail(const DaoCore::Preallocated::User& pu)
{
    Height dh = Env::get_Height();
    if (dh < pu.m_Vesting_h0)
        return 0;

    dh -= pu.m_Vesting_h0;

    if (dh >= pu.m_Vesting_dh)
        return pu.m_Total;

    return CalculateFraction(pu.m_Total, dh, pu.m_Vesting_dh);
}

ON_METHOD(manager, prealloc_totals)
{
    const Amount valAssigned =
        DaoCore::Preallocated::s_Emission -
        DaoCore::Preallocated::s_Unassigned;

    Amount valReceived = valAssigned;
    Amount valAvail = valAssigned;

    Env::Key_T<DaoCore::Preallocated::User::Key> k0, k1;
    _POD_(k0.m_Prefix.m_Cid) = cid;
    _POD_(k0.m_KeyInContract.m_Pk).SetZero();
    _POD_(k1.m_Prefix.m_Cid) = cid;
    _POD_(k1.m_KeyInContract.m_Pk).SetObject(0xff);

    for (Env::VarReader r(k0, k1); ; )
    {
        DaoCore::Preallocated::User pu;
        if (!r.MoveNext_T(k0, pu))
            break;

        valReceived += pu.m_Received - pu.m_Total;
        valAvail += CalculatePreallocAvail(pu) - pu.m_Total;
    }

    Env::DocAddNum("total", DaoCore::Preallocated::s_Emission);
    Env::DocAddNum("avail", valAvail);
    Env::DocAddNum("received", valReceived);
}

ON_METHOD(manager, prealloc_view)
{
    Env::Key_T<DaoCore::Preallocated::User::Key> puk;
    _POD_(puk.m_Prefix.m_Cid) = cid;
    Env::DerivePk(puk.m_KeyInContract.m_Pk, g_szXid, sizeof(g_szXid));

    DaoCore::Preallocated::User pu;
    if (Env::VarReader::Read_T(puk, pu))
    {
        Env::DocAddNum("total", pu.m_Total);
        Env::DocAddNum("received", pu.m_Received);

        Env::DocAddNum("vesting_start", pu.m_Vesting_h0);
        Env::DocAddNum("vesting_end", pu.m_Vesting_h0 + pu.m_Vesting_dh);

        // calculate the maximum available
        Amount nMaxAvail = CalculatePreallocAvail(pu);

        Env::DocAddNum("avail_total", nMaxAvail);
        Env::DocAddNum("avail_remaining", (nMaxAvail> pu.m_Received) ? (nMaxAvail - pu.m_Received) : 0);

        Env::DocAddNum("h", Env::get_Height());
    }
}

ON_METHOD(manager, prealloc_withdraw)
{
    auto aid = get_TrgAid(cid);
    if (!aid)
        return;

    DaoCore::GetPreallocated arg;
    Env::DerivePk(arg.m_Pk, g_szXid, sizeof(g_szXid));
    arg.m_Amount = amount;
    
    SigRequest sig;
    sig.m_pID = g_szXid;
    sig.m_nID = sizeof(g_szXid);

    FundsChange fc;
    fc.m_Aid = aid;
    fc.m_Amount = amount;
    fc.m_Consume = 0;

    uint32_t nCharge =
        ManagerUpgadable2::get_ChargeInvoke() +
        Env::Cost::LoadVar_For(sizeof(DaoCore::Preallocated::User)) +
        Env::Cost::SaveVar_For(sizeof(DaoCore::Preallocated::User)) +
        Env::Cost::LoadVar_For(sizeof(DaoCore::State)) +
        Env::Cost::FundsLock +
        Env::Cost::AddSig +
        (Env::Cost::Cycle * 1000);

    Env::GenerateKernel(&cid, arg.s_iMethod, &arg, sizeof(arg), &fc, 1, &sig, 1, "Get preallocated beamX tokens", nCharge);
}

void GetFarmingState(const ContractID& cid, DaoCore::Farming::State& fs)
{
    Height h = Env::get_Height();

    Env::Key_T<uint8_t> fsk;
    _POD_(fsk.m_Prefix.m_Cid) = cid;
    fsk.m_KeyInContract = DaoCore::Farming::s_Key;

    if (!Env::VarReader::Read_T(fsk, fs))
        _POD_(fs).SetZero();
    else
        fs.Update(h);

    fs.m_hLast = h;
}

void GetFarmingState(const ContractID& cid, DaoCore::Farming::State& fs, DaoCore::Farming::UserPos& fup)
{
    GetFarmingState(cid, fs);

    Env::Key_T<DaoCore::Farming::UserPos::Key> fupk;
    _POD_(fupk.m_Prefix.m_Cid) = cid;
    Env::DerivePk(fupk.m_KeyInContract.m_Pk, &cid, sizeof(cid));

    if (!Env::VarReader::Read_T(fupk, fup))
    {
        _POD_(fup).SetZero();
        fup.m_SigmaLast = fs.m_Sigma;
    }
}

ON_METHOD(manager, farm_view)
{
    DaoCore::Farming::State fs;
    DaoCore::Farming::UserPos fup;
    GetFarmingState(cid, fs, fup);

    {
        Env::DocGroup gr("farming");
        Env::DocAddNum("duation", fs.m_hTotal);
        Env::DocAddNum("emission", fs.get_EmissionSoFar());
        Env::DocAddNum("h", Env::get_Height());
        Env::DocAddNum("h0", DaoCore::Preallocated::s_hLaunch);
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

ON_METHOD(manager, farm_get_yield)
{
    DaoCore::Farming::State fs;
    DaoCore::Farming::UserPos fup;
    GetFarmingState(cid, fs, fup);

    fs.RemoveFraction(fup);

    DaoCore::Farming::Weight::Type w = DaoCore::Farming::Weight::Calculate(amount);
    fs.m_WeightTotal += w;

    _POD_(fup.m_SigmaLast) = fs.m_Sigma;
    fup.m_Beam = amount;

    fs.Update(fs.m_hLast + hPeriod);

    Amount res = fs.RemoveFraction(fup);

    Env::DocAddNum("yield", res);
}

ON_METHOD(manager, farm_totals)
{
    DaoCore::Farming::State fs;
    GetFarmingState(cid, fs);
    Amount beamLocked = 0, beamXinternal = 0;

    Env::Key_T<DaoCore::Farming::UserPos::Key> k0, k1;
    _POD_(k0.m_Prefix.m_Cid) = cid;
    _POD_(k0.m_KeyInContract.m_Pk).SetZero();
    _POD_(k1.m_Prefix.m_Cid) = cid;
    _POD_(k1.m_KeyInContract.m_Pk).SetObject(0xff);

    uint32_t nTotalFarming = 0;

    for (Env::VarReader r(k0, k1); ; )
    {
        DaoCore::Farming::UserPos fup;
        if (!r.MoveNext_T(k0, fup))
            break;

        beamXinternal += fup.m_BeamX; // already assigned to the user, but not claimed yet
        beamLocked += fup.m_Beam;

        if (fup.m_Beam)
            nTotalFarming++;
    }



    Env::DocAddNum("duation", fs.m_hTotal);
    Env::DocAddNum("total", fs.s_Emission);
    Env::DocAddNum("total_users", nTotalFarming);
    Env::DocAddNum("avail", fs.get_EmissionSoFar());
    Env::DocAddNum("received", fs.m_TotalDistributed - beamXinternal);
    Env::DocAddNum("beam_locked", beamLocked);
}

ON_METHOD(manager, farm_update)
{
    auto aid = get_TrgAid(cid);
    if (!aid)
        return;

    DaoCore::UpdPosFarming arg;
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

    uint32_t nCharge =
        ManagerUpgadable2::get_ChargeInvoke() +
        Env::Cost::LoadVar_For(sizeof(DaoCore::Farming::UserPos)) +
        Env::Cost::SaveVar_For(sizeof(DaoCore::Farming::UserPos)) +
        Env::Cost::LoadVar_For(sizeof(DaoCore::Farming::State)) +
        Env::Cost::SaveVar_For(sizeof(DaoCore::Farming::State)) +
        Env::Cost::LoadVar_For(sizeof(DaoCore::State)) +
        Env::Cost::FundsLock * 2 +
        Env::Cost::AddSig +
        (Env::Cost::Cycle * 2000);

    Env::GenerateKernel(&cid, arg.s_iMethod, &arg, sizeof(arg), pFc, _countof(pFc), &sig, 1, "Lock/Unlock and get farmed beamX tokens", nCharge);

}

#undef ON_METHOD
#undef THE_FIELD

BEAM_EXPORT void Method_1() 
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
            DaoCore_##role##_##name(PAR_READ) \
            On_##role##_##name(DaoCore_##role##_##name(PAR_PASS) 0); \
            return; \
        }

#define THE_ROLE(name) \
    if (!Env::Strcmp(szRole, #name)) { \
        DaoCoreRole_##name(THE_METHOD) \
        return OnError("invalid Action"); \
    }

    DaoCoreRoles_All(THE_ROLE)

#undef THE_ROLE
#undef THE_METHOD
#undef PAR_PASS
#undef PAR_READ

    OnError("unknown Role");
}

