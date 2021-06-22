#include "../common.h"
#include "../Math.h"
#include "contract.h"
#include "../Ethash.h"
#include "../Eth.h"

// Method_0 - constructor, called once when the contract is deployed
export void Ctor(void*)
{
    const char meta[] = "testcoin16";
    AssetID aid = Env::AssetCreate(meta, sizeof(meta) - 1);

    uint32_t key = 0;
    Env::SaveVar_T(key, aid);
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