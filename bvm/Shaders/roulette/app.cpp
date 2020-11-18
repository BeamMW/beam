#include "../common.h"
#include "contract.h"

#define Roulette_manager_create(macro)
#define Roulette_manager_view(macro)
#define Roulette_manager_view_params(macro) macro(ContractID, cid)
#define Roulette_manager_destroy(macro) macro(ContractID, cid)
#define Roulette_manager_view_bids(macro) macro(ContractID, cid)

#define Roulette_manager_start_round(macro) \
    macro(ContractID, cid) \
    macro(Height, hDuration)

#define Roulette_manager_view_bid(macro) \
    macro(ContractID, cid) \
    macro(PubKey, pubKey)

#define RouletteRole_manager(macro) \
    macro(manager, create) \
    macro(manager, destroy) \
    macro(manager, start_round) \
    macro(manager, view) \
    macro(manager, view_params) \
    macro(manager, view_bids) \
    macro(manager, view_bid)

#define Roulette_my_bid_view(macro) macro(ContractID, cid)
#define Roulette_my_bid_take(macro) macro(ContractID, cid)

#define Roulette_my_bid_make(macro) \
    macro(ContractID, cid) \
    macro(uint32_t, iSector)

#define RouletteRole_my_bid(macro) \
    macro(my_bid, view) \
    macro(my_bid, make) \
    macro(my_bid, take)

#define RouletteRoles_All(macro) \
    macro(manager) \
    macro(my_bid)

export void Method_0()
{
    // scheme
    Env::DocGroup root("");

    {   Env::DocGroup gr("roles");

#define THE_FIELD(type, name) Env::DocAddText(#name, #type);
#define THE_METHOD(role, name) { Env::DocGroup grMethod(#name);  Roulette_##role##_##name(THE_FIELD) }
#define THE_ROLE(name) { Env::DocGroup grRole(#name); RouletteRole_##name(THE_METHOD) }
        
        RouletteRoles_All(THE_ROLE)
#undef THE_ROLE
#undef THE_METHOD
#undef THE_FIELD
    }
}

#define THE_FIELD(type, name) const type& name,
#define ON_METHOD(role, name) void On_##role##_##name(Roulette_##role##_##name(THE_FIELD) int unused = 0)

void OnError(const char* sz)
{
    Env::DocAddText("error", sz);
}

#pragma pack (push, 1)

struct KeyPrefix
{
    ContractID m_Cid;
    uint8_t m_Tag;
};

struct KeyRaw
{
    KeyPrefix m_Prefix;
    PubKey m_Player;
};

struct KeyGlobal
{
    KeyPrefix m_Prefix;
    uint8_t m_Val = 0;
};

struct DealerKey
{
#define DEALER_SEED "roulette-dealyer-key"
    ShaderID m_SID;
    uint8_t m_pSeed[sizeof(DEALER_SEED)];

    DealerKey()
    {
        Env::Memcpy(&m_SID, &Roulette::s_SID, sizeof(Roulette::s_SID));
        Env::Memcpy(m_pSeed, DEALER_SEED, sizeof(DEALER_SEED));
    }

    void DerivePk(PubKey& pk) const
    {
        Env::DerivePk(pk, this, sizeof(*this));
    }
};

#pragma pack (pop)

struct StateInfoPlus
{
    bool m_RoundOver;
    bool m_isDealer;
    uint32_t m_iWinSector;
    Height m_hRoundEnd;

    const Roulette::State* Init(const ContractID& cid)
    {
        KeyGlobal k;
        k.m_Prefix.m_Cid = cid;
        k.m_Prefix.m_Tag = 0;

        Env::VarsEnum(&k, sizeof(k), &k, sizeof(k));

        const void* pK;
        const Roulette::State* pVal;

        uint32_t nKey, nVal;
        if (!Env::VarsMoveNext(&pK, &nKey, (const void**) &pVal, &nVal) || (sizeof(*pVal) != nVal))
        {
            OnError("failed to read");
            return nullptr;
        }

        DealerKey dk;
        PubKey pk;
        dk.DerivePk(pk);
        m_isDealer = !Env::Memcmp(&pk, &pVal->m_Dealer, sizeof(pk));

        m_hRoundEnd = pVal->m_hRoundEnd;
        m_RoundOver = m_hRoundEnd && (Env::get_Height() >= m_hRoundEnd);
        if (m_RoundOver)
        {
            m_iWinSector = pVal->DeriveWinSector();
        }

        return pVal;
    }

