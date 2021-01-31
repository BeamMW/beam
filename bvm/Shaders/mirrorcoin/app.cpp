#include "../common.h"
#include "../app_common_impl.h"
#include "contract.h"
#include "../pipe/contract.h"

#define MirrorCoin_manager_create(macro) \
    macro(ContractID, cidPipe) \
    macro(AssetID, aid) \
    macro(uint32_t, bIsMirror) \

#define MirrorCoin_manager_set_remote(macro) \
    macro(ContractID, cid) \
    macro(ContractID, cidRemote)

#define MirrorCoin_manager_view(macro)
#define MirrorCoin_manager_view_params(macro) macro(ContractID, cid)
#define MirrorCoin_manager_destroy(macro) macro(ContractID, cid)

#define MirrorCoin_manager_view_incoming(macro) \
    macro(ContractID, cid) \
    macro(PubKey, pkUser) \
    macro(uint32_t, iStartFrom)

#define MirrorCoinRole_manager(macro) \
    macro(manager, create) \
    macro(manager, set_remote) \
    macro(manager, destroy) \
    macro(manager, view) \
    macro(manager, view_params) \
    macro(manager, view_incoming)

#define MirrorCoin_user_view_incoming(macro) \
    macro(ContractID, cid) \
    macro(uint32_t, iStartFrom)

#define MirrorCoin_user_send(macro) \
    macro(ContractID, cid) \
    macro(PubKey, pkDst) \
    macro(Amount, amount)

#define MirrorCoin_user_receive_all(macro) \
    macro(ContractID, cid) \
    macro(uint32_t, iStartFrom)

#define MirrorCoinRole_user(macro) \
    macro(user, view_incoming) \
    macro(user, send) \
    macro(user, receive_all)

#define MirrorCoinRoles_All(macro) \
    macro(manager) \
    macro(user)

export void Method_0()
{
    // scheme
    Env::DocGroup root("");

    {   Env::DocGroup gr("roles");

#define THE_FIELD(type, name) Env::DocAddText(#name, #type);
#define THE_METHOD(role, name) { Env::DocGroup grMethod(#name);  MirrorCoin_##role##_##name(THE_FIELD) }
#define THE_ROLE(name) { Env::DocGroup grRole(#name); MirrorCoinRole_##name(THE_METHOD) }
        
    MirrorCoinRoles_All(THE_ROLE)
#undef THE_ROLE
#undef THE_METHOD
#undef THE_FIELD
    }
}

#define THE_FIELD(type, name) const type& name,
#define ON_METHOD(role, name) void On_##role##_##name(MirrorCoin_##role##_##name(THE_FIELD) int unused = 0)

void OnError(const char* sz)
{
    Env::DocAddText("error", sz);
}

const MirrorCoin::Global* get_Global(const ContractID& cid)
{
    Env::Key_T<uint8_t> gk;
    Utils::Copy(gk.m_Prefix.m_Cid, cid);
    gk.m_KeyInContract = 0;

    auto* pG = Env::VarRead_T<MirrorCoin::Global>(gk);
    if (!pG)
        OnError("no global state");

    return pG;
}

struct IncomingWalker
{
    const ContractID& m_Cid;
    IncomingWalker(const ContractID& cid) :m_Cid(cid) {}

    ContractID m_cidRemote;

#pragma pack (push, 1)
    struct MyMsg
        :public Pipe::MsgHdr
        , public MirrorCoin::Message
    {
    };
#pragma pack (pop)

    const Env::Key_T<Pipe::MsgHdr::KeyIn>* m_pKey;
    const MyMsg* m_pMsg;


    bool Restart(uint32_t iStartFrom)
    {
        auto* pG = get_Global(m_Cid);
        if (!pG)
            return false;

        m_cidRemote = pG->m_Remote;

        Env::Key_T<Pipe::MsgHdr::KeyIn> k1;
        k1.m_Prefix.m_Cid = pG->m_PipeID;
        k1.m_KeyInContract.m_iCheckpoint_BE = Utils::FromBE(iStartFrom);
        k1.m_KeyInContract.m_iMsg_BE = 0;

        auto k2 = k1;
        k2.m_KeyInContract.m_iCheckpoint_BE = -1;

        Env::VarsEnum_T(k1, k2);
        return true;
    }

    bool MoveNext(const PubKey* pPk)
    {
        while (true)
        {
            if (!Env::VarsMoveNext_T(m_pKey, m_pMsg))
                return false;

            if (Utils::Cmp(m_pMsg->m_Sender, m_cidRemote) || Utils::Cmp(m_pMsg->m_Receiver, m_Cid))
                continue;

            if (pPk && Utils::Cmp(*pPk, m_pMsg->m_User))
                continue;

            return true;
        }
    }
};

void ViewIncoming(const ContractID& cid, const PubKey* pPk, uint32_t iStartFrom)
{
    Env::DocArray gr("incoming");

    IncomingWalker wlk(cid);
    if (!wlk.Restart(iStartFrom))
        return;

    while (wlk.MoveNext(pPk))
    {
        Env::DocGroup gr("");
        Env::DocAddNum("iCheckpoint", Utils::FromBE(wlk.m_pKey->m_KeyInContract.m_iCheckpoint_BE));
        Env::DocAddNum("iMsg", Utils::FromBE(wlk.m_pKey->m_KeyInContract.m_iMsg_BE));
        Env::DocAddNum("amount", wlk.m_pMsg->m_Amount);

        if (!pPk)
            Env::DocAddBlob_T("User", wlk.m_pMsg->m_User);
    }
}

