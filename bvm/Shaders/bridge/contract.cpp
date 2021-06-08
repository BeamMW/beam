#include "../common.h"
#include "../Math.h"
#include "contract.h"
#include "../Eth.h"

// Method_0 - constructor, called once when the contract is deployed
export void Ctor(void*)
{
    const char meta[] = "testcoin11";
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

export void Method_4(const Bridge::ImportMessage& value)
{
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