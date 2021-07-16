#include "../common.h"
#include "../Math.h"
#include "contract.h"
#include "../Ethash.h"
#include "../Eth.h"

namespace
{
struct RlpVisitor
{
    bool OnNode(const Eth::Rlp::Node& node)
    {
        auto& item = m_Items.emplace_back();
        item = node;
        return false;
    }

    Utils::Vector<Eth::Rlp::Node> m_Items;
};

bool FindBridgeLog(const uint8_t* buffer, uint32_t bufferSize, const uint8_t** data, uint32_t& dataSize)
{
    const uint8_t expectedTopic[] = { 160,17,220,234,188,149,79,103,20,251,148,168,222,215,64,236,190,17,46,241,182,194,35,31,163,69,9,69,158,54,52,85 };
    RlpVisitor v0;

    Env::Halt_if(!Eth::Rlp::Decode(buffer, bufferSize, v0));
    Env::Halt_if(v0.m_Items.m_Count != 1);
    Env::Halt_if(v0.m_Items.m_p[0].m_Type != Eth::Rlp::Node::Type::List);

    RlpVisitor v1;
    Env::Halt_if(!Eth::Rlp::Decode(v0.m_Items.m_p[0].m_pBuf, (uint32_t)v0.m_Items.m_p[0].m_nLen, v1));
    Env::Halt_if(v1.m_Items.m_Count != 4);
    Env::Halt_if(v1.m_Items.m_p[v1.m_Items.m_Count - 1].m_Type != Eth::Rlp::Node::Type::List);

    // get event list
    RlpVisitor v2;
    Env::Halt_if(!Eth::Rlp::Decode(v1.m_Items.m_p[v1.m_Items.m_Count - 1].m_pBuf, v1.m_Items.m_p[v1.m_Items.m_Count - 1].m_nLen, v2));
    Env::Halt_if(v2.m_Items.m_Count == 0);
    
    for (uint32_t idx = 0; idx < v2.m_Items.m_Count; ++idx)
    {
        RlpVisitor eventNode;
        Env::Halt_if(!Eth::Rlp::Decode(v2.m_Items.m_p[idx].m_pBuf, v2.m_Items.m_p[idx].m_nLen, eventNode));
        Env::Halt_if(eventNode.m_Items.m_Count != 3);

        RlpVisitor topics;
        Env::Halt_if(!Eth::Rlp::Decode(eventNode.m_Items.m_p[1].m_pBuf, eventNode.m_Items.m_p[1].m_nLen, topics));

        if (topics.m_Items.m_Count != 1) continue;
        if (topics.m_Items.m_p[0].m_nLen != 32) continue;
        if (!Env::Memcmp(topics.m_Items.m_p[0].m_pBuf, expectedTopic, 32))
        {
            dataSize = eventNode.m_Items.m_p[2].m_nLen;
            *data = eventNode.m_Items.m_p[2].m_pBuf;
            return true;
        }
    }
    return false;
}

bool CheckBridgeLog(const uint8_t* data, uint32_t dataSize, const Bridge::PushRemote& pushRemote, const uint8_t* msgBody, uint32_t msgBodySize)
{
    if (dataSize <= 160) return false;

    const uint8_t* idx = data;

    // check msgId
    Opaque<32> rawNumber;
    Env::Memcpy(&rawNumber, idx, 32);
    MultiPrecision::UInt<8> msgId;
    msgId.FromBE_T(rawNumber);
    if (msgId.get_Val<1>() != pushRemote.m_MsgId) return false;

    // check sender address
    idx += 32;
    if (Env::Memcmp(idx + 12, &pushRemote.m_MsgHdr.m_ContractSender, 20)) return false;

    // check receiver address
    idx += 32;
    if (Env::Memcmp(idx, &pushRemote.m_MsgHdr.m_ContractReceiver, 32)) return false;

    // check offset
    idx += 32;
    Env::Memcpy(&rawNumber, idx, 32);
    MultiPrecision::UInt<8> offset;
    offset.FromBE_T(rawNumber);
    if (offset.get_Val<1>() != 128u) return false;

    // check msg size
    idx += 32;
    Env::Memcpy(&rawNumber, idx, 32);
    MultiPrecision::UInt<8> size;
    size.FromBE_T(rawNumber);
    if (size.get_Val<1>() != msgBodySize) return false;

    // check msg
    idx += 32;
    if (Env::Memcmp(idx, msgBody, msgBodySize)) return false;

    return true;
}
} // namespace

// Method_0 - constructor, called once when the contract is deployed
export void Ctor(void*)
{
}

// Method_1 - destructor, called once when the contract is destroyed
export void Dtor(void*)
{
}

