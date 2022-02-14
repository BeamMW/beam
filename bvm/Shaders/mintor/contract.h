#pragma once

namespace Mintor
{
    static const ShaderID s_SID = { 0x31,0x94,0x69,0xe7,0x40,0x8c,0x27,0x92,0x9b,0x30,0x41,0x70,0xac,0x00,0xde,0x37,0xd3,0x0a,0x1c,0xfe,0x2e,0xfe,0xd1,0x7e,0xeb,0xa9,0xb3,0x6f,0xb2,0x65,0x43,0x43 };

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

    }
#pragma pack (pop)

} // namespace Mintor
