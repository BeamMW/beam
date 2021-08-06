#include "../common.h"
#include "../app_common_impl.h"
#include "../Eth.h"
#include "contract.h"
#include "../bridge/contract.h"

// TODO: remove
namespace Env
{
    inline bool DocGet(const char* szID, Eth::Address& val) {
        return Env::DocGetBlobEx(szID, &val, sizeof(val));
    }
}

#define MirrorToken_manager_create(macro) \
    macro(ContractID, cidBridge)

#define MirrorToken_manager_set_remote(macro) \
    macro(ContractID, cid) \
    macro(Eth::Address, addressRemote)

#define MirrorToken_manager_view(macro)
#define MirrorToken_manager_view_params(macro) macro(ContractID, cid)
#define MirrorToken_manager_destroy(macro) macro(ContractID, cid)

#define MirrorToken_manager_view_incoming(macro) \
    macro(ContractID, cid) \
    macro(PubKey, pkUser) \
    macro(uint32_t, iStartFrom)

#define MirrorTokenRole_manager(macro) \
    macro(manager, create) \
    macro(manager, set_remote) \
    macro(manager, destroy) \
    macro(manager, view) \
    macro(manager, view_params) \
    macro(manager, view_incoming)

#define MirrorToken_user_view_incoming(macro) \
    macro(ContractID, cid) \
    macro(uint32_t, iStartFrom)

#define MirrorToken_user_get_pk(macro) \
    macro(ContractID, cid)

#define MirrorToken_user_send(macro) \
    macro(ContractID, cid) \
    macro(Eth::Address, receiver) \
    macro(Amount, amount)

#define MirrorToken_user_receive_all(macro) \
    macro(ContractID, cid) \
    macro(uint32_t, iStartFrom)

#define MirrorToken_user_receive(macro) \
    macro(ContractID, cid) \
    macro(uint32_t, msgId)

#define MirrorToken_user_mint(macro) \
    macro(ContractID, cid) \
    macro(Amount, amount)

#define MirrorTokenRole_user(macro) \
    macro(user, get_pk) \
    macro(user, view_incoming) \
    macro(user, send) \
    macro(user, receive_all) \
    macro(user, receive) \
    macro(user, mint)

#define MirrorTokenRoles_All(macro) \
    macro(manager) \
    macro(user)

BEAM_EXPORT void Method_0()
{
    // scheme
    Env::DocGroup root("");

    {   Env::DocGroup gr("roles");

#define THE_FIELD(type, name) Env::DocAddText(#name, #type);
#define THE_METHOD(role, name) { Env::DocGroup grMethod(#name);  MirrorToken_##role##_##name(THE_FIELD) }
#define THE_ROLE(name) { Env::DocGroup grRole(#name); MirrorTokenRole_##name(THE_METHOD) }
        
    MirrorTokenRoles_All(THE_ROLE)
#undef THE_ROLE
#undef THE_METHOD
#undef THE_FIELD
    }
}

#define THE_FIELD(type, name) const type& name,
#define ON_METHOD(role, name) void On_##role##_##name(MirrorToken_##role##_##name(THE_FIELD) int unused = 0)

void OnError(const char* sz)
{
    Env::DocAddText("error", sz);
}

struct ParamsPlus : public MirrorToken::Params
{
    bool get(const ContractID& cid)
    {
        Env::Key_T<uint8_t> gk;
        _POD_(gk.m_Prefix.m_Cid) = cid;
        gk.m_KeyInContract = MirrorToken::kParamsKey;

        if (Env::VarReader::Read_T(gk, *this))
            return true;

        OnError("no params");
        return false;
    }
};

struct IncomingWalker
{
    const ContractID& m_Cid;
    IncomingWalker(const ContractID& cid) :m_Cid(cid) {}

    Eth::Address m_Remote;
    Env::VarReaderEx<true> m_Reader;

#pragma pack (push, 1)
    struct MyMsg
        :public Bridge::RemoteMsgHdr
        ,public MirrorToken::InMessage
    {
    };
#pragma pack (pop)

