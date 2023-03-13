#include "../common.h"
#include "../app_common_impl.h"
#include "../upgradable3/app_common_impl.h"
#include "contract.h"

#define DaoCore_manager_schedule_upgrade(macro) Upgradable3_schedule_upgrade(macro)
#define DaoCore_manager_replace_admin(macro) Upgradable3_replace_admin(macro)
#define DaoCore_manager_set_min_approvers(macro) Upgradable3_set_min_approvers(macro)
#define DaoCore_manager_explicit_upgrade(macro) macro(ContractID, cid)
#define DaoCore_manager_withdraw_unassigned(macro) \
    macro(ContractID, cid) \
    macro(Amount, amountBeamX)

#define DaoCore_manager_view(macro)
#define DaoCore_manager_view_params(macro) macro(ContractID, cid)

#define DaoCore_manager_farm_view(macro) \
    macro(ContractID, cid)

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


#define DaoCoreRole_manager(macro) \
    macro(manager, schedule_upgrade) \
    macro(manager, replace_admin) \
    macro(manager, set_min_approvers) \
    macro(manager, my_admin_key) \
    macro(manager, view) \
    macro(manager, explicit_upgrade) \
    macro(manager, withdraw_unassigned) \
    macro(manager, view_params) \
    macro(manager, my_xid) \
    macro(manager, my_admin_key) \
    macro(manager, prealloc_totals) \
    macro(manager, prealloc_view) \
    macro(manager, prealloc_withdraw) \
    macro(manager, farm_view) \
    macro(manager, farm_totals) \
    macro(manager, farm_update)

#define DaoCoreRoles_All(macro) \
    macro(manager)