ON_METHOD(manager, view)
{
    EnumAndDumpContracts(MirrorCoin::s_SID);
}

static const Amount g_DepositCA = 100000000000ULL; // 1K beams

ON_METHOD(manager, create)
{
    static const char szMeta[] = "metadata";

    uint32_t nMetaSize = 0;
    if (bIsMirror)
    {
        nMetaSize = Env::DocGetText(szMeta, nullptr, 0);
        if (nMetaSize < 2)
        {
            OnError("metadata should be non-empty");
            return;
        }
    }

    auto* pArg = (MirrorCoin::Create0*) Env::StackAlloc(sizeof(MirrorCoin::Create0) + nMetaSize);
    pArg->m_PipeID = cidPipe;
    pArg->m_Aid = aid;

    if (bIsMirror)
    {
        Env::DocGetText(szMeta, (char*)(pArg + 1), nMetaSize);
        nMetaSize--;
    }

    pArg->m_MetadataSize = nMetaSize;

    FundsChange fc;
    fc.m_Aid = 0; // asset id
    fc.m_Amount = g_DepositCA; // amount of the input or output
    fc.m_Consume = 1; // contract consumes funds (i.e input, in this case)

    Env::GenerateKernel(nullptr, pArg->s_iMethod, pArg, sizeof(*pArg) + nMetaSize, &fc, bIsMirror ? 1 : 0, nullptr, 0, "generate MirrorCoin contract", 2000000U);
}

ON_METHOD(manager, set_remote)
{
    MirrorCoin::SetRemote pars;
    pars.m_Cid = cidRemote;

    Env::GenerateKernel(&cid, pars.s_iMethod, &pars, sizeof(pars), nullptr, 0, nullptr, 0, "Set MirrorCoin contract remote counter-part", 1000000U);
}

ON_METHOD(manager, destroy)
{
    Env::GenerateKernel(&cid, 1, nullptr, 0, nullptr, 0, nullptr, 0, "destroy MirrorCoin contract", 2000000U);
}

ON_METHOD(manager, view_params)
{
    auto* pG = get_Global(cid);
    if (!pG)
        return;

    Env::DocGroup gr("params");

    Env::DocAddNum("aid", pG->m_Aid);
    Env::DocAddNum("isMirror", (uint32_t) pG->m_IsMirror);
    Env::DocAddBlob_T("RemoteID", pG->m_Remote);
    Env::DocAddBlob_T("PipeID", pG->m_PipeID);

    // TODO: make sure the Pipe is indeed operated by the conventional Pipe shader with adequate params
}

ON_METHOD(manager, view_incoming)
{
    ViewIncoming(cid, Utils::IsZero(pkUser) ? nullptr : &pkUser, iStartFrom);
}

void DerivePk(PubKey& pk, const ContractID& cid)
{
    Env::DerivePk(pk, &cid, sizeof(cid));
}

ON_METHOD(user, view_incoming)
{
    PubKey pk;
    DerivePk(pk, cid);
    ViewIncoming(cid, &pk, iStartFrom);
}

ON_METHOD(user, send)
{
    auto* pG = get_Global(cid);
    if (!pG)
        return;

    MirrorCoin::Send pars;
    pars.m_Amount = amount;
    if (Utils::IsZero(pkDst))
        DerivePk(pars.m_User, cid);
    else
        Utils::Copy(pars.m_User, pkDst);

    FundsChange fc;
    fc.m_Aid = pG->m_Aid;
    fc.m_Amount = amount;
    fc.m_Consume = 1;

    Env::GenerateKernel(&cid, pars.s_iMethod, &pars, sizeof(pars), &fc, 1, nullptr, 0, "Send funds via MirrorCoin", 1000000U);
}

ON_METHOD(user, receive_all)
{
    auto* pG = get_Global(cid);
    if (!pG)
        return;

    IncomingWalker wlk(cid);
    if (!wlk.Restart(iStartFrom))
        return;

    PubKey pk;
    DerivePk(pk, cid);

    uint32_t nCount = 0;
    for (; wlk.MoveNext(&pk); nCount++)
    {
        MirrorCoin::Receive pars;
        pars.m_iCheckpoint = Utils::FromBE(wlk.m_pKey->m_KeyInContract.m_iCheckpoint_BE);
        pars.m_iMsg = Utils::FromBE(wlk.m_pKey->m_KeyInContract.m_iMsg_BE);

        FundsChange fc;
        fc.m_Aid = pG->m_Aid;
        fc.m_Amount = wlk.m_pMsg->m_Amount;
        fc.m_Consume = 0;

        SigRequest sig;
        sig.m_pID = &cid;
        sig.m_nID = sizeof(cid);

        Env::GenerateKernel(&cid, pars.s_iMethod, &pars, sizeof(pars), &fc, 1, &sig, 1, "Receive funds from MirrorCoin", 1000000U);
    }

    if (!nCount)
        OnError("no unspent funds");
}

#undef ON_METHOD
#undef THE_FIELD

export void Method_1() 
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
            MirrorCoin_##role##_##name(PAR_READ) \
            On_##role##_##name(MirrorCoin_##role##_##name(PAR_PASS) 0); \
            return; \
        }

#define THE_ROLE(name) \
    if (!Env::Strcmp(szRole, #name)) { \
        MirrorCoinRole_##name(THE_METHOD) \
        return OnError("invalid Action"); \
    }

    MirrorCoinRoles_All(THE_ROLE)

#undef THE_ROLE
#undef THE_METHOD
#undef PAR_PASS
#undef PAR_READ

    OnError("unknown Role");
}