    Env::Key_T<Bridge::RemoteMsgHdr::Key> m_Key;
    MyMsg m_Msg;


    bool Restart(uint32_t iStartFrom)
    {
        ParamsPlus params;
        if (!params.get(m_Cid))
            return false;

        m_Remote = params.m_Remote;

        Env::Key_T<Bridge::RemoteMsgHdr::Key> k1;
        k1.m_Prefix.m_Cid = params.m_BridgeID;
        k1.m_KeyInContract.m_MsgId_BE = Utils::FromBE(iStartFrom);

        auto k2 = k1;
        k2.m_KeyInContract.m_MsgId_BE = -1;

        m_Reader.Enum_T(k1, k2);
        return true;
    }

    bool MoveNext(const PubKey* pPk)
    {
        while (true)
        {
            if (!m_Reader.MoveNext_T(m_Key, m_Msg))
                return false;

            if ((_POD_(m_Msg.m_ContractSender) != m_Remote) || (_POD_(m_Msg.m_ContractReceiver) != m_Cid))
                continue;

            if (pPk && (_POD_(*pPk) !=m_Msg.m_User))
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
        Env::DocAddNum("MsgId", Utils::FromBE(wlk.m_Key.m_KeyInContract.m_MsgId_BE));
        Env::DocAddNum("amount", Utils::FromBE(wlk.m_Msg.m_Amount));

        if (!pPk)
            Env::DocAddBlob_T("User", wlk.m_Msg.m_User);
    }
}

ON_METHOD(manager, view)
{
    EnumAndDumpContracts(MirrorToken::s_SID);
}

static const Amount g_DepositCA = 300000000000ULL; // 3K beams

ON_METHOD(manager, create)
{
    static const char szMeta[] = "metadata";

    uint32_t nMetaSize = Env::DocGetText(szMeta, nullptr, 0);
    if (nMetaSize < 2)
    {
        OnError("metadata should be non-empty");
        return;
    }

    auto* pArg = (MirrorToken::Create*) Env::StackAlloc(sizeof(MirrorToken::Create) + nMetaSize);
    pArg->m_BridgeID = cidBridge;

    Env::DocGetText(szMeta, (char*)(pArg + 1), nMetaSize);
    nMetaSize--;

    pArg->m_MetadataSize = nMetaSize;

    FundsChange fc;
    fc.m_Aid = 0; // asset id
    fc.m_Amount = g_DepositCA; // amount of the input or output
    fc.m_Consume = 1; // contract consumes funds (i.e input, in this case)

    Env::GenerateKernel(nullptr, pArg->s_iMethod, pArg, sizeof(*pArg) + nMetaSize, &fc, 1, nullptr, 0, "generate MirrorToken contract", 0);
}

ON_METHOD(manager, set_remote)
{
    MirrorToken::SetRemote pars;
    pars.m_Remote = addressRemote;

    Env::GenerateKernel(&cid, pars.s_iMethod, &pars, sizeof(pars), nullptr, 0, nullptr, 0, "Set MirrorToken contract remote counter-part", 0);
}

ON_METHOD(manager, destroy)
{
    Env::GenerateKernel(&cid, 1, nullptr, 0, nullptr, 0, nullptr, 0, "destroy MirrorToken contract", 0);
}

ON_METHOD(manager, view_params)
{
    ParamsPlus params;
    if (!params.get(cid))
        return;

    Env::DocGroup gr("params");

    Env::DocAddNum("aid", params.m_Aid);
    Env::DocAddBlob_T("RemoteID", params.m_Remote);
    Env::DocAddBlob_T("PipeID", params.m_BridgeID);
}

ON_METHOD(manager, view_incoming)
{
    ViewIncoming(cid, _POD_(pkUser).IsZero() ? nullptr : &pkUser, iStartFrom);
}

void DerivePk(PubKey& pk, const ContractID& cid)
{
    Env::DerivePk(pk, &cid, sizeof(cid));
}

ON_METHOD(user, get_pk)
{
    PubKey pk;
    DerivePk(pk, cid);
    Env::DocAddBlob_T("pk", pk);
}

ON_METHOD(user, view_incoming)
{
    PubKey pk;
    DerivePk(pk, cid);
    ViewIncoming(cid, &pk, iStartFrom);
}

ON_METHOD(user, send)
{
    ParamsPlus params;
    if (!params.get(cid))
        return;

    MirrorToken::Send pars;
    pars.m_Amount = amount;
    _POD_(pars.m_User) = receiver;

    FundsChange fc;
    fc.m_Aid = params.m_Aid;
    fc.m_Amount = amount;
    fc.m_Consume = 1;

    Env::GenerateKernel(&cid, pars.s_iMethod, &pars, sizeof(pars), &fc, 1, nullptr, 0, "Send funds via MirrorToken", 0);
}

ON_METHOD(user, receive_all)
{
    ParamsPlus params;
    if (!params.get(cid))
        return;

    FundsChange fc;
    fc.m_Aid = params.m_Aid;
    fc.m_Consume = 0;

    IncomingWalker wlk(cid);
    if (!wlk.Restart(iStartFrom))
        return;

    PubKey pk;
    DerivePk(pk, cid);

    uint32_t nCount = 0;
    for (; wlk.MoveNext(&pk); nCount++)
    {
        MirrorToken::Receive pars;
        pars.m_MsgId = Utils::FromBE(wlk.m_Key.m_KeyInContract.m_MsgId_BE);

        fc.m_Amount = wlk.m_Msg.m_Amount;

        SigRequest sig;
        sig.m_pID = &cid;
        sig.m_nID = sizeof(cid);

        Env::GenerateKernel(&cid, pars.s_iMethod, &pars, sizeof(pars), &fc, 1, &sig, 1, "Receive funds from MirrorToken", 0);
    }

    if (!nCount)
        OnError("no unspent funds");
}

ON_METHOD(user, receive)
{
    ParamsPlus params;
    if (!params.get(cid))
        return;

    Env::Key_T<Bridge::RemoteMsgHdr::Key> key;
    key.m_Prefix.m_Cid = params.m_BridgeID;
    key.m_KeyInContract.m_MsgId_BE = Utils::FromBE(msgId);

    IncomingWalker::MyMsg msg;
    if (Env::VarReader::Read_T(key, msg))
    {

        FundsChange fc;
        fc.m_Aid = params.m_Aid;
        fc.m_Amount = Utils::FromBE(msg.m_Amount);
        fc.m_Consume = 0;

        MirrorToken::Receive pars;
        pars.m_MsgId = msgId;

        SigRequest sig;
        sig.m_pID = &cid;
        sig.m_nID = sizeof(cid);

        Env::GenerateKernel(&cid, pars.s_iMethod, &pars, sizeof(pars), &fc, 1, &sig, 1, "Receive funds from MirrorToken", 0);
        return;
    }

    OnError("absent message");
}

ON_METHOD(user, mint)
{
    ParamsPlus params;
    if (!params.get(cid))
        return;

    FundsChange fc;
    fc.m_Aid = params.m_Aid;
    fc.m_Amount = amount;
    fc.m_Consume = 0;

    MirrorToken::Mint arg;
    arg.m_Amount = amount;

    Env::GenerateKernel(&cid, arg.s_iMethod, &arg, sizeof(arg), &fc, 1, nullptr, 0, "Mint MirrorToken", 0);
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
            MirrorToken_##role##_##name(PAR_READ) \
            On_##role##_##name(MirrorToken_##role##_##name(PAR_PASS) 0); \
            return; \
        }

#define THE_ROLE(name) \
    if (!Env::Strcmp(szRole, #name)) { \
        MirrorTokenRole_##name(THE_METHOD) \
        return OnError("invalid Action"); \
    }

    MirrorTokenRoles_All(THE_ROLE)

#undef THE_ROLE
#undef THE_METHOD
#undef PAR_PASS
#undef PAR_READ

    OnError("unknown Role");
}

