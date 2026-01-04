#pragma once
#include "../common.h"

namespace I_PBFT
{
#pragma pack (push, 1)

    typedef HashValue Address;

    // The contract implements the following logic:
    //
    // 1. At each block the node needs the current validator set. The info node needs is:
    //  - Validator address
    //  - Weight
    //  - isJailed flag
    //
    // based on this, the node implements the PBFT logic. Those are reflected directly by the contract variables, and mirrored in the node memory. All other things are implementation-specific
    //
    // 2. In addition the node interacts with the contract (in terms of tx kernels) to call those methods:
    //  - AddReward, for each block to collect the fees. Theoretically consensus parameters may include more rewards for the validators
    //  - ValidatorStatusUpdate, this is to trigger validator Jail/Unjail, or Slashing
    //
    // 3. The rest functionality is intended for users, and can be customized in implementations.

    namespace State
    {
        struct Tag
        {
            static const uint8_t s_Validator = 2;
        };

        struct Validator
        {
            struct Key
            {
                uint8_t m_Tag = Tag::s_Validator;
                Address m_Address;
            };

            enum class Status : uint8_t {
                Active = 0,
                Jailed = 1, // Won't receive reward, can't be a leader. But preserves the voting power, and votes normally
                Suspended = 2, // Looses voting power. Temporary in case of Slash, or Permanent if Tombed
                Tombed = 3, // disabled permanently. Either by the validator itself, or by the validators
                Slash = 4, //  Slash, not a permanent state
            };

            // this is the part of interest to the node.
            uint64_t m_Weight;
            Status m_Status;
            uint8_t m_NumSlashed;
            Height m_hSuspend; //  when was suspended
        };

    } // namespace State

    namespace Events
    {
        struct Tag
        {
            static const uint8_t s_Slash = 1;
        };

        struct Slash
        {
            struct Key {
                uint8_t m_Tag = Tag::s_Slash;
            };

            Address m_Validator;
            Amount m_StakeBurned;
        };
    }

    namespace Method
    {
        struct ValidatorStatusUpdate
        {
            static const uint32_t s_iMethod = 3;
            Address m_Address;
            State::Validator::Status m_Status;
        };

        struct AddReward
        {
            static const uint32_t s_iMethod = 4;
            Amount m_Amount;
        };
    }

#pragma pack (pop)

} // namespace PBFT
