#include "../common.h"
#include "../app_common_impl.h"
#include "../vault_anon/app_impl.h"



#define GalDrop_view_keys(macro) macro(uint32_t, nftId)
#define GalDrop_view_airdrops(macro) macro(ContractID, cidVaultAnon)
#define GalDrop_claim_airdrops(macro) macro(ContractID, cidVaultAnon)

#define GalDropActions_All(macro) \
    macro(view_keys) \
    macro(view_airdrops) \
    macro(claim_airdrops)


BEAM_EXPORT void Method_0()
{
    // scheme
    Env::DocGroup root("");

    {   Env::DocGroup gr("actions");

#define THE_FIELD(type, name) Env::DocAddText(#name, #type);
#define THE_ACTION(name) { Env::DocGroup gr(#name);  GalDrop_##name(THE_FIELD) }
        
        GalDropActions_All(THE_ACTION)
#undef THE_ACTION
#undef THE_FIELD
    }
}

#define THE_FIELD(type, name) const type& name,
#define ON_METHOD(name) void On_##name(GalDrop_##name(THE_FIELD) int unused = 0)

void OnError(const char* sz)
{
    Env::DocAddText("error", sz);
}



static const ContractID g_CidGallery1 = { 0xc0,0x2d,0xbd,0x60,0x3e,0xc8,0x92,0x51,0x77,0xcd,0x3f,0xd5,0xe2,0x68,0x48,0xe3,0xb8,0x2c,0xd3,0x69,0x1b,0x69,0xed,0xd8,0xad,0xd4,0xcc,0xfd,0x40,0x81,0xae,0x9e };
static const ContractID g_CidGallery2 = { 0x43,0x90,0xf7,0x5c,0x95,0xf6,0x0e,0x6c,0x06,0x9f,0xb2,0x5a,0x4c,0x21,0x0d,0x9b,0x3b,0x8a,0x79,0x80,0x4b,0x1e,0x5d,0xdb,0xa4,0x31,0x96,0x5e,0xa8,0xeb,0x4c,0xd9 };




namespace KeyMaterial
{
#pragma pack (push, 1)
    const char g_szOwner[] = "Gallery-key-owner";

    struct Owner
    {
        ContractID m_Cid;
        uint32_t m_ID;
        uint8_t m_pSeed[sizeof(g_szOwner) - sizeof(char)];

        Owner()
        {
            Env::Memcpy(m_pSeed, g_szOwner, sizeof(m_pSeed));
            m_ID = 0;
        }

        void SetCid(const ContractID& cid)
        {
            _POD_(m_Cid) = cid;
        }
    };
#pragma pack (pop)
}

struct PayoutReader
{
    VaultAnon::AccountReader m_Ar;

    Utils::Vector<uint8_t,0x1000> m_Buf;
    uint32_t m_Pos = 0;

    void WriteBuf(const void* p, uint32_t n)
    {
        m_Buf.Prepare(m_Buf.m_Count + n);
        Env::Memcpy(m_Buf.m_p + m_Buf.m_Count, p, n);
        m_Buf.m_Count += n;
    }

    void ReadBuf(void* p, uint32_t n)
    {
        assert(m_Pos + n <= m_Buf.m_Count);
        Env::Memcpy(p, m_Buf.m_p + m_Pos, n);
        m_Pos += n;
    }

#pragma pack (push, 1)
    struct Record
    {
        Amount m_Amount;
        uint32_t m_SizeCustom;
        VaultAnon::Account::KeyBase m_Key;
        // followed by custom
    };

#pragma pack (pop)

    PayoutReader(const ContractID& cid)
        :m_Ar(cid)
    {
    }

    void ReadAll()
    {
        Env::Key_T<VaultAnon::Account::Key0> k0;
        _POD_(k0.m_Prefix.m_Cid) = m_Ar.m_Key.m_Prefix.m_Cid;
        _POD_(k0.m_KeyInContract).SetZero();
        k0.m_KeyInContract.m_Tag = VaultAnon::Tags::s_Account;

        for (Env::VarReader r(k0, m_Ar.m_Key); m_Ar.MoveNext(r); )
        {
            if (m_Ar.m_SizeCustom < sizeof(PubKey))
                continue;

            WriteBuf(&m_Ar.m_Amount, sizeof(m_Ar.m_Amount));
            WriteBuf(&m_Ar.m_SizeCustom, sizeof(m_Ar.m_SizeCustom));
            WriteBuf(&Cast::Down<VaultAnon::Account::KeyBase>(m_Ar.m_Key.m_KeyInContract), sizeof(VaultAnon::Account::KeyBase) + m_Ar.m_SizeCustom);
        }
    }

    bool MoveNext()
    {
        if (m_Pos >= m_Buf.m_Count)
            return false;

        ReadBuf(&m_Ar.m_Amount, sizeof(m_Ar.m_Amount));
        ReadBuf(&m_Ar.m_SizeCustom, sizeof(m_Ar.m_SizeCustom));
        ReadBuf(&Cast::Down<VaultAnon::Account::KeyBase>(m_Ar.m_Key.m_KeyInContract), sizeof(VaultAnon::Account::KeyBase) + m_Ar.m_SizeCustom);

        return true;
    }

