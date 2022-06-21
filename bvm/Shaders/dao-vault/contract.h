#pragma once
#include "../upgradable3/contract.h"

namespace DaoVault
{
    static const ShaderID s_pSID[] = {
        { 0x3d,0xfd,0xec,0x7c,0x86,0xaf,0x9b,0x22,0xce,0x7c,0xf4,0xed,0xe8,0xc8,0x1c,0x24,0x20,0xf3,0x69,0xee,0x71,0xd8,0x41,0x58,0x65,0x22,0x0b,0x8b,0x28,0xbb,0x66,0x7a },
    };

#pragma pack (push, 1)

    struct Tags
    {
    };

    namespace Method
    {
        struct Create
        {
            static const uint32_t s_iMethod = 0; // Ctor
            Upgradable3::Settings m_Upgradable;
        };

        struct Deposit
        {
            static const uint32_t s_iMethod = 3;
            AssetID m_Aid;
            Amount m_Amount;
        };

        struct Withdraw
        {
            static const uint32_t s_iMethod = 4;
            AssetID m_Aid;
            Amount m_Amount;
            uint32_t m_ApproveMask;
        };
    }
#pragma pack (pop)

} // namespace DaoVault
