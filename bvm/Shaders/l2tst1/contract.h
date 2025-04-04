#pragma once
#include "../upgradable3/contract.h"

namespace L2Tst1
{
    static const ShaderID s_pSID[] = {
        { 0xc7,0xe0,0x02,0x18,0x91,0x04,0x54,0x5f,0x56,0xe6,0x0e,0x46,0x1b,0xbe,0xa6,0xb0,0x90,0xd6,0x6f,0x3e,0x69,0xfa,0x45,0x0a,0x25,0x5d,0xf7,0x13,0x9d,0xad, 0x3d,0x3c },
    };

#pragma pack (push, 1)

    struct Tags
    {
        static const uint8_t s_State = 0;
        static const uint8_t s_User = 1;
    };

    struct Settings
    {
        AssetID m_aidStaking; // would be BeamX on mainnet L2
        AssetID m_aidLiquidity; // would be Beam on maiinet L2
        Height m_hPreEnd;
    };

    struct State
    {
        Settings m_Settings;
        // no more fields so far
    };


    struct User
    {
        struct Key {
            uint8_t m_Tag = Tags::s_User;
            PubKey m_pk;
        };

        Amount m_Stake;
    };

    namespace Method
    {
        struct Create
        {
            static const uint32_t s_iMethod = 0;

            Upgradable3::Settings m_Upgradable;
            Settings m_Settings;
        };

        struct UserStake
        {
            static const uint32_t s_iMethod = 3;

            PubKey m_pkUser;
            Amount m_Amount;
        };
    }

#pragma pack (pop)

} // namespace L2Tst1
