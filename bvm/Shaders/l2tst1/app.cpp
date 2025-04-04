#include "../common.h"
#include "../app_common_impl.h"
#include "../upgradable3/app_common_impl.h"
#include "contract.h"

#define L2Tst1_schedule_upgrade(macro) Upgradable3_schedule_upgrade(macro)
#define L2Tst1_replace_admin(macro) Upgradable3_replace_admin(macro)
#define L2Tst1_set_min_approvers(macro) Upgradable3_set_min_approvers(macro)
#define L2Tst1_explicit_upgrade(macro) macro(ContractID, cid)

#define L2Tst1_deploy(macro) \
    Upgradable3_deploy(macro) \
    macro(Height, hPrePhaseEnd) \
    macro(AssetID, aidStaking) \
    macro(AssetID, aidLiquidity)

#define L2Tst1_view_deployed(macro)
#define L2Tst1_view_params(macro) macro(ContractID, cid)
#define L2Tst1_user_view(macro) macro(ContractID, cid)
#define L2Tst1_users_view_all(macro) macro(ContractID, cid)

#define L2Tst1_user_stake(macro) \
    macro(ContractID, cid) \
    macro(Amount, amount)

#define L2Tst1Actions_All(macro) \
    macro(view_deployed) \
    macro(view_params) \
    macro(deploy) \
	macro(schedule_upgrade) \
	macro(replace_admin) \
	macro(set_min_approvers) \
	macro(explicit_upgrade) \
	macro(user_view) \
	macro(users_view_all) \
	macro(user_stake) \


namespace L2Tst1 {

BEAM_EXPORT void Method_0()
{
    // scheme
    Env::DocGroup root("");

    {   Env::DocGroup gr("actions");

#define THE_FIELD(type, name) Env::DocAddText(#name, #type);
#define THE_ACTION(name) { Env::DocGroup gr(#name);  L2Tst1_##name(THE_FIELD) }
        
        L2Tst1Actions_All(THE_ACTION)
#undef THE_ACTION
#undef THE_FIELD
    }
}

#define THE_FIELD(type, name) const type& name,
#define ON_METHOD(name) void On_##name(L2Tst1_##name(THE_FIELD) int unused = 0)

void OnError(const char* sz)
{
    Env::DocAddText("error", sz);
}

const char g_szAdminSeed[] = "upgr3-l2tst1";

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

    if (hPrePhaseEnd <= Env::get_Height())
        return OnError("pre-phase too short");
    arg.m_Settings.m_hPreEnd = hPrePhaseEnd;

    if (!aidStaking || !aidLiquidity)
        return OnError("aids not provided");

    arg.m_Settings.m_aidStaking = aidStaking;
    arg.m_Settings.m_aidLiquidity = aidLiquidity;

    const uint32_t nCharge =
        Upgradable3::Manager::get_ChargeDeploy() +
        Env::Cost::SaveVar_For(sizeof(State)) +
        Env::Cost::Cycle * 50;

    Env::GenerateKernel(nullptr, arg.s_iMethod, &arg, sizeof(arg), nullptr, 0, nullptr, 0, "Deploy L2Tst1 contract", nCharge);
}

ON_METHOD(schedule_upgrade)
{
    AdminKeyID kid;
    g_VerInfo.ScheduleUpgrade(cid, kid, hTarget);
}

ON_METHOD(explicit_upgrade)
{
    const uint32_t nCharge =
        Env::Cost::LoadVar_For(sizeof(State)) +
        Env::Cost::SaveVar_For(sizeof(State)) +
        Env::Cost::Cycle * 300;

    Upgradable3::Manager::MultiSigRitual::Perform_ExplicitUpgrade(cid, nCharge);
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

struct MyState
    :public State
{
    bool Read(const ContractID& cid)
    {
        Env::Key_T<uint8_t> key;
        _POD_(key.m_Prefix.m_Cid) = cid;
        key.m_KeyInContract = Tags::s_State;

        if (Env::VarReader::Read_T(key, *this))
            return true;

        OnError("State not found");
        return false;
    }
};

ON_METHOD(view_params)
{
    MyState s;
    if (!s.Read(cid))
        return;

    Env::DocGroup gr("res");
    Env::DocAddNum("aid-staking", s.m_Settings.m_aidStaking);
    Env::DocAddNum("aid-liquidity", s.m_Settings.m_aidLiquidity);
    Env::DocAddNum("hPreEnd", s.m_Settings.m_hPreEnd);

    Amount totalStake = WalkerFunds::FromContract_Lo(cid, s.m_Settings.m_aidStaking);
    Env::DocAddNum("total_stake", totalStake);
}


struct MyUser
    :public User
{
    void Print(const Key& uk)
    {
        Env::DocAddBlob_T("key", uk.m_pk);
        Env::DocAddNum("stake", m_Stake);
    }

    struct KeyID :public Env::KeyID {
        KeyID(const ContractID& cid) :Env::KeyID(cid) {}
    };
};

ON_METHOD(user_view)
{
    Env::Key_T<User::Key> k;
    _POD_(k.m_Prefix.m_Cid) = cid;
    MyUser::KeyID(cid).get_Pk(k.m_KeyInContract.m_pk);

    MyUser u;
    if (Env::VarReader::Read_T(k, u))
    {
        Env::DocGroup gr("res");
        u.Print(k.m_KeyInContract);
    }
}

ON_METHOD(users_view_all)
{
    Env::DocArray gr0("res");

    Env::Key_T<User::Key> k0, k1;
    _POD_(k0.m_Prefix.m_Cid) = cid;
    _POD_(k1.m_Prefix.m_Cid) = cid;

    _POD_(k0.m_KeyInContract.m_pk).SetZero();
    _POD_(k1.m_KeyInContract.m_pk).SetObject(0xff);

    for (Env::VarReader r(k0, k1); ; )
    {
        MyUser u;
        if (!r.MoveNext_T(k0, u))
            break;

        Env::DocGroup gr1("");
        u.Print(k0.m_KeyInContract);
    }
}

ON_METHOD(user_stake)
{
    if (!amount)
        return OnError("amount not specified");

    MyState s;
    if (!s.Read(cid))
        return;

    if (Env::get_Height() >= s.m_Settings.m_hPreEnd)
        return OnError("pre-phase is over");

    FundsChange fc;
    fc.m_Amount = amount;
    fc.m_Aid = s.m_Settings.m_aidStaking;
    fc.m_Consume = 1;

    uint32_t nCharge =
        Env::Cost::CallFar +
        Env::Cost::LoadVar_For(sizeof(State)) +
        Env::Cost::LoadVar_For(sizeof(User)) +
        Env::Cost::SaveVar_For(sizeof(User)) +
        Env::Cost::FundsLock +
        Env::Cost::Cycle * 200;


    Method::UserStake arg;
    arg.m_Amount = amount;
    MyUser::KeyID(cid).get_Pk(arg.m_pkUser);

    Env::GenerateKernel(&cid, arg.s_iMethod, &arg, sizeof(arg), &fc, 1, nullptr, 0, "L2Tst1 user stake", nCharge);
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
            L2Tst1_##name(PAR_READ) \
            On_##name(L2Tst1_##name(PAR_PASS) 0); \
            return; \
        }

    L2Tst1Actions_All(THE_METHOD)

#undef THE_METHOD
#undef PAR_PASS
#undef PAR_READ

    OnError("unknown action");
}

} // namespace L2Tst1
