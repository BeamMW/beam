#pragma once

namespace Roulette
{
    static const ShaderID s_SID = { 0x6a,0x8e,0x4e,0x05,0x30,0x07,0xa3,0xec,0xfa,0xc5,0xc1,0xd8,0x15,0xe2,0xce,0x1f,0x4f,0xfe,0xd4,0xae,0xdb,0x2c,0x79,0x1e,0xe3,0xaa,0x2d,0x20,0xe4,0xe5,0x45,0x07 };

#pragma pack (push, 1)

    struct Params {
        static const uint32_t s_iMethod = 0;
        PubKey m_Dealer; // authorized to start rounds
    };

    struct Spin {
        static const uint32_t s_iMethod = 2;
        uint32_t m_PlayingSectors = 0; // for tests, can lower num of sectors
    };

    struct BetsOff {
        static const uint32_t s_iMethod = 3;
    };

    struct Bid {
        static const uint32_t s_iMethod = 4;
        PubKey m_Player;
        uint32_t m_iSector;
    };

    struct Take {
        static const uint32_t s_iMethod = 5;
        PubKey m_Player;
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
