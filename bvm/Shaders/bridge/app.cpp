#include "../common.h"
#include "../app_common_impl.h"
#include "contract.h"
#include "../Ethash.h"

namespace
{
    void OnError(const char* sz)
    {
        Env::DocAddText("error", sz);
    }
} // namespace

namespace manager
{
    void Create()
    {
        Env::GenerateKernel(nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0, "create Bridge contract", 0);
    }

    void View()
    {
        EnumAndDumpContracts(Bridge::s_SID);
    }

    void PushRemote(const ContractID& cid, uint32_t msgId, const Bridge::RemoteMsgHdr& msgHdr, const Eth::Header& header, uint32_t datasetCount)
    {
        uint32_t proofSize = Env::DocGetBlob("powProof", nullptr, 0);
        uint32_t receiptProofSize = Env::DocGetBlob("receiptProof", nullptr, 0);
        uint32_t trieKeySize = Env::DocGetBlob("txIndex", nullptr, 0);
        uint32_t msgBodySize = Env::DocGetBlob("msgBody", nullptr, 0);
        uint32_t fullArgsSize = sizeof(Bridge::PushRemote) + proofSize + receiptProofSize + trieKeySize + msgBodySize;
        auto* arg = (Bridge::PushRemote*)Env::StackAlloc(fullArgsSize);
        uint8_t* tmp = (uint8_t*)(arg + 1);

        Env::DocGetBlob("powProof", tmp, proofSize);
        tmp += proofSize;
        Env::DocGetBlob("receiptProof", tmp, receiptProofSize);
        tmp += receiptProofSize;
        Env::DocGetBlob("txIndex", tmp, trieKeySize);
        tmp += trieKeySize;
        Env::DocGetBlob("msgBody", tmp, msgBodySize);

        arg->m_DatasetCount = datasetCount;
        _POD_(arg->m_Header) = header;
        _POD_(arg->m_MsgHdr) = msgHdr;
        arg->m_MsgId = msgId;
        arg->m_MsgSize = msgBodySize;
        arg->m_ProofSize = proofSize;
        arg->m_ReceiptProofSize = receiptProofSize;
        arg->m_TrieKeySize = trieKeySize;

        Env::GenerateKernel(&cid, arg->s_iMethod, arg, fullArgsSize, nullptr, 0, nullptr, 0, "Bridge::PushRemote", 0);
    }

    void GetLocalMsgCount(const ContractID& cid)
    {
        Env::Key_T<uint8_t> key;
        key.m_KeyInContract = Bridge::kLocalMsgCounterKey;
        key.m_Prefix.m_Cid = cid;
        
        uint32_t localMsgCounter = 0;
        Env::VarReader::Read_T(key, localMsgCounter);

        Env::DocAddNum32("count", localMsgCounter);
    }

    void GetLocalMsg(const ContractID& cid, uint32_t msgId)
    {
        Env::Key_T<Bridge::LocalMsgHdr::Key> msgKey;
        msgKey.m_Prefix.m_Cid = cid;
        msgKey.m_KeyInContract.m_MsgId_BE = Utils::FromBE(msgId);

        Env::VarReader reader(msgKey, msgKey);

        uint32_t size = 0;
        uint32_t keySize = sizeof(msgKey);
        Bridge::LocalMsgHdr* pMsg;
        if (!reader.MoveNext(nullptr, keySize, pMsg, size, 0))
        {
            OnError("msg with current id is absent");
            return;
        }
        pMsg = (Bridge::LocalMsgHdr*)Env::StackAlloc(size);
        if (!reader.MoveNext(nullptr, keySize, pMsg, size, 1))
        {
            OnError("msg with current id is absent");
            return;
        }

        Env::DocAddBlob_T("sender", pMsg->m_ContractSender);
        Env::DocAddBlob_T("receiver", pMsg->m_ContractReceiver);
        Env::DocAddBlob("body", pMsg + 1, size - sizeof(*pMsg));
    }

    void GetLocalMsgProof(const ContractID& cid, uint32_t msgId)
    {
        Env::Key_T<Bridge::LocalMsgHdr::Key> key;
        key.m_Prefix.m_Cid = cid;
        key.m_KeyInContract.m_MsgId_BE = Utils::FromBE(msgId);

        const uint8_t* msg;
        uint32_t msgTotalSize;
        const Merkle::Node* proof;

        uint32_t proofCount = Env::VarGetProof(&key, sizeof(key), (const void**)&msg, &msgTotalSize, &proof);

        if (!proofCount)
        {
            OnError("no such a checkpoint");
            return;
        }

        Env::DocAddBlob("Msg", msg, msgTotalSize);
        Env::DocGroup root("Proof");
        Env::DocAddNum("count", proofCount);
        Env::DocAddBlob("nodes", proof, sizeof(*proof) * proofCount);
        Env::DocAddNum64("height", Env::get_Height());
    }
} // namespace manager

