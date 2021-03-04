#include "../common.h"
#include "../app_common_impl.h"
#include "contract.h"

#define Pipe_manager_view(macro)

#define Pipe_manager_create(macro) \
    macro(uint32_t, Out_CheckpointMaxMsgs) \
    macro(uint32_t, Out_CheckpointMaxDH) \
    macro(HashValue, In_RulesRemote) \
    macro(Amount, In_ComissionPerMsg) \
    macro(Amount, In_StakeForRemote) \
    macro(Height, In_hDisputePeriod) \
    macro(Height, In_hContenderWaitPeriod) \
    macro(uint32_t, In_FakePoW)

#define Pipe_manager_set_remote(macro) \
    macro(ContractID, cid) \
    macro(ContractID, cidRemote)

#define Pipe_manager_get_NumOutCheckpoints(macro) \
    macro(ContractID, cid) \

#define Pipe_manager_get_OutCheckpoint(macro) \
    macro(ContractID, cid) \
    macro(uint32_t, iIdx) \
    macro(uint32_t, bMsgs) \
    macro(uint32_t, bProof) \

#define Pipe_manager_get_InCheckpointDispute(macro) \
    macro(ContractID, cid) \

#define PipeRole_manager(macro) \
    macro(manager, view) \
    macro(manager, create) \
    macro(manager, set_remote) \
    macro(manager, get_NumOutCheckpoints) \
    macro(manager, get_OutCheckpoint) \
    macro(manager, get_InCheckpointDispute) \


#define PipeRoles_All(macro) \
    macro(manager) \

export void Method_0()
{
    // scheme
    Env::DocGroup root("");

    {   Env::DocGroup gr("roles");

#define THE_FIELD(type, name) Env::DocAddText(#name, #type);
#define THE_METHOD(role, name) { Env::DocGroup grMethod(#name);  Pipe_##role##_##name(THE_FIELD) }
#define THE_ROLE(name) { Env::DocGroup grRole(#name); PipeRole_##name(THE_METHOD) }
        
        PipeRoles_All(THE_ROLE)
#undef THE_ROLE
#undef THE_METHOD
#undef THE_FIELD
    }
}

#define THE_FIELD(type, name) const type& name,
#define ON_METHOD(role, name) void On_##role##_##name(Pipe_##role##_##name(THE_FIELD) int unused = 0)

void OnError(const char* sz)
{
    Env::DocAddText("error", sz);
}

ON_METHOD(manager, view)
{
    EnumAndDumpContracts(Pipe::s_SID);
}

ON_METHOD(manager, create)
{
    Pipe::Create arg;
    arg.m_Cfg.m_Out.m_CheckpointMaxMsgs = Out_CheckpointMaxMsgs;
    arg.m_Cfg.m_Out.m_CheckpointMaxDH = Out_CheckpointMaxDH;
    arg.m_Cfg.m_In.m_RulesRemote = In_RulesRemote;
    arg.m_Cfg.m_In.m_ComissionPerMsg = In_ComissionPerMsg;
    arg.m_Cfg.m_In.m_StakeForRemote = In_StakeForRemote;
    arg.m_Cfg.m_In.m_hDisputePeriod = In_hDisputePeriod;
    arg.m_Cfg.m_In.m_hContenderWaitPeriod = In_hContenderWaitPeriod;
    arg.m_Cfg.m_In.m_FakePoW = !!In_FakePoW;

    Env::GenerateKernel(nullptr, arg.s_iMethod, &arg, sizeof(arg), nullptr, 0, nullptr, 0, "create Pipe contract", 0);
}

ON_METHOD(manager, set_remote)
{
    Pipe::SetRemote arg;
    arg.m_cid = cidRemote;

    Env::GenerateKernel(&cid, arg.s_iMethod, &arg, sizeof(arg), nullptr, 0, nullptr, 0, "Pipe contract 2nd-stage init", 0);
}

ON_METHOD(manager, get_NumOutCheckpoints)
{
    Env::Key_T<Pipe::StateOut::Key> key;
    key.m_Prefix.m_Cid = cid;

    auto* pState = Env::VarRead_T<Pipe::StateOut>(key);
    if (!pState)
    {
        OnError("no out state");
        return;
    }

    uint32_t res = pState->m_Checkpoint.m_iIdx;
    if (pState->IsCheckpointClosed(Env::get_Height() + 1))
        res++;

    Env::DocAddNum("count", res);
}

