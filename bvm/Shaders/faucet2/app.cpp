#include "../common.h"
#include "../app_common_impl.h"
#include "contract.h"

#define Faucet2_funds_move(macro) \
    macro(ContractID, cid) \
    macro(Amount, amount) \
    macro(AssetID, aid)

#define Faucet2_admin_create(macro) \
    macro(Height, backlogPeriod) \
    macro(Amount, withdrawLimit)

#define Faucet2_admin_destroy(macro) macro(ContractID, cid)

#define Faucet2_admin_view(macro)

#define Faucet2_admin_control(macro) \
    macro(ContractID, cid) \
    macro(uint32_t, bEnable)


#define Faucet2_admin_withdraw(macro) Faucet2_funds_move(macro)

#define Faucet2Role_admin(macro) \
    macro(admin, create) \
    macro(admin, destroy) \
    macro(admin, view) \
    macro(admin, control) \
    macro(admin, withdraw)


#define Faucet2_user_view(macro)
#define Faucet2_user_view_all_assets(macro)
#define Faucet2_user_view_params(macro) macro(ContractID, cid)
#define Faucet2_user_view_funds(macro) macro(ContractID, cid)
#define Faucet2_user_deposit(macro) Faucet2_funds_move(macro)
#define Faucet2_user_withdraw(macro) Faucet2_funds_move(macro)

#define Faucet2Role_user(macro) \
    macro(user, view) \
    macro(user, view_all_assets) \
    macro(user, view_params) \
    macro(user, view_funds) \
    macro(user, deposit) \
    macro(user, withdraw)


#define Faucet2Roles_All(macro) \
    macro(admin) \
    macro(user)

BEAM_EXPORT void Method_0()
{
    // scheme
    Env::DocGroup root("");

    {   Env::DocGroup gr("roles");

#define THE_FIELD(type, name) Env::DocAddText(#name, #type);
#define THE_METHOD(role, name) { Env::DocGroup grMethod(#name);  Faucet2_##role##_##name(THE_FIELD) }
#define THE_ROLE(name) { Env::DocGroup grRole(#name); Faucet2Role_##name(THE_METHOD) }
        
        Faucet2Roles_All(THE_ROLE)
#undef THE_ROLE
#undef THE_METHOD
#undef THE_FIELD
    }
}

#define THE_FIELD(type, name) const type& name,
#define ON_METHOD(role, name) void On_##role##_##name(Faucet2_##role##_##name(THE_FIELD) int unused = 0)

void OnError(const char* sz)
{
    Env::DocAddText("error", sz);
}

static const char g_szAdmin[] = "faucet2.admin";

struct KidAdmin :public Env::KeyID {
    KidAdmin() :Env::KeyID(g_szAdmin, sizeof(g_szAdmin)) {}
};

ON_METHOD(admin, create)
{
    if (!backlogPeriod || !withdrawLimit)
        return OnError("backlog and withdraw limit should be nnz");

    Faucet2::Method::Create arg;
    KidAdmin().get_Pk(arg.m_Params.m_pkAdmin);
    arg.m_Params.m_Limit.m_Amount = withdrawLimit;
    arg.m_Params.m_Limit.m_Height = backlogPeriod;

    Env::GenerateKernel(nullptr, arg.s_iMethod, &arg, sizeof(arg), nullptr, 0, nullptr, 0, "create Faucet2 contract", 0);
}

ON_METHOD(admin, view)
{
    EnumAndDumpContracts(Faucet2::s_SID);
}

ON_METHOD(admin, destroy)
{
    KidAdmin kid;
    Env::GenerateKernel(&cid, 1, nullptr, 0, nullptr, 0, &kid, 1, "destroy Faucet2 contract", 0);
}

ON_METHOD(admin, control)
{
    Faucet2::Method::AdminCtl arg;
    arg.m_Enable = !!bEnable;

    KidAdmin kid;
    Env::GenerateKernel(&cid, arg.s_iMethod, &arg, sizeof(arg), nullptr, 0, &kid, 1, "Faucet2 control", 0);
}

