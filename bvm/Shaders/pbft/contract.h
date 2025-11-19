#pragma once
#include "../Math.h"

namespace PBFT
{
    static const ShaderID s_SID = {
        0xc5,0x57,0xdb,0xf6,0xba,0x78,0xfb,0x9f,0x7d,0xbe,0xb4,0x32,0x74,0x9b,0x73,0x55,0x7c,0x12,0x8b,0x96,0x26,0x88,0xbe,0x13,0x33,0x9d,0x01,0x4e,0x3b,0xc3,0x35,0x5c
    };

#pragma pack (push, 1)

    struct Settings
    {
        AssetID m_aidStake;
    };

    typedef HashValue Address;

    struct ValidatorInit {
        Address m_Address;
        Amount m_Stake;
    };

    namespace State
    {
        struct Tag
        {
            static const uint8_t s_Global = 1;
            static const uint8_t s_Validator = 2;
        };

        typedef MultiPrecision::UInt<5> SummaType;

        struct Global
        {
            Settings m_Settings;

            struct Pool
            {
                SummaType m_Summa;
                uint64_t m_WeightNonJailed;

                Amount m_RewardRemaining;
                Amount m_RewardPending; // this value is added explicitly by the node, bypassing the appropriate method call

            } m_Pool;
        };

        struct Validator
        {
            struct Key
            {
                uint8_t m_Tag = Tag::s_Validator;
                Address m_Address;
            };

            struct Flags
            {
                static const uint8_t Jail = 1;
            };

            uint64_t m_Weight;
            uint8_t m_Flags;
        };


    } // namespace State

    namespace Method
    {
        struct Create
        {
            static const uint32_t s_iMethod = 0;
            Settings m_Settings;
            uint32_t m_Validators;
            // followed by array of ValidatorInit structs
        };

        struct ValidatorStatusUpdate
        {
            static const uint32_t s_iMethod = 2;
            Address m_Address;
            uint8_t m_Flags;
        };
    }

#pragma pack (pop)

} // namespace PBFT
