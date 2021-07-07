#include "../common.h"
#include "../app_common_impl.h"
#include "contract.h"
#include "../Ethash.h"

namespace
{
    uint32_t ReadAid(const ContractID& cid)
    {
        Env::Key_T<uint32_t> key;
        key.m_KeyInContract = 0;
        key.m_Prefix.m_Cid = cid;

        uint32_t aid;
        Env::VarReader::Read_T(key, aid);
        return aid;
    }

    void ReadImportedMsg(Bridge::InMsg& msg, const ContractID& cid)
    {
        Env::Key_T<uint32_t> key;
        key.m_KeyInContract = 1;
        key.m_Prefix.m_Cid = cid;

        Env::VarReader::Read_T(key, msg);
    }

    void OnError(const char* sz)
    {
        Env::DocAddText("error", sz);
    }
} // namespace

namespace manager
{
    void Create()
    {
        FundsChange fc;
        fc.m_Aid = 0; // asset id
        fc.m_Amount = 300000000000ULL; // 3K beams; // amount of the input or output
        fc.m_Consume = 1; // contract consumes funds (i.e input, in this case)
        Env::GenerateKernel(nullptr, 0, nullptr, 0, &fc, 1, nullptr, 0, "create Bridge contract", 0);
    }

    void View()
    {
        EnumAndDumpContracts(Bridge::s_SID);
    }

    void ImportMsg(const ContractID& cid, uint32_t amount, const PubKey& pk, const Eth::Header& header, uint32_t datasetCount)
    {
        uint32_t proofSize = Env::DocGetBlob("proof", nullptr, 0);
        uint32_t receiptProofSize = Env::DocGetBlob("receiptProof", nullptr, 0);
        uint32_t trieKeySize = Env::DocGetBlob("txIndex", nullptr, 0);
        auto* arg = (Bridge::ImportMessage*)Env::StackAlloc(sizeof(Bridge::ImportMessage) + proofSize + receiptProofSize + trieKeySize);
        uint8_t* tmp = (uint8_t*)(arg + 1);

        Env::DocGetBlob("proof", tmp, proofSize);
        tmp += proofSize;
        Env::DocGetBlob("receiptProof", tmp, receiptProofSize);
        tmp += receiptProofSize;
        Env::DocGetBlob("txIndex", tmp, trieKeySize);

        /*Env::DocAddNum32("proof_size", proofSize);
        tmp = (uint8_t*)(arg + 1);
        Env::DocAddBlob("proof", tmp, proofSize);
        Env::DocAddNum32("receipt_proof_size", receiptProofSize);
        tmp += proofSize;
        Env::DocAddBlob("receipt_proof", tmp, receiptProofSize);*/

        Bridge::InMsg msg;

        msg.m_Amount = amount;
        msg.m_Pk = pk;
        msg.m_Finalized = 0;
        arg->m_Msg = msg;
        arg->m_DatasetCount = datasetCount;
        arg->m_ProofSize = proofSize;
        arg->m_ReceiptProofSize = receiptProofSize;
        arg->m_TrieKeySize = trieKeySize;

        _POD_(arg->m_Header) = header;

        Env::GenerateKernel(&cid, arg->s_iMethod, arg, sizeof(*arg) + proofSize + receiptProofSize + trieKeySize, nullptr, 0, nullptr, 0, "Import message", 0);
    }

    void ExportMsg(const ContractID& cid)
    {

    }

    void FinalizeMsg(const ContractID& cid)
    {
        Bridge::Finalized arg;
        Env::GenerateKernel(&cid, arg.s_iMethod, &arg, sizeof(arg), nullptr, 0, nullptr, 0, "Finalize message", 0);
    }

    void Unlock(const ContractID& cid)
    {
        Bridge::InMsg msg;

        ReadImportedMsg(msg, cid);

        FundsChange fc;
        fc.m_Aid = ReadAid(cid); // asset id
        fc.m_Amount = msg.m_Amount; // amount of the input or output
        fc.m_Consume = 0; // contract consumes funds (i.e input, in this case)
        

        Bridge::Unlock arg;

        SigRequest sig;
        sig.m_pID = &cid;
        sig.m_nID = sizeof(cid);

        Env::GenerateKernel(&cid, arg.s_iMethod, &arg, sizeof(arg), &fc, 1, &sig, 1, "Mint", 100000000);
    }

