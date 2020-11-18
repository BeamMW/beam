#pragma once

namespace Roulette
{
    static const ShaderID s_SID = { 0x5c,0x95,0x9d,0x1e,0x3b,0xaa,0x5a,0xda,0xaa,0x97,0x79,0x49,0x9e,0x03,0x93,0xe5,0x33,0xc6,0xdd,0x67,0xc5,0xa6,0xbe,0x4c,0x43,0x12,0xdb,0x55,0x74,0x1b,0xbf,0xb6 };

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
        uint32_t m_pBidders[s_Sectors];
        PubKey m_Dealer;

        uint32_t DeriveWinSector() const
        {
            BlockHeader hdr;
            hdr.m_Height = m_hRoundEnd;
            Env::get_Hdr(hdr); // would fail if this height isn't reached yet

            uint64_t val;
            Env::Memcpy(&val, &hdr.m_Hash, sizeof(val));
            return static_cast<uint32_t>(val % Roulette::State::s_Sectors);
        }
    };

    struct BidInfo
    {
        uint32_t m_iSector;
        Height m_hRoundEnd;
    };

#pragma pack (pop)

}
