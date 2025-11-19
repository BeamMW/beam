#pragma once
#include "../Math.h"

namespace PBFT
{
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

        struct AddReward
        {
            static const uint32_t s_iMethod = 3;
            Amount m_Amount;
        };
    }

#pragma pack (pop)

} // namespace PBFT
