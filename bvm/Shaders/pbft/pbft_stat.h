#pragma once
#include "i_pbft.h"

// The contract extends the PBFT logic, with static validator set

namespace PBFT_STAT
{
#pragma pack (push, 1)

    static const ShaderID s_SID = { 0xc3,0xc3,0x9d,0xa0,0x53,0x9a,0x53,0xa4,0xa4,0x6c,0x4f,0x81,0xad,0x84,0xee,0x58,0xcc,0x88,0x35,0x44,0xcd,0xcd,0x6c,0x96,0x11,0x65,0x41,0xf2,0x47,0x65,0x55,0x79 };

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