ON_METHOD(manager, get_OutCheckpoint)
{
    {
        Env::Key_T<Pipe::OutCheckpoint::Key> key;
        key.m_Prefix.m_Cid = cid;
        key.m_KeyInContract.m_iCheckpoint_BE = Utils::FromBE(iIdx);

        const Pipe::OutCheckpoint::ValueType* pCp;
        const Merkle::Node* pProof;
        uint32_t nVal;
        uint32_t nProof = Env::VarGetProof(&key, sizeof(key), (const void**) &pCp, &nVal, &pProof);

        if (!nProof)
        {
            OnError("no such a checkpoint");
            return;
        }

        Env::DocAddBlob_T("Hash", *pCp);

        if (bProof)
        {
            Env::DocGroup root("Proof");

            Env::DocAddNum("count", nProof);
            Env::DocAddBlob("nodes", pProof, sizeof(*pProof) * nProof);
        }
    }

    if (bMsgs)
    {
        Env::DocArray gr("msgs");

        Env::Key_T<Pipe::MsgHdr::KeyOut> key1;
        key1.m_Prefix.m_Cid = cid;
        key1.m_KeyInContract.m_iCheckpoint_BE = Utils::FromBE(iIdx);
        key1.m_KeyInContract.m_iMsg_BE = 0;

        auto key2 = key1;
        key1.m_KeyInContract.m_iMsg_BE = static_cast<uint32_t>(-1);

        Env::VarsEnum_T(key1, key2);

        while (true)
        {
            const void* pKey;
            const Pipe::MsgHdr* pHdr;
            uint32_t nKey, nVal;
            if (!Env::VarsMoveNext(&pKey, &nKey, (const void**) &pHdr, &nVal))
                break;

            if (nVal < sizeof(*pHdr))
                continue;

            Env::DocGroup gr("");
            Env::DocAddBlob_T("Sender", pHdr->m_Sender);
            Env::DocAddBlob_T("Receiver", pHdr->m_Receiver);

            nVal -= sizeof(*pHdr);
            Env::DocAddNum("Size", nVal);
            Env::DocAddBlob("Msg", pHdr + 1, nVal);
        }
    }
}

void DerivePk(PubKey& pk, const ContractID& cid)
{
    Env::DerivePk(pk, &cid, sizeof(cid));
}

ON_METHOD(manager, get_InCheckpointDispute)
{
    Env::Key_T<Pipe::StateIn::Key> key;
    key.m_Prefix.m_Cid = cid;

    auto* pState = Env::VarRead_T<Pipe::StateIn>(key);
    if (!pState)
    {
        OnError("no in state");
        return;
    }

    Env::DocAddNum("num", pState->m_Dispute.m_iIdx);

    if (pState->m_Dispute.m_Variants)
    {
        Env::DocAddNum("stake", pState->m_Dispute.m_Stake);
        Env::DocAddNum("Height", pState->m_Dispute.m_Height);

        PubKey pk;
        DerivePk(pk, cid);

        Env::Key_T<Pipe::Variant::Key> vk;
        vk.m_Prefix.m_Cid = cid;
        vk.m_KeyInContract.m_hvVariant = pState->m_Dispute.m_hvBestVariant;
        Env::VarsEnum_T(vk, vk);

        uint32_t nKey, nVar;
        const void* pKey;
        const Pipe::Variant* pVar;

        Env::VarsMoveNext(&pKey, &nKey, (const void**) &pVar, &nVar);

        Env::DocAddNum("is_my", (uint32_t) (_POD_(pk) == pVar->m_User));

        //if (_POD_(vk.m_KeyInContract.m_hvVariant).IsZero())
        //    pVar->Evaluate(vk.m_KeyInContract.m_hvVariant, nVar);

        Env::DocAddBlob_T("hash", vk.m_KeyInContract.m_hvVariant);
    }
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

#define PAR_READ(type, name) type arg_##name; Env::DocGet(#name, arg_##name);
#define PAR_PASS(type, name) arg_##name,

#define THE_METHOD(role, name) \
        if (!Env::Strcmp(szAction, #name)) { \
            Pipe_##role##_##name(PAR_READ) \
            On_##role##_##name(Pipe_##role##_##name(PAR_PASS) 0); \
            return; \
        }

#define THE_ROLE(name) \
    if (!Env::Strcmp(szRole, #name)) { \
        PipeRole_##name(THE_METHOD) \
        return OnError("invalid Action"); \
    }

    PipeRoles_All(THE_ROLE)

#undef THE_ROLE
#undef THE_METHOD
#undef PAR_PASS
#undef PAR_READ

    OnError("unknown Role");
}

