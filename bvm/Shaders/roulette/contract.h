#pragma once

namespace Roulette
{
    static const ShaderID s_SID = { 0xf3,0x7b,0x07,0x25,0xc3,0x0c,0x29,0x24,0xe2,0x05,0x4a,0x54,0x78,0xd2,0xe2,0xf0,0x4d,0xfc,0x1a,0xf5,0xa8,0x19,0xeb,0x91,0x1f,0xdb,0x9b,0xff,0x1c,0x39,0x6b,0xca };

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
        uint32_t m_PlayingSectors = 0; // for tests, can lower num of sectors

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
        static const Amount s_PrizeParity = 100000000U; // 1 jetton
        static const Amount s_PrizeSector = s_PrizeParity * 25;

        AssetID m_Aid;
        Height m_hRoundEnd;
        uint32_t m_PlayingSectors;
        PubKey m_Dealer;

        uint32_t DeriveWinSector() const
        {
            BlockHeader hdr;
            hdr.m_Height = m_hRoundEnd;
            Env::get_Hdr(hdr); // would fail if this height isn't reached yet

            uint64_t val;
            Env::Memcpy(&val, &hdr.m_Hash, sizeof(val));
            return static_cast<uint32_t>(val % m_PlayingSectors);
        }
    };

    struct BidInfo
    {
        uint32_t m_iSector;
        Height m_hRoundEnd;
    };

#pragma pack (pop)

}