    void Lock(const ContractID& cid, uint32_t aid, uint32_t amount)
    {
        FundsChange fc;
        fc.m_Aid = aid; // asset id
        fc.m_Amount = amount; // amount of the input or output
        fc.m_Consume = 1; // contract consumes funds (i.e input, in this case)


        Bridge::Lock arg;
        arg.m_Amount = amount;

        Env::GenerateKernel(&cid, arg.s_iMethod, &arg, sizeof(arg), &fc, 1, nullptr, 0, "Lock", 0);
    }

    void GeneratePK(const ContractID& cid)
    {
        PubKey pubKey;
        Env::DerivePk(pubKey, &cid, sizeof(cid));

        Env::DocAddBlob_T("pubkey", pubKey);
    }

    void GetAid(const ContractID& cid)
    {
        Env::DocAddNum32("aid", ReadAid(cid));
    }

    void PushRemote(const ContractID& cid, uint32_t msgId, const Bridge::RemoteMsgHdr& msgHdr, const Eth::Header& header, uint32_t datasetCount)
    {
        uint32_t proofSize = Env::DocGetBlob("proof", nullptr, 0);
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
        tmp += msgBodySize;
        Env::DocGetBlob("msgData", tmp, msgBodySize);

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
        reader.MoveNext(nullptr, keySize, pMsg, size, 0); // check result
        pMsg = (Bridge::LocalMsgHdr*)Env::StackAlloc(size);
        reader.MoveNext(nullptr, keySize, pMsg, size, 1); // check result

        Env::DocAddBlob_T("sender", pMsg->m_ContractSender);
        Env::DocAddBlob_T("receiver", pMsg->m_ContractReceiver);
        Env::DocAddBlob("body", pMsg + 1, size - sizeof(*pMsg));
        Env::DocAddBlob("msg", pMsg, size);
    }

