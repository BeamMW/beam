#include "../common.h"
#include "../Math.h"
#include "contract.h"
#include "../Ethash.h"
#include "../Eth.h"

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

    // save msg
    const uint8_t* msgBody = trieKey + value.m_TrieKeySize;
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