export void Method_0()
{
    // scheme
    Env::DocGroup root("");
    {
        Env::DocGroup gr("roles");
        {
            Env::DocGroup grRole("manager");
            {
                Env::DocGroup grMethod("create");
            }
            {
                Env::DocGroup grMethod("view");
            }
            {
                Env::DocGroup grMethod("pushRemote");
                Env::DocAddText("cid", "ContractID");
                Env::DocAddText("msgId", "uint32");
            }
            {
                Env::DocGroup grMethod("getLocalMsgCount");
                Env::DocAddText("cid", "ContractID");
            }
            {
                Env::DocGroup grMethod("getLocalMsg");
                Env::DocAddText("cid", "ContractID");
                Env::DocAddText("msgId", "uint32");
            }
            {
                Env::DocGroup grMethod("getLocalMsgProof");
                Env::DocAddText("cid", "ContractID");
                Env::DocAddText("msgId", "uint32");
            }
        }
    }
}

export void Method_1()
{
    Env::DocGroup root("");

    char szRole[0x10], szAction[0x12];

    if (!Env::DocGetText("role", szRole, sizeof(szRole)))
        return OnError("Role not specified");

    if (!Env::DocGetText("action", szAction, sizeof(szAction)))
        return OnError("Action not specified");

    if (!Env::Strcmp(szRole, "manager"))
    {
        if (!Env::Strcmp(szAction, "create"))
        {
            manager::Create();
            return;
        }
        if (!Env::Strcmp(szAction, "view"))
        {
            manager::View();
            return;
        }
        if (!Env::Strcmp(szAction, "pushRemote"))
        {
            ContractID cid;
            Env::DocGet("cid", cid);

            Bridge::RemoteMsgHdr msgHdr;
            Env::DocGet("contractReceiver", msgHdr.m_ContractReceiver);
            Env::DocGetBlobEx("contractSender", &msgHdr.m_ContractSender, sizeof(msgHdr.m_ContractSender));
            uint32_t msgId = 0;
            Env::DocGetNum32("msgId", &msgId);

            // fill ETH header
            Eth::Header header;
            Env::DocGet("parentHash", header.m_ParentHash);
            Env::DocGet("uncleHash", header.m_UncleHash);
            Env::DocGetBlob("coinbase", &header.m_Coinbase, sizeof(header.m_Coinbase));
            Env::DocGet("root", header.m_Root);
            Env::DocGet("txHash", header.m_TxHash);
            Env::DocGet("receiptHash", header.m_ReceiptHash);
            Env::DocGetBlob("bloom", &header.m_Bloom, sizeof(header.m_Bloom));
            header.m_nExtra = Env::DocGetBlob("extra", &header.m_Extra, sizeof(header.m_Extra));

            Env::DocGetNum64("difficulty", &header.m_Difficulty);
            Env::DocGetNum64("number", &header.m_Number);
            Env::DocGetNum64("gasLimit", &header.m_GasLimit);
            Env::DocGetNum64("gasUsed", &header.m_GasUsed);
            Env::DocGetNum64("time", &header.m_Time);
            Env::DocGetNum64("nonce", &header.m_Nonce);
            Env::DocGetNum64("baseFeePerGas", &header.m_BaseFeePerGas);

            uint32_t datasetCount = 0;
            Env::DocGetNum32("datasetCount", &datasetCount);

            manager::PushRemote(cid, msgId, msgHdr, header, datasetCount);
            return;
        }
        if (!Env::Strcmp(szAction, "getLocalMsgCount"))
        {
            ContractID cid;
            Env::DocGet("cid", cid);
            manager::GetLocalMsgCount(cid);
            return;
        }
        if (!Env::Strcmp(szAction, "getLocalMsg"))
        {
            ContractID cid;
            Env::DocGet("cid", cid);
            uint32_t msgId = 0;
            Env::DocGetNum32("msgId", &msgId);
            manager::GetLocalMsg(cid, msgId);
            return;
        }
        if (!Env::Strcmp(szAction, "getLocalMsgProof"))
        {
            ContractID cid;
            Env::DocGet("cid", cid);
            uint32_t msgId = 0;
            Env::DocGetNum32("msgId", &msgId);
            manager::GetLocalMsgProof(cid, msgId);
            return;
        }
        else
        {
            return OnError("invalid Action.");
        }
    }
}