#include "../common.h"
#include "../app_common_impl.h"
#include "contract.h"
#include "../upgradable/contract.h"
#include "../Math.h"

#define DemoXdao_manager_deploy_version(macro)
#define DemoXdao_manager_view(macro)
#define DemoXdao_manager_view_params(macro) macro(ContractID, cid)
#define DemoXdao_manager_lock(macro) \
    macro(ContractID, cid) \
    macro(Amount, amount)

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
    macro(manager, lock)

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

template <typename T>
struct Vector
{
    T* m_p = nullptr;
    uint32_t m_Count = 0;
    uint32_t m_Alloc = 0;

    ~Vector()
    {
        if (m_p)
            Env::Heap_Free(m_p);
    }

    void Prepare(uint32_t n)
    {
        if (m_Alloc >= n)
            return;

        m_Alloc = std::max(m_Alloc * 2, n);
        m_Alloc = std::max(m_Alloc, 16U);

        T* pOld = m_p;
        m_p = (T*) Env::Heap_Alloc(sizeof(T) * n);
        
        if (pOld)
        {
            Env::Memcpy(m_p, pOld, sizeof(T) * m_Count);
            Env::Heap_Free(pOld);
        }
    }

    T& emplace_back()
    {
        Prepare(m_Count + 1);
        return m_p[m_Count++];
    }
};

void get_CidFromSid(ContractID& cid, const ShaderID& sid)
{
    HashProcessor::Sha256()
        << "bvm.cid"
        << sid
        << static_cast<uint32_t>(0)
        >> cid;
}

Height FindDeployed(const ContractID& cid, const ShaderID& sid)
{
    Env::Key_T<WalkerContracts::SidCid> key;
    _POD_(key.m_Prefix.m_Cid).SetZero();
    key.m_Prefix.m_Tag = KeyTag::SidCid;

    _POD_(key.m_KeyInContract.m_Sid) = sid;
    _POD_(key.m_KeyInContract.m_Cid) = cid;

    const auto* pHeight_be = Env::VarRead_T<Height>(key);
    return pHeight_be ? Utils::FromBE(*pHeight_be) : 0;
}

ON_METHOD(manager, view)
{
    // Calculate the CID of the current version
    ContractID cid0, cid1;
    get_CidFromSid(cid0, DemoXdao::s_SID_0);
    get_CidFromSid(cid1, DemoXdao::s_SID);

    Height  h = FindDeployed(cid0, DemoXdao::s_SID_0);
    if (h)
    {
        Env::DocGroup gr0("ver0");

        Env::DocAddNum("Height", h);
        Env::DocAddBlob_T("cid", cid0);
    }

    h = FindDeployed(cid1, DemoXdao::s_SID);
    if (h)
    {
        Env::DocGroup gr0("ver1");

        Env::DocAddNum("Height", h);
        Env::DocAddBlob_T("cid", cid1);
    }

    struct Entry {
        ContractID m_Cid;
        Height m_Height;
    };

    Vector<Entry> vUpgr;
    {
        WalkerContracts wlk;
        for (wlk.Enum(Upgradable::s_SID); wlk.MoveNext(); )
        {
            auto& e = vUpgr.emplace_back();
            _POD_(e.m_Cid) = wlk.m_pPos->m_Cid;
            e.m_Height = wlk.m_Height;
        }
    }

    Env::DocArray gr("contracts");

    PubKey pk;
    Env::DerivePk(pk, &Upgradable::s_SID, sizeof(Upgradable::s_SID));

    for (uint32_t i = 0; i < vUpgr.m_Count; i++)
    {
        const auto& e = vUpgr.m_p[i];

        Env::Key_T<uint8_t> key;
        key.m_Prefix.m_Cid = e.m_Cid;
        key.m_KeyInContract = Upgradable::State::s_Key;

        auto* pS = Env::VarRead_T<Upgradable::State>(key);
        if (!pS)
            continue;

        uint32_t nVerCurrent = (_POD_(cid0) == pS->m_Cid) ? 0 : (_POD_(cid1) == pS->m_Cid) ? 1 : 1000;
        uint32_t nVerNext = (_POD_(cid0) == pS->m_cidNext) ? 0 : (_POD_(cid1) == pS->m_cidNext) ? 1 : 1000;

        if ((nVerCurrent >= 0) || (nVerNext >= 0))
        {
            Env::DocGroup root("");

            Env::DocAddBlob_T("cid", e.m_Cid);
            Env::DocAddNum("Height", e.m_Height);

            Env::DocAddNum("owner", (uint32_t) ((_POD_(pS->m_Pk) == pk) ? 1 : 0));
            Env::DocAddNum("min_upgrade_delay", pS->m_hMinUpgadeDelay);

            Env::DocAddNum("current_version", nVerCurrent);

            if (pS->m_hNextActivate != static_cast<Height>(-1))
            {
                Env::DocAddNum("next_version", nVerNext);
                Env::DocAddNum("next_height", pS->m_hNextActivate);
            }
        }
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

    const auto* pVal = Env::VarRead_T<AmountBig>(key);
    if (!pVal)
        return 0;

    return Utils::FromBE(pVal->m_Lo);
}

AssetID get_TrgAid(const ContractID& cid)
{
    Env::Key_T<uint8_t> key;
    _POD_(key.m_Prefix.m_Cid) = cid;
    key.m_KeyInContract = 0;

    const auto* pS = Env::VarRead_T<DemoXdao::State>(key);
    if (!pS)
    {
        OnError("no such contract");
        return 0;
    }

    return pS->m_Aid;
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

    const auto* pAmount = Env::VarRead_T<Amount>(key);
    Env::DocAddNum("stake", pAmount ? *pAmount : 0);
}

ON_METHOD(manager, lock)
{
    auto aid = get_TrgAid(cid);
    if (!aid)
        return;

    DemoXdao::LockAndGet arg;
    arg.m_Amount = amount;

    Env::DerivePk(arg.m_Pk, &cid, sizeof(cid));

    SigRequest sig;
    sig.m_pID = &cid;
    sig.m_nID = sizeof(cid);

    FundsChange pFc[2];
    pFc[0].m_Aid = aid;
    pFc[0].m_Amount = DemoXdao::State::s_ReleasePerLock;
    pFc[0].m_Consume = 0;
    pFc[1].m_Aid = 0;
    pFc[1].m_Amount = amount;
    pFc[1].m_Consume = 1;

    Env::GenerateKernel(&cid, arg.s_iMethod, &arg, sizeof(arg), pFc, _countof(pFc), &sig, 1, "Lock-and-get demoX tokens", 0);
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