    const char* get_BidStatus(const Roulette::BidInfo& bi, Amount& nWin) const
    {
        nWin = 0;

        if (m_hRoundEnd != bi.m_hRoundEnd)
            return "not-actual";

        if (!m_RoundOver)
            return "in-progress";

        if (bi.m_iSector == m_iWinSector)
        {
            nWin = Roulette::State::s_PrizeSector;
            return "jack-pot";
        }

        if (!(1 & (bi.m_iSector ^ m_iWinSector)))
        {
            nWin = Roulette::State::s_PrizeParity;
            return "win";
        }

        return "lose";
    }
};

void EnumAndDump(const StateInfoPlus& sip)
{
    Env::DocArray gr("bids");

    while (true)
    {
        const KeyRaw* pRawKey;
        const Roulette::BidInfo* pVal;

        uint32_t nKey, nVal;
        if (!Env::VarsMoveNext((const void**) &pRawKey, &nKey, (const void**) &pVal, &nVal))
            break;

        if ((sizeof(*pRawKey) == nKey) && (sizeof(*pVal) == nVal))
        {
            Env::DocGroup gr("");

            Env::DocAddBlob_T("Player", pRawKey->m_Player);
            Env::DocAddNum("Sector", pVal->m_iSector);
            Env::DocAddNum("Round-end", pVal->m_hRoundEnd);

            Amount nWin;
            Env::DocAddText("Status", sip.get_BidStatus(*pVal, nWin));

            if (nWin)
                Env::DocAddNum("Prize", nWin);
        }
    }
}

void EnumBid(const PubKey& pubKey, const ContractID& cid)
{
    KeyRaw k;
    k.m_Prefix.m_Cid = cid;
    k.m_Prefix.m_Tag = 0;
    k.m_Player = pubKey;

    Env::VarsEnum(&k, sizeof(k), &k, sizeof(k));
}

void DumpBid(const PubKey& pubKey, const ContractID& cid)
{
    StateInfoPlus sip;
    if (!sip.Init(cid))
        return;

    EnumBid(pubKey, cid);
    EnumAndDump(sip);
}

ON_METHOD(manager, view)
{

#pragma pack (push, 1)
    struct Key {
        KeyPrefix m_Prefix;
        ContractID m_Cid;
    };
#pragma pack (pop)

    Key k0, k1;
    k0.m_Prefix.m_Cid = Roulette::s_SID;
    k0.m_Prefix.m_Tag = 0x10; // sid-cid tag
    k1.m_Prefix = k0.m_Prefix;

    Env::Memset(&k0.m_Cid, 0, sizeof(k0.m_Cid));
    Env::Memset(&k1.m_Cid, 0xff, sizeof(k1.m_Cid));

    Env::VarsEnum(&k0, sizeof(k0), &k1, sizeof(k1));

    Env::DocArray gr("Cids");

    while (true)
    {
        const Key* pKey;
        const void* pVal;
        uint32_t nKey, nVal;

        if (!Env::VarsMoveNext((const void**) &pKey, &nKey, &pVal, &nVal))
            break;

        if ((sizeof(Key) != nKey) || (1 != nVal))
            continue;

        Env::DocAddBlob_T("", pKey->m_Cid);
    }
}

static const Amount g_DepositCA = 100000000000ULL; // 1K beams

ON_METHOD(manager, create)
{
    Roulette::Params pars;

    DealerKey dk;
    dk.DerivePk(pars.m_Dealer);

    FundsChange fc;
    fc.m_Aid = 0;
    fc.m_Amount = g_DepositCA;
    fc.m_Consume = 1;

    Env::GenerateKernel(nullptr, pars.s_iMethod, &pars, sizeof(pars), &fc, 1, nullptr, 0, 2000000U);
}

ON_METHOD(manager, destroy)
{
    DealerKey dk;

    SigRequest sig;
    sig.m_pID = &dk;
    sig.m_nID = sizeof(dk);

    FundsChange fc;
    fc.m_Aid = 0;
    fc.m_Amount = g_DepositCA;
    fc.m_Consume = 0;

    Env::GenerateKernel(&cid, 1, nullptr, 0, &fc, 1, &sig, 1, 2000000U);
}

