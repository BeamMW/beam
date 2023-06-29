#include "../common.h"
#include "../app_common_impl.h"
#include "contract.h"


#define AssetConverter_deploy(macro)
#define AssetConverter_view_deployed(macro)
#define AssetConverter_destroy(macro) macro(ContractID, cid)
#define AssetConverter_pools_view(macro) macro(ContractID, cid)

#define AssetConverter_poolop(macro) \
    macro(ContractID, cid) \
    macro(AssetID, aidFrom) \
    macro(AssetID, aidTo)

#define AssetConverter_pool_view(macro) AssetConverter_poolop(macro)
#define AssetConverter_pool_create(macro) AssetConverter_poolop(macro)

#define AssetConverter_pool_deposit(macro) \
    AssetConverter_poolop(macro) \
    macro(Amount, amount)

#define AssetConverter_pool_convert(macro) \
    AssetConverter_poolop(macro) \
    macro(Amount, amount)


#define AssetConverterActions_All(macro) \
    macro(view_deployed) \
    macro(destroy) \
    macro(deploy) \
    macro(pools_view) \
    macro(pool_view) \
    macro(pool_create) \
    macro(pool_deposit) \
    macro(pool_convert)


namespace AssetConverter {

BEAM_EXPORT void Method_0()
{
    // scheme
    Env::DocGroup root("");

    {   Env::DocGroup gr("actions");

#define THE_FIELD(type, name) Env::DocAddText(#name, #type);
#define THE_ACTION(name) { Env::DocGroup gr(#name);  AssetConverter_##name(THE_FIELD) }
        
        AssetConverterActions_All(THE_ACTION)
#undef THE_ACTION
#undef THE_FIELD
    }
}

#define THE_FIELD(type, name) const type& name,
#define ON_METHOD(name) void On_##name(AssetConverter_##name(THE_FIELD) int unused = 0)

void OnError(const char* sz)
{
    Env::DocAddText("error", sz);
}


ON_METHOD(view_deployed)
{
    EnumAndDumpContracts(AssetConverter::s_SID);
}

ON_METHOD(deploy)
{
    Env::GenerateKernel(nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0, "Deploy AssetConverter contract", 0);
}

ON_METHOD(destroy)
{
    Env::GenerateKernel(&cid, 1, nullptr, 0, nullptr, 0, nullptr, 0, "destroy AssetConverter contract", 0);
}


struct PoolsWalker
{
    Env::VarReaderEx<true> m_R;
    Env::Key_T<Pool::Key> m_Key;
    Pool m_Pool;

    void Enum(const ContractID& cid)
    {
        _POD_(m_Key.m_Prefix.m_Cid) = cid;
        _POD_(m_Key.m_KeyInContract.m_ID).SetZero();

        Env::Key_T<Pool::Key> key2;
        _POD_(key2.m_Prefix.m_Cid) = cid;
        _POD_(key2.m_KeyInContract.m_ID).SetObject(0xff);

        m_R.Enum_T(m_Key, key2);
    }

    void Enum(const ContractID& cid, AssetID aidFrom, AssetID aidTo)
    {
        _POD_(m_Key.m_Prefix.m_Cid) = cid;
        m_Key.m_KeyInContract.m_ID.m_AidFrom = aidFrom;
        m_Key.m_KeyInContract.m_ID.m_AidTo = aidTo;

        m_R.Enum_T(m_Key, m_Key);
    }

    bool Move()
    {
        return m_R.MoveNext_T(m_Key, m_Pool);
    }

    bool MoveMustExist()
    {
        if (Move())
            return true;
        OnError("no such a pool");
        return false;
    }

    static void PrintAmount(const char* szName, const char* szNameHi, const AmountBig& x)
    {
        Env::DocAddNum(szName, x.m_Lo);
        if (x.m_Hi)
            Env::DocAddNum(szNameHi, x.m_Hi);
    }

