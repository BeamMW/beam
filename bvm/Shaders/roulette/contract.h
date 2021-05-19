#pragma once

namespace Roulette
{
    static const ShaderID s_SID = { 0x63,0xd2,0x06,0xa1,0xba,0x92,0x57,0x6d,0x33,0x69,0xb4,0x0e,0x5c,0x37,0x58,0xda,0x90,0x2f,0xd7,0xad,0x67,0xc4,0x61,0xcf,0x03,0x27,0xfe,0xd0,0xc5,0xb5,0xe4,0x79 };

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