ON_METHOD(manager, start_round)
{
    if (!hDuration)
        return OnError("round duration must be nnz");

    DealerKey dk;

    SigRequest sig;
    sig.m_pID = &dk;
    sig.m_nID = sizeof(dk);

    Roulette::Restart arg;
    arg.m_dhRound = hDuration;

    Env::GenerateKernel(&cid, arg.s_iMethod, &arg, sizeof(arg), nullptr, 0, &sig, 1, 2000000U);
}

ON_METHOD(manager, view_params)
{
    StateInfoPlus sip;
    auto* pState = sip.Init(cid);
    if (!pState)
        return;

    Env::DocGroup gr("params");

    Env::DocAddBlob_T("Dealer", pState->m_Dealer);
    Env::DocAddText("IsDealer", sip.m_isDealer ? "yes" : "no");
    Env::DocAddNum("Round-end", pState->m_hRoundEnd);

    if (sip.m_RoundOver)
        Env::DocAddNum("WinSector", sip.m_iWinSector);
}


ON_METHOD(manager, view_bids)
{
    StateInfoPlus sip;
    if (!sip.Init(cid))
        return;

    KeyPrefix k0, k1;
    k0.m_Cid = cid;
    k0.m_Tag = 0;
    k1.m_Cid = cid;
    k1.m_Tag = 1;

    Env::VarsEnum(&k0, sizeof(k0), &k1, sizeof(k1)); // enum all internal contract vars
    EnumAndDump(sip);
}

ON_METHOD(manager, view_bid)
{
    DumpBid(pubKey, cid);
}

void DerivePlayerPk(PubKey& pubKey, const ContractID& cid)
{
    Env::DerivePk(pubKey, &cid, sizeof(cid));
}

ON_METHOD(my_bid, view)
{
    PubKey pubKey;
    DerivePlayerPk(pubKey, cid);
    DumpBid(pubKey, cid);
}

ON_METHOD(my_bid, make)
{
    Roulette::PlaceBid arg;
    DerivePlayerPk(arg.m_Player, cid);
    arg.m_iSector = iSector - 1;

    if (arg.m_iSector >= Roulette::State::s_Sectors)
        return OnError("sector is out of range");

    SigRequest sig;
    sig.m_pID = &cid;
    sig.m_nID = sizeof(cid);

    Env::GenerateKernel(&cid, arg.s_iMethod, &arg, sizeof(arg), nullptr, 0, &sig, 1, 1000000U);
}

ON_METHOD(my_bid, take)
{
    StateInfoPlus sip;
    auto* pState = sip.Init(cid);
    if (!pState)
        return;

    // pState is temporary, consequent vars enum will invalidate it.
    FundsChange fc;
    fc.m_Aid = pState->m_Aid;

    if (!sip.m_RoundOver)
        return OnError("round not finished");

    Roulette::Take arg;
    DerivePlayerPk(arg.m_Player, cid);

    EnumBid(arg.m_Player, cid);

    const KeyRaw* pRawKey;
    const Roulette::BidInfo* pVal;

    uint32_t nKey, nVal;
    if (!Env::VarsMoveNext((const void**) &pRawKey, &nKey, (const void**) &pVal, &nVal) ||
        (sizeof(*pRawKey) != nKey) ||
        (sizeof(*pVal) != nVal))
        return OnError("no bid");

    sip.get_BidStatus(*pVal, fc.m_Amount);

    if (!fc.m_Amount)
        return OnError("you lost");

    SigRequest sig;
    sig.m_pID = &cid;
    sig.m_nID = sizeof(cid);

    fc.m_Consume = 0;

    Env::GenerateKernel(&cid, arg.s_iMethod, &arg, sizeof(arg), &fc, 1, &sig, 1, 1000000U);
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

    const char* szErr = nullptr;

#define PAR_READ(type, name) type arg_##name; Env::DocGet(#name, arg_##name);
#define PAR_PASS(type, name) arg_##name,

#define THE_METHOD(role, name) \
        if (!Env::Strcmp(szAction, #name)) { \
            Roulette_##role##_##name(PAR_READ) \
            On_##role##_##name(Roulette_##role##_##name(PAR_PASS) 0); \
            return; \
        }

#define THE_ROLE(name) \
    if (!Env::Strcmp(szRole, #name)) { \
        RouletteRole_##name(THE_METHOD) \
        return OnError("invalid Action"); \
    }

    RouletteRoles_All(THE_ROLE)

#undef THE_ROLE
#undef THE_METHOD
#undef PAR_PASS
#undef PAR_READ

    OnError("unknown Role");
}