    void PrintPool() const
    {
        PrintAmount("remaining", "remaining-hi", m_Pool.m_ToRemaining);
        PrintAmount("converted", "converted-hi", m_Pool.m_FromLocked);
    }

    void PrintKey() const
    {
        Env::DocAddNum("aidFrom", m_Key.m_KeyInContract.m_ID.m_AidFrom);
        Env::DocAddNum("aidTo", m_Key.m_KeyInContract.m_ID.m_AidTo);
    }

};

ON_METHOD(pools_view)
{
    Env::DocArray gr("res");

    PoolsWalker pw;
    for (pw.Enum(cid); pw.Move(); )
    {
        Env::DocGroup gr1("");

        pw.PrintKey();
        pw.PrintPool();
    }
}

ON_METHOD(pool_view)
{
    PoolsWalker pw;
    pw.Enum(cid, aidFrom, aidTo);
    if (pw.MoveMustExist())
    {
        Env::DocGroup gr("res");
        pw.PrintPool();
    }
}

ON_METHOD(pool_create)
{
    PoolsWalker pw;
    pw.Enum(cid, aidFrom, aidTo);
    if (pw.Move())
        return OnError("pool already exists");

    Method::PoolCreate arg;
    arg.m_Pid = pw.m_Key.m_KeyInContract.m_ID;

    Env::GenerateKernel(&cid, arg.s_iMethod, &arg, sizeof(arg), nullptr, 0, nullptr, 0, "AssetConverter create pool", 0);
}


ON_METHOD(pool_deposit)
{
    if (!amount)
        return OnError("amount not specified");

    PoolsWalker pw;
    pw.Enum(cid, aidFrom, aidTo);
    if (!pw.MoveMustExist())
        return;


    Method::PoolDeposit arg;
    arg.m_Pid = pw.m_Key.m_KeyInContract.m_ID;
    arg.m_Amount = amount;

    FundsChange fc;
    fc.m_Aid = aidTo;
    fc.m_Amount = amount;
    fc.m_Consume = 1;

    Env::GenerateKernel(&cid, arg.s_iMethod, &arg, sizeof(arg), &fc, 1, nullptr, 0, "AssetConverter deposit", 0);
}

ON_METHOD(pool_convert)
{
    if (!amount)
        return OnError("amount not specified");

    PoolsWalker pw;
    pw.Enum(cid, aidFrom, aidTo);
    if (!pw.MoveMustExist())
        return;


    Method::PoolConvert arg;
    arg.m_Pid = pw.m_Key.m_KeyInContract.m_ID;
    arg.m_Amount = amount;

    FundsChange pFc[2];
    pFc[0].m_Aid = aidTo;
    pFc[0].m_Amount = amount;
    pFc[0].m_Consume = 0;
    pFc[1].m_Aid = aidFrom;
    pFc[1].m_Amount = amount;
    pFc[1].m_Consume = 1;

    Env::GenerateKernel(&cid, arg.s_iMethod, &arg, sizeof(arg), pFc, _countof(pFc), nullptr, 0, "AssetConverter deposit", 0);
}

#undef ON_METHOD
#undef THE_FIELD

BEAM_EXPORT void Method_1() 
{
    Env::DocGroup root("");

    char szAction[0x20];

    if (!Env::DocGetText("action", szAction, sizeof(szAction)))
        return OnError("Action not specified");

#define PAR_READ(type, name) type arg_##name; Env::DocGet(#name, arg_##name);
#define PAR_PASS(type, name) arg_##name,

#define THE_METHOD(name) \
        static_assert(sizeof(szAction) >= sizeof(#name)); \
        if (!Env::Strcmp(szAction, #name)) { \
            AssetConverter_##name(PAR_READ) \
            On_##name(AssetConverter_##name(PAR_PASS) 0); \
            return; \
        }

    AssetConverterActions_All(THE_METHOD)

#undef THE_METHOD
#undef PAR_PASS
#undef PAR_READ

    OnError("unknown action");
}

} // namespace AssetConverter
