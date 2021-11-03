#include "../common.h"
#include "../app_common_impl.h"
#include "contract.h"
#include "../upgradable2/contract.h"
#include "../upgradable2/app_common_impl.h"

#define Gallery_manager_deploy_version(macro)
#define Gallery_manager_view(macro)
#define Gallery_manager_my_admin_key(macro)
#define Gallery_manager_schedule_upgrade(macro) Upgradable2_schedule_upgrade(macro)
#define Gallery_manager_explicit_upgrade(macro) macro(ContractID, cid)
#define Gallery_manager_replace_admin(macro) Upgradable2_replace_admin(macro)
#define Gallery_manager_set_min_approvers(macro) Upgradable2_set_min_approvers(macro)

#define Gallery_manager_deploy_contract(macro) \
    Upgradable2_deploy(macro) \
    macro(Amount, voteRewardAmount) \
    macro(AssetID, voteRewardAid)

#define GalleryRole_manager(macro) \
    macro(manager, deploy_version) \
    macro(manager, view) \
    macro(manager, deploy_contract) \
    macro(manager, schedule_upgrade) \
    macro(manager, explicit_upgrade) \
    macro(manager, replace_admin) \
    macro(manager, set_min_approvers) \
    macro(manager, my_admin_key) \

#define GalleryRoles_All(macro) \
    macro(manager)


BEAM_EXPORT void Method_0()
{
    // scheme
    Env::DocGroup root("");

    {   Env::DocGroup gr("roles");

#define THE_FIELD(type, name) Env::DocAddText(#name, #type);
#define THE_METHOD(role, name) { Env::DocGroup grMethod(#name);  Gallery_##role##_##name(THE_FIELD) }
#define THE_ROLE(name) { Env::DocGroup grRole(#name); GalleryRole_##name(THE_METHOD) }
        
        GalleryRoles_All(THE_ROLE)
#undef THE_ROLE
#undef THE_METHOD
#undef THE_FIELD
    }
}

#define THE_FIELD(type, name) const type& name,
#define ON_METHOD(role, name) void On_##role##_##name(Gallery_##role##_##name(THE_FIELD) int unused = 0)

void OnError(const char* sz)
{
    Env::DocAddText("error", sz);
}

namespace KeyMaterial
{
    const char g_szAdmin[] = "Gallery-key-admin";

    struct MyAdminKey :public Env::KeyID {
        MyAdminKey() :Env::KeyID(g_szAdmin, sizeof(g_szAdmin) - sizeof(char)) {}
    };

#pragma pack (push, 1)

    const char g_szOwner[] = "Gallery-key-owner";

    struct Owner
    {
        ContractID m_Cid;
        Gallery::Masterpiece::ID m_ID;
        uint8_t m_pSeed[sizeof(g_szOwner) - sizeof(char)];

        void Set(const ContractID& cid)
        {
            _POD_(m_Cid) = cid;
            m_ID = 0;
            Env::Memcpy(m_pSeed, g_szOwner, sizeof(m_pSeed));
        }

        void Get(PubKey& pk) const {
            Env::DerivePk(pk, this, sizeof(*this));
        }
    };
#pragma pack (pop)

    const Gallery::Masterpiece::ID g_MskImpression = Utils::FromBE((static_cast<Gallery::Masterpiece::ID>(-1) >> 1) + 1); // hi bit
}

ON_METHOD(manager, view)
{
    static const ShaderID s_pSid[] = {
        Gallery::s_SID,
    };

    ContractID pVerCid[_countof(s_pSid)];
    Height pVerDeploy[_countof(s_pSid)];

    ManagerUpgadable2::Walker wlk;
    wlk.m_VerInfo.m_Count = _countof(s_pSid);
    wlk.m_VerInfo.s_pSid = s_pSid;
    wlk.m_VerInfo.m_pCid = pVerCid;
    wlk.m_VerInfo.m_pHeight = pVerDeploy;

    KeyMaterial::MyAdminKey kid;
    wlk.ViewAll(&kid);
}

ON_METHOD(manager, deploy_version)
{
    Env::GenerateKernel(nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0, "Deploy Gallery bytecode", 0);
}


ON_METHOD(manager, deploy_contract)
{
#pragma pack (push, 1)
    struct Args
        :public Upgradable2::Create
        ,public Gallery::Method::Init
    {
    };
#pragma pack (pop)

    Args args;
    _POD_(args).SetZero();

    args.m_Config.m_VoteReward.m_Amount = voteRewardAmount;
    args.m_Config.m_VoteReward.m_Aid = voteRewardAid;
    KeyMaterial::MyAdminKey().get_Pk(args.m_Config.m_pkAdmin);

    if (!ManagerUpgadable2::FillDeployArgs(args, &args.m_Config.m_pkAdmin))
        return;

    Env::GenerateKernel(nullptr, 0, &args, sizeof(args), nullptr, 0, nullptr, 0, "Deploy Gallery contract", 0);
}

ON_METHOD(manager, schedule_upgrade)
{
    KeyMaterial::MyAdminKey kid;
    ManagerUpgadable2::MultiSigRitual::Perform_ScheduleUpgrade(cid, kid, cidVersion, hTarget);
}

ON_METHOD(manager, explicit_upgrade)
{
    ManagerUpgadable2::MultiSigRitual::Perform_ExplicitUpgrade(cid);
}

ON_METHOD(manager, replace_admin)
{
    KeyMaterial::MyAdminKey kid;
    ManagerUpgadable2::MultiSigRitual::Perform_ReplaceAdmin(cid, kid, iAdmin, pk);
}

ON_METHOD(manager, set_min_approvers)
{
    KeyMaterial::MyAdminKey kid;
    ManagerUpgadable2::MultiSigRitual::Perform_SetApprovers(cid, kid, newVal);
}

ON_METHOD(manager, my_admin_key)
{
    PubKey pk;
    KeyMaterial::MyAdminKey().get_Pk(pk);
    Env::DocAddBlob_T("admin_key", pk);
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

#define PAR_READ(type, name) type arg_##name; Env::DocGet(#name, arg_##name);
#define PAR_PASS(type, name) arg_##name,

#define THE_METHOD(role, name) \
        if (!Env::Strcmp(szAction, #name)) { \
            Gallery_##role##_##name(PAR_READ) \
            On_##role##_##name(Gallery_##role##_##name(PAR_PASS) 0); \
            return; \
        }

#define THE_ROLE(name) \
    if (!Env::Strcmp(szRole, #name)) { \
        GalleryRole_##name(THE_METHOD) \
        return OnError("invalid Action"); \
    }

    GalleryRoles_All(THE_ROLE)

#undef THE_ROLE
#undef THE_METHOD
#undef PAR_PASS
#undef PAR_READ

    OnError("unknown Role");
}