    bool ProcessOneKey(const Env::KeyID& kid, VaultAnon::WalkerAccounts& wlk)
    {
        VaultAnon::AnonScanner as(kid);

        for (m_Pos = 0; MoveNext(); )
        {
            if (!m_Ar.Recognize(as))
                continue;

            if (!wlk.OnAccount(m_Ar, &as))
                return false;
        }

        return true;
    }

    bool ProcessAllKeys(VaultAnon::WalkerAccounts& wlk)
    {
        struct Range {
            uint16_t id0;
            uint16_t id1;
        };

        static const Range s_pRange[] = {
            { 0, 0 }, // i.e. artist key
            { 329, 338 },
            { 443, 453 },
            { 531, 540 },
            { 615, 624 },
            { 698, 707 },
            { 849, 858 },
            { 898, 898 },
            { 982, 991 },
            { 1083, 1102 },
            { 1201, 1210 },
            { 1275, 1284 },
            { 1352, 1361 },
            { 1449, 1467 },
            { 1580, 1589 },
            { 1771, 1780 },
            { 1910, 1917 },
            { 1944, 1947 },
            { 2008, 2017 },
            { 2195, 2204 },
            { 2270, 2279 },
            { 2330, 2339 },
            { 2360, 2373 },
            { 2400, 2409 },
            { 2487, 2496 },
            { 2517, 2526 },
            { 2557, 2566 },
            { 2602, 2611 },
            { 2652, 2661 },
            { 2757, 2766 },
            { 2815, 2826 },
            { 2855, 2865 },
            { 2973, 2994 },
            { 3046, 3055 },
            { 3106, 3115 },
            { 3155, 3164 },
            // ver1 starts here
            { 3218, 3227 },
            { 3351, 3360 },
            { 3490, 3499 },
            { 3570, 3579 },
            { 3650, 3659 },
            { 3762, 3771 },
            { 3881, 3890 },
            { 3914, 3923 },
            { 3953, 3962 },
            { 3976, 3980 },
        };

        KeyMaterial::Owner km;

        Env::KeyID kid(&km, sizeof(km));

        for (uint32_t iR = 0; iR < _countof(s_pRange); iR++)
        {
            const Range& r = s_pRange[iR];

            for (uint32_t nftId = r.id0; nftId <= r.id1; nftId++)
            {
                km.m_ID = Utils::FromBE(nftId);

                km.SetCid(g_CidGallery2);
                if (!ProcessOneKey(kid, wlk))
                    return false;

                if (nftId < 3200)
                {
                    km.SetCid(g_CidGallery1);
                    if (!ProcessOneKey(kid, wlk))
                        return false;
                }
            }
        }

        return true;
    }
};

ON_METHOD(view_keys)
{
    Env::DocGroup gr("res");

    KeyMaterial::Owner km;

    km.m_ID = Utils::FromBE(nftId);
    km.SetCid(g_CidGallery2);

    Env::KeyID kid(&km, sizeof(km));
    PubKey pk;
    kid.get_Pk(pk);
    Env::DocAddBlob_T("pk1", pk);


    km.SetCid(g_CidGallery1);
    kid.get_Pk(pk);
    Env::DocAddBlob_T("pk0", pk);
}

ON_METHOD(view_airdrops)
{
    struct MyAccountsPrinter
        :public VaultAnon::WalkerAccounts_Print
    {
        void PrintMsg(bool bIsAnon, const uint8_t* pMsg, uint32_t nMsg) override
        {
            if (nMsg)
            {
                char szTxt[VaultAnon::s_MaxMsgSize + 1];
                Env::Memcpy(szTxt, pMsg, nMsg);
                szTxt[nMsg] = 0;
                Env::DocAddText("message", szTxt);
            }
        }

    } wlk;

    Env::DocGroup gr("res");


    PayoutReader pr(cidVaultAnon);
    pr.ReadAll();
    pr.ProcessAllKeys(wlk);
}

ON_METHOD(claim_airdrops)
{
    struct MyWalker
        :public VaultAnon::WalkerAccounts
    {
        uint32_t m_Remaining;

        bool OnAccount(VaultAnon::AccountReader& ar, const VaultAnon::AnonScanner* pAnon) override
        {
            assert(pAnon);
            VaultAnon::OnUser_receive_internal_anon(ar, *pAnon, pAnon->m_Kid);

            return (--m_Remaining) > 0;
        }
    };

    MyWalker wlk;
    wlk.m_Remaining = 100;

    PayoutReader pr(cidVaultAnon);
    pr.ReadAll();
    pr.ProcessAllKeys(wlk);
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
            GalDrop_##name(PAR_READ) \
            On_##name(GalDrop_##name(PAR_PASS) 0); \
            return; \
        }

    GalDropActions_All(THE_METHOD)

#undef THE_METHOD
#undef PAR_PASS
#undef PAR_READ

    OnError("unknown action");
}
