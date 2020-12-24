#pragma once

namespace Roulette
{
    static const ShaderID s_SID = { 0xa8,0xca,0xb7,0xec,0x54,0xd6,0xb0,0x70,0x79,0x77,0x4c,0x53,0x48,0x64,0x90,0x2a,0xf0,0x86,0xd4,0x92,0xba,0x83,0xe5,0xbf,0x8a,0xa7,0xdd,0x0a,0x32,0xe4,0x58,0x83, };

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
