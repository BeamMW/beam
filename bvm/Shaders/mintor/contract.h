#pragma once

namespace Mintor
{
    static const ShaderID s_SID = { 0xaf,0x0f,0x63,0x5c,0x58,0xf6,0x97,0x9f,0x04,0xff,0x21,0x5a,0x38,0x53,0x71,0xef,0x81,0xc2,0x12,0x1b,0x01,0x7e,0x4a,0x41,0x89,0x20,0x31,0xc4,0x53,0x0f,0x93,0x6e };
    static const ShaderID s_CID = { 0x98,0x1d,0x15,0x5a,0xbd,0xa3,0x65,0x3c,0x98,0x32,0x69,0x87,0x11,0xfb,0xa8,0x17,0x82,0x17,0x3c,0x71,0x19,0x34,0x00,0x81,0x4d,0xae,0xb4,0x1e,0x3a,0x15,0x65,0x0d };

#pragma pack (push, 1)

    struct AmountBig
    {
        Amount m_Lo;
        Amount m_Hi;

        void operator += (Amount val)
        {
            m_Lo += val;
            if (m_Lo < val)
                Env::Halt_if(!++m_Hi);
        }

        void operator -= (Amount val)
        {
            if (m_Lo < val)
                Env::Halt_if(!m_Hi--);
            m_Lo -= val;
        }
    };

    struct Tags
    {
        static const uint8_t s_Global = 0;
        // don't use tag=1 for multiple data entries, it's used by Upgradable2
        static const uint8_t s_Token = 2;
        static const uint8_t s_User = 3;
    };

    struct PubKeyFlag
    {
        static const uint8_t s_Cid = 2; // pk.X == cid
        static const uint8_t s_CA = 3; // transfer directly via allocated CA
    };

    struct Token
    {
        typedef uint32_t ID;

        struct Key {
            uint8_t m_Tag = Tags::s_Token;
            ID m_ID;
        };

        AmountBig m_Mint;
        AmountBig m_Limit;
        PubKey m_pkOwner;
        AssetID m_Aid;
    };

    struct User
    {
        struct Key {
            uint8_t m_Tag = Tags::s_User;
            Token::ID m_Tid;
            PubKey m_pk;
        };
    };

    struct Global
    {
        Token::ID m_Tokens;
    };

    namespace Method
    {
        struct Base {
            Token::ID m_Tid;
        };

        struct BaseWithUser :public Base {
            PubKey m_pkUser;
        };

        struct View :public Base
        {
            static const uint32_t s_iMethod = 2;

            Token m_Result;
        };

        struct ViewUser :public BaseWithUser
        {
            static const uint32_t s_iMethod = 3;

            Amount m_Result;
        };

        struct Create :public BaseWithUser
        {
            static const uint32_t s_iMethod = 4;

            AmountBig m_Limit;
        };

        struct Mint :public BaseWithUser
        {
            static const uint32_t s_iMethod = 5;

            Amount m_Value;
            uint8_t m_Mint; // or burn.
        };

        struct Transfer :public BaseWithUser
        {
            static const uint32_t s_iMethod = 6;

            Amount m_Value;
            PubKey m_pkDst;
        };

        struct CreateCA :public Base
        {
            static const uint32_t s_iMethod = 7;
        };
    }
#pragma pack (pop)

} // namespace Mintor