bool SetAmounts(Faucet2::AmountWithAsset& trg, FundsChange& fc, Amount amount, AssetID aid)
{
    if (!amount)
    {
        OnError("amount should be nnz");
        return false;
    }

    trg.m_Amount = amount;
    trg.m_Aid = aid;
    fc.m_Amount = amount;
    fc.m_Aid = aid;

    return true;
}

ON_METHOD(admin, withdraw)
{
    Faucet2::Method::AdminWithdraw arg;
    FundsChange fc;

    if (!SetAmounts(arg.m_Value, fc, amount, aid))
        return;

    fc.m_Consume = 0;
    KidAdmin kid;
    Env::GenerateKernel(&cid, arg.s_iMethod, &arg, sizeof(arg), &fc, 1, &kid, 1, "Faucet2 admin withdraw", 0);
}

ON_METHOD(user, view)
{
    EnumAndDumpContracts(Faucet2::s_SID);
}

ON_METHOD(user, view_all_assets)
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

ON_METHOD(user, view_params)
{
    Env::Key_T<uint8_t> k;
    k.m_Prefix.m_Cid = cid;
    k.m_KeyInContract = Faucet2::State::s_Key;

    Faucet2::State s;
    if (!Env::VarReader::Read_T(k, s))
        return OnError("no such a contract");

    {
        Env::DocGroup gr("params");
        Env::DocAddNum("backlogPeriod", s.m_Params.m_Limit.m_Height);
        Env::DocAddNum("withdrawLimit", s.m_Params.m_Limit.m_Amount);

        PubKey pk;
        KidAdmin().get_Pk(pk);

        uint32_t bEqual = _POD_(s.m_Params.m_pkAdmin) == pk;
        Env::DocAddNum("isAdmin", bEqual);

        Env::DocAddNum("enabled", (uint32_t) s.m_Enabled);
    }

    Height h = std::max(Env::get_Height(), s.m_Epoch.m_Height);
    s.UpdateEpoch(h);

    Env::DocAddNum("epoch_limit", s.m_Epoch.m_Amount);
    Env::DocAddNum("epoch_remaining", s.m_Epoch.m_Height + s.m_Params.m_Limit.m_Height - h);
}

ON_METHOD(user, view_funds)
{
    Env::DocArray gr0("funds");

    WalkerFunds wlk;
    for (wlk.Enum(cid); wlk.MoveNext(); )
    {
        Env::DocGroup gr("");

        Env::DocAddNum("Aid", wlk.m_Aid);
        Env::DocAddNum("Amount", wlk.m_Val.m_Lo);
    }
}

ON_METHOD(user, deposit)
{
    Faucet2::Method::Deposit arg;
    FundsChange fc;

    if (!SetAmounts(arg.m_Value, fc, amount, aid))
        return;

    fc.m_Consume = 1;
    Env::GenerateKernel(&cid, arg.s_iMethod, &arg, sizeof(arg), &fc, 1, nullptr, 0, "Faucet2 deposit", 0);
}

ON_METHOD(user, withdraw)
{
    Faucet2::Method::Withdraw arg;
    FundsChange fc;

    if (!SetAmounts(arg.m_Value, fc, amount, aid))
        return;

    fc.m_Consume = 0;
    Env::GenerateKernel(&cid, arg.s_iMethod, &arg, sizeof(arg), &fc, 1, nullptr, 0, "Faucet2 withdraw", 0);
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
            Faucet2_##role##_##name(PAR_READ) \
            On_##role##_##name(Faucet2_##role##_##name(PAR_PASS) 0); \
            return; \
        }

#define THE_ROLE(name) \
    if (!Env::Strcmp(szRole, #name)) { \
        Faucet2Role_##name(THE_METHOD) \
        return OnError("invalid Action"); \
    }

    Faucet2Roles_All(THE_ROLE)

#undef THE_ROLE
#undef THE_METHOD
#undef PAR_PASS
#undef PAR_READ

    OnError("unknown Role");
}