namespace DaoCore2 {

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

const char g_szAdminSeed[] = "upgr2-dao-core"; // leave this name, it should be compatible with already-deployed dao-core

struct AdminKeyID :public Env::KeyID {
    AdminKeyID() :Env::KeyID(&g_szAdminSeed, sizeof(g_szAdminSeed)) {}
};

const Upgradable3::Manager::VerInfo g_VerInfo = { s_pSID, _countof(s_pSID) };

AssetID get_TrgAid(const ContractID& cid)
{
    Env::Key_T<uint8_t> key;
    _POD_(key.m_Prefix.m_Cid) = cid;
    key.m_KeyInContract = Tags::s_State;

    State s;
    if (!Env::VarReader::Read_T(key, s))
    {
        OnError("no such contract");
        return 0;
    }

    return s.m_Aid;
}

ON_METHOD(manager, view)
{
    AdminKeyID kid;
    g_VerInfo.DumpAll(&kid);
}

ON_METHOD(manager, explicit_upgrade)
{
    Upgradable3::Manager::MultiSigRitual::Perform_ExplicitUpgrade(cid);
}

ON_METHOD(manager, withdraw_unassigned)
{
    if (!amountBeamX)
        return OnError("amount not specified");

    auto aid = get_TrgAid(cid);
    if (!aid)
        return;

    AdminKeyID kid;

    Method::AdminWithdraw arg;
    arg.m_BeamX = amountBeamX;

    Upgradable3::Manager::MultiSigRitual msp;
    msp.m_szComment = "Dao-Core withdraw unassigned BeamX";
    msp.m_iMethod = arg.s_iMethod;
    msp.m_nArg = sizeof(arg);
    msp.m_pCid = &cid;
    msp.m_Kid = kid;

    FundsChange fc;
    fc.m_Consume = 0;
    fc.m_Amount = amountBeamX;
    fc.m_Aid = aid;

    msp.m_pFc = &fc;
    msp.m_nFc = 1;

    msp.m_Charge +=
        Env::Cost::LoadVar_For(sizeof(Amount)) +
        Env::Cost::SaveVar_For(sizeof(Amount)) +
        Env::Cost::FundsLock;

    msp.Perform(arg);
}

ON_METHOD(manager, schedule_upgrade)
{
    AdminKeyID kid;
    g_VerInfo.ScheduleUpgrade(cid, kid, hTarget);
}

ON_METHOD(manager, replace_admin)
{
    AdminKeyID kid;
    Upgradable3::Manager::MultiSigRitual::Perform_ReplaceAdmin(cid, kid, iAdmin, pk);
}

ON_METHOD(manager, set_min_approvers)
{
    AdminKeyID kid;
    Upgradable3::Manager::MultiSigRitual::Perform_SetApprovers(cid, kid, newVal);
}

ON_METHOD(manager, view_params)
{
    auto aid = get_TrgAid(cid);
    if (!aid)
        return;

    Env::DocGroup gr("params");
    Env::DocAddNum("aid", aid);
    Env::DocAddNum("locked_beamX", WalkerFunds::FromContract_Lo(cid, aid));
    Env::DocAddNum("locked_beams", WalkerFunds::FromContract_Lo(cid, 0));

    Env::Key_T<uint8_t> key;
    _POD_(key.m_Prefix.m_Cid) = cid;
    key.m_KeyInContract = Tags::s_WithdrawReserve;

    Amount val = Preallocated::s_Unassigned;
    Env::VarReader::Read_T(key, val);

    Env::DocAddNum("unassigned_beamX", val);
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
    AdminKeyID kid;
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

Amount CalculatePreallocAvail(const Preallocated::User& pu)
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
        Preallocated::s_Emission -
        Preallocated::s_Unassigned;

    Amount valReceived = valAssigned;
    Amount valAvail = valAssigned;

    Env::Key_T<Preallocated::User::Key> k0, k1;
    _POD_(k0.m_Prefix.m_Cid) = cid;
    _POD_(k0.m_KeyInContract.m_Pk).SetZero();
    _POD_(k1.m_Prefix.m_Cid) = cid;
    _POD_(k1.m_KeyInContract.m_Pk).SetObject(0xff);

    for (Env::VarReader r(k0, k1); ; )
    {
        Preallocated::User pu;
        if (!r.MoveNext_T(k0, pu))
            break;

        valReceived += pu.m_Received - pu.m_Total;
        valAvail += CalculatePreallocAvail(pu) - pu.m_Total;
    }

    Env::DocAddNum("total", Preallocated::s_Emission);
    Env::DocAddNum("avail", valAvail);
    Env::DocAddNum("received", valReceived);
}

ON_METHOD(manager, prealloc_view)
{
    Env::Key_T<Preallocated::User::Key> puk;
    _POD_(puk.m_Prefix.m_Cid) = cid;
    Env::DerivePk(puk.m_KeyInContract.m_Pk, g_szXid, sizeof(g_szXid));

    Preallocated::User pu;
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

    Method::GetPreallocated arg;
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
        Env::Cost::CallFar +
        Env::Cost::LoadVar_For(sizeof(Preallocated::User)) +
        Env::Cost::SaveVar_For(sizeof(Preallocated::User)) +
        Env::Cost::LoadVar_For(sizeof(State)) +
        Env::Cost::FundsLock +
        Env::Cost::AddSig +
        (Env::Cost::Cycle * 1000);

    Env::GenerateKernel(&cid, arg.s_iMethod, &arg, sizeof(arg), &fc, 1, &sig, 1, "Get preallocated beamX tokens", nCharge);
}

void GetFarmingState(const ContractID& cid, Farming::State& fs)
{
    Height h = Env::get_Height();

    Env::Key_T<uint8_t> fsk;
    _POD_(fsk.m_Prefix.m_Cid) = cid;
    fsk.m_KeyInContract = Tags::s_Farm;

    if (!Env::VarReader::Read_T(fsk, fs))
        _POD_(fs).SetZero();

    fs.m_hLast = h;
}

void GetFarmingState(const ContractID& cid, Farming::State& fs, Farming::UserPos& fup)
{
    GetFarmingState(cid, fs);

    Env::Key_T<Farming::UserPos::Key> fupk;
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
    Farming::State fs;
    Farming::UserPos fup;
    GetFarmingState(cid, fs, fup);

    {
        Env::DocGroup gr("farming");
        Env::DocAddNum("duation", fs.m_hTotal);
        Env::DocAddNum("emission", fs.get_EmissionSoFar());
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

ON_METHOD(manager, farm_totals)
{
    Farming::State fs;
    GetFarmingState(cid, fs);
    Amount beamLocked = 0, beamXinternal = 0;

    Env::Key_T<Farming::UserPos::Key> k0, k1;
    _POD_(k0.m_Prefix.m_Cid) = cid;
    _POD_(k0.m_KeyInContract.m_Pk).SetZero();
    _POD_(k1.m_Prefix.m_Cid) = cid;
    _POD_(k1.m_KeyInContract.m_Pk).SetObject(0xff);

    uint32_t nTotalFarming = 0;

    for (Env::VarReader r(k0, k1); ; )
    {
        Farming::UserPos fup;
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

    Method::UpdPosFarming arg;
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
        Env::Cost::CallFar +
        Env::Cost::LoadVar_For(sizeof(Farming::UserPos)) +
        Env::Cost::SaveVar_For(sizeof(Farming::UserPos)) +
        Env::Cost::LoadVar_For(sizeof(Farming::State)) +
        Env::Cost::SaveVar_For(sizeof(Farming::State)) +
        Env::Cost::LoadVar_For(sizeof(State)) +
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

} // namespace DaoCore2
