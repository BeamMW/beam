#pragma once

namespace Roulette
{
    static const ShaderID s_SID = { 0x30,0xa2,0x39,0x18,0x05,0xcf,0xe9,0xe1,0x39,0x9a,0x18,0xb7,0xd9,0x3d,0xb1,0xcd,0xcf,0xfb,0x39,0xaa,0x7c,0xa1,0x0e,0x82,0x48,0x96,0xcf,0x05,0x5c,0xe1,0x01,0xed };

#pragma pack (push, 1)

    struct Params {
        static const uint32_t s_iMethod = 0;
        PubKey m_Dealer; // authorized to start rounds

        template <bool bToShader>
        void Convert()
        {
        }
    };

    struct Spin {
        static const uint32_t s_iMethod = 2;
        uint32_t m_PlayingSectors = 0; // for tests, can lower num of sectors

        template <bool bToShader>
        void Convert()
        {
            ConvertOrd<bToShader>(m_PlayingSectors);
        }
    };

    struct BetsOff {
        static const uint32_t s_iMethod = 3;

        template <bool bToShader>
        void Convert()
        {
        }
    };

    struct Bid {
        static const uint32_t s_iMethod = 4;
        PubKey m_Player;
        uint32_t m_iSector;

        template <bool bToShader>
        void Convert()
        {
            ConvertOrd<bToShader>(m_iSector);
        }
    };

    struct Take {
        static const uint32_t s_iMethod = 5;
        PubKey m_Player;

        template <bool bToShader>
        void Convert()
        {
        }
    };

    struct State
    {
        static const uint32_t s_Sectors = 37;
        static const Amount s_PrizeParity = 100000000U; // 1 jetton
        static const Amount s_PrizeSector = s_PrizeParity * 25;

        AssetID m_Aid;
        uint32_t m_iRound;
        uint32_t m_PlayingSectors;
        uint32_t m_iWinner;
        PubKey m_Dealer;
    };

    struct BidInfo
    {
        uint32_t m_iSector;
        uint32_t m_iRound;
    };

#pragma pack (pop)

}
