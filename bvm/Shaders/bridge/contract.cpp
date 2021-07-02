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
} // namespace

// Method_0 - constructor, called once when the contract is deployed
export void Ctor(void*)
{
    const char meta[] = "testcoin17";
    AssetID aid = Env::AssetCreate(meta, sizeof(meta) - 1);

    uint32_t key = 0;
    Env::SaveVar_T(key, aid);

    Env::SaveVar_T(Bridge::kLocalPckgCounterKey, uint32_t(0));
}

// Method_1 - destructor, called once when the contract is destroyed
export void Dtor(void*)
{
}

export void Method_2(const Bridge::Unlock& value)
{
    uint32_t key = 0;
    AssetID aid;

    Env::LoadVar_T(key, aid);

    // read message
    key = 1;
    Bridge::InMsg msg;
    Env::LoadVar_T(key, msg);

    Env::Halt_if(!msg.m_Finalized);
    Env::AssetEmit(aid, msg.m_Amount, 1);
    Env::FundsUnlock(aid, msg.m_Amount);
    Env::AddSig(msg.m_Pk);
}

export void Method_3(const Bridge::Lock& value)
{
    uint32_t key = 0;
    AssetID aid;

    Env::LoadVar_T(key, aid);

    Env::FundsLock(aid, value.m_Amount);
}

export void Method_4(Bridge::ImportMessage& value)
{
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

    // only for DummyContract
    RlpVisitor v0;
    Env::Halt_if(!Eth::Rlp::Decode(out, size, v0));
    RlpVisitor v1;
    Env::Halt_if(!Eth::Rlp::Decode(v0.m_Items.m_p[0].m_pBuf, v0.m_Items.m_p[0].m_nLen, v1));
    RlpVisitor v2;
    Env::Halt_if(!Eth::Rlp::Decode(v1.m_Items.m_p[v1.m_Items.m_Count - 1].m_pBuf, v1.m_Items.m_p[v1.m_Items.m_Count - 1].m_nLen, v2));
    RlpVisitor v3;
    Env::Halt_if(!Eth::Rlp::Decode(v2.m_Items.m_p[v2.m_Items.m_Count - 1].m_pBuf, v2.m_Items.m_p[v2.m_Items.m_Count - 1].m_nLen, v3));

    // check only data
    const Eth::Rlp::Node& node = v3.m_Items.m_p[v3.m_Items.m_Count - 1];
    const uint8_t* idx = node.m_pBuf;

    // look at https://docs.soliditylang.org/en/v0.5.3/abi-spec.html#events
    // check buffer size 
    Env::Halt_if(node.m_nLen != 192);

    // check amount
    idx += 32;
    Opaque<32> rawNumber;
    Env::Memcpy(&rawNumber, idx, 32);
    MultiPrecision::UInt<8> amount;
    amount.FromBE_T(rawNumber);
    MultiPrecision::UInt<8> expectedAmount;
    expectedAmount = value.m_Msg.m_Amount;
    Env::Halt_if(amount.cmp(expectedAmount));

    // check public key
    idx += 96;
    PubKey pubKey;
    Env::Memcpy(&pubKey, idx, sizeof(pubKey));
    Env::Halt_if(Env::Memcmp(&pubKey, &value.m_Msg.m_Pk, sizeof(pubKey)));

    uint32_t key = 1;
    Bridge::InMsg tmp = value.m_Msg;
    tmp.m_Finalized = 0;
    Env::SaveVar_T(key, tmp);
}

export void Method_5(const Bridge::Finalized& /*value*/)
{
    uint32_t key = 1;
    Bridge::InMsg msg;
    Env::LoadVar_T(key, msg);

    msg.m_Finalized = 1;
    Env::SaveVar_T(key, msg);
}

export void Method_6(const Bridge::PushLocal& value)
{
    uint32_t size = value.m_MsgSize + sizeof(Bridge::LocalMsgHdr);
    auto* pMsg = (Bridge::LocalMsgHdr*)Env::StackAlloc(size);

    if (Env::get_CallDepth() > 1)
        Env::get_CallerCid(1, pMsg->m_ContractSender);
    else
        _POD_(pMsg->m_ContractSender).SetZero();

    _POD_(pMsg->m_ContractReceiver) = value.m_ContractReceiver;
    Env::Memcpy(pMsg + 1, &value + 1, value.m_MsgSize);

    uint32_t localPckgCounter = 0;
    Env::LoadVar_T(Bridge::kLocalPckgCounterKey, localPckgCounter);

    Bridge::LocalMsgHdr::Key msgKey;
    msgKey.m_PckgId_BE = ++localPckgCounter;
    msgKey.m_MsgId_BE = Utils::FromBE(uint32_t(0));
    Env::SaveVar(&msgKey, sizeof(msgKey), pMsg, size, KeyTag::Internal);

    Env::SaveVar_T(Bridge::kLocalPckgCounterKey, localPckgCounter);
}

export void Method_7(const Bridge::PushRemote& value)
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

    // save msg
    const uint8_t* msgBody = trieKey + value.m_TrieKeySize;
    uint32_t fullMsgSize = value.m_MsgSize + sizeof(Bridge::RemoteMsgHdr);
    auto* pMsg = (Bridge::RemoteMsgHdr*)Env::StackAlloc(fullMsgSize);

    _POD_(*pMsg) = value.m_MsgHdr;
    Env::Memcpy(pMsg + 1, msgBody, value.m_MsgSize);

    Bridge::RemoteMsgHdr::Key keyMsg;
    keyMsg.m_PckgId_BE = Utils::FromBE(value.m_PckgId);
    keyMsg.m_MsgId_BE = Utils::FromBE(value.m_MsgId);

    Env::SaveVar(&keyMsg, sizeof(keyMsg), pMsg, fullMsgSize, KeyTag::Internal);
}

export void Method_8(Bridge::ReadRemote& value)
{
    Bridge::RemoteMsgHdr::Key keyMsg;
    keyMsg.m_PckgId_BE = Utils::FromBE(value.m_PckgId);
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