export void Method_2(const Bridge::PushLocal& value)
{
    uint32_t size = value.m_MsgSize + sizeof(Bridge::LocalMsgHdr);
    auto* pMsg = (Bridge::LocalMsgHdr*)Env::StackAlloc(size);

    if (Env::get_CallDepth() > 1)
        Env::get_CallerCid(1, pMsg->m_ContractSender);
    else
        _POD_(pMsg->m_ContractSender).SetZero();

    _POD_(pMsg->m_ContractReceiver) = value.m_ContractReceiver;
    Env::Memcpy(pMsg + 1, &value + 1, value.m_MsgSize);

    uint32_t localMsgCounter = 0;
    Env::LoadVar_T(Bridge::kLocalMsgCounterKey, localMsgCounter);

    Bridge::LocalMsgHdr::Key msgKey;
    msgKey.m_MsgId_BE = Utils::FromBE(++localMsgCounter);
    Env::SaveVar(&msgKey, sizeof(msgKey), pMsg, size, KeyTag::Internal);

    Env::SaveVar_T(Bridge::kLocalMsgCounterKey, localMsgCounter);
}

export void Method_3(const Bridge::PushRemote& value)
{
    // validate
    const auto& hdr = value.m_Header;

    Ethash::Hash512 hvSeed;
    hdr.get_SeedForPoW(hvSeed);

    Ethash::VerifyHdr(hdr.get_Epoch(), value.m_DatasetCount, hvSeed, hdr.m_Nonce, hdr.m_Difficulty, &value + 1, value.m_ProofSize); //?????

    uint32_t size = 0;
    uint8_t* out = nullptr;
    const uint8_t* receiptProof = (uint8_t*)(&value + 1) + value.m_ProofSize;
    const uint8_t* trieKey = receiptProof + value.m_ReceiptProofSize;
    uint32_t nibblesLength = 2 * value.m_TrieKeySize;
    uint8_t* pathInNibbles = (uint8_t*)Env::StackAlloc(nibblesLength);
    Eth::TriePathToNibbles(trieKey, value.m_TrieKeySize, pathInNibbles, nibblesLength);
    Env::Halt_if(!Eth::VerifyEthProof(pathInNibbles, nibblesLength, receiptProof, value.m_ReceiptProofSize, value.m_Header.m_ReceiptHash, &out, size));

    // TODO: add validation of the msgData
    const uint8_t* data = nullptr;
    uint32_t dataSize = 0;
    Env::Halt_if(!FindBridgeLog(out, size, &data, dataSize));

    // save msg
    const uint8_t* msgBody = trieKey + value.m_TrieKeySize;

    Env::Halt_if(!CheckBridgeLog(data, dataSize, value, msgBody, value.m_MsgSize));

    uint32_t fullMsgSize = value.m_MsgSize + sizeof(Bridge::RemoteMsgHdr);
    auto* pMsg = (Bridge::RemoteMsgHdr*)Env::StackAlloc(fullMsgSize);

    _POD_(*pMsg) = value.m_MsgHdr;
    Env::Memcpy(pMsg + 1, msgBody, value.m_MsgSize);

    Bridge::RemoteMsgHdr::Key keyMsg;
    keyMsg.m_MsgId_BE = Utils::FromBE(value.m_MsgId);

    Env::SaveVar(&keyMsg, sizeof(keyMsg), pMsg, fullMsgSize, KeyTag::Internal);
}

export void Method_4(Bridge::ReadRemote& value)
{
    Bridge::RemoteMsgHdr::Key keyMsg;
    keyMsg.m_MsgId_BE = Utils::FromBE(value.m_MsgId);

    uint32_t size = Env::LoadVar(&keyMsg, sizeof(keyMsg), nullptr, 0, KeyTag::Internal);
    Env::Halt_if(size < sizeof(Bridge::RemoteMsgHdr));

    auto* pMsg = (Bridge::RemoteMsgHdr*)Env::StackAlloc(size);
    Env::LoadVar(&keyMsg, sizeof(keyMsg), pMsg, size, KeyTag::Internal);

    // check the recipient
    ContractID cid;
    Env::get_CallerCid(1, cid);
    Env::Halt_if(_POD_(cid) != pMsg->m_ContractReceiver);

    // TODO: check
    // Env::DelVar_T(keyMsg);

    size -= sizeof(Bridge::RemoteMsgHdr);

    _POD_(value.m_ContractSender) = pMsg->m_ContractSender;
    Env::Memcpy(&value + 1, pMsg + 1, std::min(size, value.m_MsgSize));
    value.m_MsgSize = size;
}