#pragma once
#include "i_pbft.h"

// The contract extends the PBFT logic, with static validator set

namespace PBFT_STAT
{
#pragma pack (push, 1)

    static const ShaderID s_SID = { 0x4b,0x4a,0x52,0xb6,0x8f,0x94,0xab,0x1f,0x56,0xbd,0xb4,0x0a,0xdf,0x53,0x18,0x3a,0xfc,0xb7,0xbb,0x51,0xe9,0xe3,0x1c,0x06,0x63,0xe0,0x88,0xe6,0xc2,0x68,0x1c,0xf4 };

    namespace Method
    {
        struct Create
        {
            static const uint32_t s_iMethod = 0;

            struct ValidatorInit {
                I_PBFT::Address m_Address;
                uint64_t m_Weight;
            };

            uint32_t m_Count;
            
            // followed by array of ValidatorInit
            ValidatorInit* get_VI() const { return (ValidatorInit*) (this + 1); }
        };
    }

#pragma pack (pop)

} // namespace PBFT_STAT