    void GetLocalMsgProof(const ContractID& cid, uint32_t msgId)
    {
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
        }
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
                Env::DocGroup grMethod("importMsg");
                Env::DocAddText("cid", "ContractID");
                Env::DocAddText("amount", "uint32");
                Env::DocAddText("pubkey", "PubKey");
            }
            {
                Env::DocGroup grMethod("exportMsg");
                Env::DocAddText("cid", "ContractID");
            }
            {
                Env::DocGroup grMethod("finalizeMsg");
                Env::DocAddText("cid", "ContractID");
            }
            {
                Env::DocGroup grMethod("unlock");
                Env::DocAddText("cid", "ContractID");
            }
            {
                Env::DocGroup grMethod("lock");
                Env::DocAddText("cid", "ContractID");
                Env::DocAddText("aid", "uint32");
                Env::DocAddText("amount", "uint32");
            }
            {
                Env::DocGroup grMethod("generatePK");
                Env::DocAddText("cid", "ContractID");
            }
            {
                Env::DocGroup grMethod("getAid");
                Env::DocAddText("cid", "ContractID");
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
        if (!Env::Strcmp(szAction, "importMsg"))
        {
            ContractID cid;
            Env::DocGet("cid", cid);
            uint32_t amount = 0;
            Env::DocGetNum32("amount", &amount);
            PubKey pk;
            Env::DocGet("pubkey", pk);
            
            Eth::Header header;
            Env::DocGet("parentHash", header.m_ParentHash);
            Env::DocGet("uncleHash", header.m_UncleHash);
            Env::DocGetBlob("coinbase", &header.m_Coinbase, sizeof(header.m_Coinbase)); //??
            Env::DocGet("root", header.m_Root);
            Env::DocGet("txHash", header.m_TxHash);
            Env::DocGet("receiptHash", header.m_ReceiptHash);
            Env::DocGetBlob("bloom", &header.m_Bloom, sizeof(header.m_Bloom)); // ??
            header.m_nExtra = Env::DocGetBlob("extra", &header.m_Extra, sizeof(header.m_Extra)); // ??

            Env::DocAddNum32("extra_size", header.m_nExtra);
            
            Env::DocGetNum64("difficulty", &header.m_Difficulty);
            Env::DocGetNum64("number", &header.m_Number);
            Env::DocGetNum64("gasLimit", &header.m_GasLimit);
            Env::DocGetNum64("gasUsed", &header.m_GasUsed);
            Env::DocGetNum64("time", &header.m_Time);
            Env::DocGetNum64("nonce", &header.m_Nonce);

            uint32_t datasetCount = 0;
            Env::DocGetNum32("datasetCount", &datasetCount);

            manager::ImportMsg(cid, amount, pk, header, datasetCount);
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

            Env::DocAddNum32("extra_size", header.m_nExtra);
            Env::DocGetNum64("difficulty", &header.m_Difficulty);
            Env::DocGetNum64("number", &header.m_Number);
            Env::DocGetNum64("gasLimit", &header.m_GasLimit);
            Env::DocGetNum64("gasUsed", &header.m_GasUsed);
            Env::DocGetNum64("time", &header.m_Time);
            Env::DocGetNum64("nonce", &header.m_Nonce);

            uint32_t datasetCount = 0;
            Env::DocGetNum32("datasetCount", &datasetCount);

            manager::PushRemote(cid, msgId, msgHdr, header, datasetCount);
            return;
        }
        if (!Env::Strcmp(szAction, "generateSeed"))
        {
            Eth::Header header;
            Env::DocGet("parentHash", header.m_ParentHash);
            Env::DocGet("uncleHash", header.m_UncleHash);
            Env::DocGetBlob("coinbase", &header.m_Coinbase, sizeof(header.m_Coinbase)); //??
            Env::DocGet("root", header.m_Root);
            Env::DocGet("txHash", header.m_TxHash);
            Env::DocGet("receiptHash", header.m_ReceiptHash);
            Env::DocGetBlob("bloom", &header.m_Bloom, sizeof(header.m_Bloom)); // ??
            header.m_nExtra = Env::DocGetBlob("extra", &header.m_Extra, sizeof(header.m_Extra)); // ??

            Env::DocAddNum32("extra_size", header.m_nExtra);

            Env::DocGetNum64("difficulty", &header.m_Difficulty);
            Env::DocGetNum64("number", &header.m_Number);
            Env::DocGetNum64("gasLimit", &header.m_GasLimit);
            Env::DocGetNum64("gasUsed", &header.m_GasUsed);
            Env::DocGetNum64("time", &header.m_Time);
            Env::DocGetNum64("nonce", &header.m_Nonce);

            Ethash::Hash512 hvSeed;
            header.get_SeedForPoW(hvSeed);

            Env::DocAddBlob_T("seed", hvSeed);
            Env::DocAddBlob_T("parentHash", header.m_ParentHash);
            Env::DocAddBlob_T("uncleHash", header.m_UncleHash);
            Env::DocAddBlob_T("coinbase", header.m_Coinbase);
            Env::DocAddBlob_T("root", header.m_Root);
            Env::DocAddBlob_T("txHash", header.m_TxHash);
            Env::DocAddBlob_T("receiptHash", header.m_ReceiptHash);
            Env::DocAddBlob_T("bloom", header.m_Bloom);
            Env::DocAddBlob_T("extra", header.m_Extra);

            Env::DocAddNum64("difficulty", header.m_Difficulty);
            Env::DocAddNum64("number", header.m_Number);
            Env::DocAddNum64("gasLimit", header.m_GasLimit);
            Env::DocAddNum64("gasUsed", header.m_GasUsed);
            Env::DocAddNum64("time", header.m_Time);
            Env::DocAddNum64("nonce", header.m_Nonce);

            return;
        }
        if (!Env::Strcmp(szAction, "exportMsg"))
        {
            ContractID cid;
            Env::DocGet("cid", cid);
            manager::ExportMsg(cid);
            return;
        }
        if (!Env::Strcmp(szAction, "finalizeMsg"))
        {
            ContractID cid;
            Env::DocGet("cid", cid);
            manager::FinalizeMsg(cid);
            return;
        }
        if (!Env::Strcmp(szAction, "unlock"))
        {
            ContractID cid;
            Env::DocGet("cid", cid);
            manager::Unlock(cid);
            return;
        }
        if (!Env::Strcmp(szAction, "lock"))
        {
            ContractID cid;
            Env::DocGet("cid", cid);
            uint32_t amount = 0;
            Env::DocGetNum32("amount", &amount);
            uint32_t aid = 0;
            Env::DocGetNum32("aid", &aid);
            manager::Lock(cid, aid, amount);
            return;
        }
        if (!Env::Strcmp(szAction, "generatePK"))
        {
            ContractID cid;
            Env::DocGet("cid", cid);
            manager::GeneratePK(cid);
            return;
        }
        if (!Env::Strcmp(szAction, "getAid"))
        {
            ContractID cid;
            Env::DocGet("cid", cid);
            manager::GetAid(cid);
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