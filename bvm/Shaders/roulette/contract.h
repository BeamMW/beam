#pragma once

namespace Roulette
{
    static const ShaderID s_SID = { 0x8b,0x9c,0x0b,0xc3,0x71,0x09,0x1f,0xbc,0xa1,0x5b,0xf6,0x33,0x20,0xab,0xbf,0xa9,0x53,0x03,0xcc,0xcd,0x73,0xb4,0x2c,0x54,0x6f,0x3e,0xa4,0x7e,0x20,0x97,0x8d,0x5b };

#pragma pack (push, 1)

    struct Params {
        static const uint32_t s_iMethod = 0;
        PubKey m_Dealer; // authorized to start rounds

        template <bool bToShader>
        void Convert()
        {
        }
    };

    struct Restart {
        static const uint32_t s_iMethod = 2;
        Height m_dhRound;

        template <bool bToShader>
        void Convert()
        {
            ConvertOrd<bToShader>(m_dhRound);
        }
    };

    struct PlaceBid {
        static const uint32_t s_iMethod = 3;
        PubKey m_Player;
        uint32_t m_iSector;

        template <bool bToShader>
        void Convert()
        {
            ConvertOrd<bToShader>(m_iSector);
        }
    };

    struct Take {
        static const uint32_t s_iMethod = 4;
        PubKey m_Player;

        template <bool bToShader>
        void Convert()
        {
        }
    };

    struct State
    {
        static const uint32_t s_Sectors = 36;
        static const Amount s_Prize = 100000000U; // 1 jetton

        AssetID m_Aid;
        Height m_hRoundEnd;
        uint32_t m_iWinSector;
        uint32_t m_pBidders[s_Sectors];
        PubKey m_Dealer;
    };

    struct BidInfo
    {
        uint32_t m_iSector;
        Height m_hRoundEnd;
    };

#pragma pack (pop)

}
