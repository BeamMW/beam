#pragma once
#include "../Math.h"
#include "../Float.h"

namespace PBFT
{
#pragma pack (push, 1)

    struct Settings
    {
        AssetID m_aidStake;
        uint32_t m_hUnbondLock;
    };

    typedef HashValue Address;

    struct ValidatorInit {
        Address m_Address;
        Amount m_Stake;
        PubKey m_Delegator;
    };

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
    //  - AddReward, for each block to collect the fees. Theoreticall consensus parameters may include more rewards fro the validators
    //  - ValidatorStatusUpdate, this is to trigger validator Jail/Unjail, or Slashing
    //
    // 3. The rest functionality is intended for users. This is the intended behavior:
    //  - Each user can delegate its stake to a selected validator, or run its own validator
    //  - The reward is distributed among active (non-jailed) validators proportional to their statke
    //      - For loose behavior validators can be jailed. Such validators don't  get the reward, until unjailed (once their behavoir becomes responsive)
    //      - For gross violation a validator can be Slashed. Part of its stake is reduced and burned.
    //  - Each delegator gets a reward proportional to its stake, after validator commission subtraction
    //      - The reward once earned can't be taken back (i.e. slashed), and the delegator can withdraw it at any moment
    //      - The stake can be unbonded. Unbonded stake can't be withdrawn immediately, there's a stake lock timeout. But it can be re-bonded, delegated to any validator.
    //
    //  4. Implementation details
    //
    // The implementation goal is to implement all the above functionality such that any method invocation works in O(1) complexity (ignoring the complexity of storage access).

    namespace State
    {
        struct Tag
        {
            static const uint8_t s_Global = 1;
            static const uint8_t s_Validator = 2;
            static const uint8_t s_Delegator = 3;
            static const uint8_t s_Unbonded = 4;
        };


        struct Accumulator
        {
            typedef MultiPrecision::UInt<5> Type;
            static const uint32_t s_Normalization = 3;

            Type m_Weighted;

            void Add(Amount reward, uint64_t nTotalWeight)
            {
                auto resid = MultiPrecision::FromEx<s_Normalization>(reward);
                Type ds;
                ds.SetDivResid(resid, MultiPrecision::From(nTotalWeight));

                Strict::Add(m_Weighted, ds);
            }

            struct Cursor
            {
                Type m_Pos0;

                Amount Update(const Accumulator& acc, uint64_t nWeight)
                {
                    Amount reward = 0;
                    if (m_Pos0 != acc.m_Weighted)
                    {
                        if (nWeight)
                        {
                            Type ds;
                            ds.SetSub(acc.m_Weighted, m_Pos0);

                            auto res = ds * MultiPrecision::From(nWeight); // TODO: type too long, Summa::Type should be sufficient

                            const MultiPrecision::Word nHalfMaxWord = (static_cast<MultiPrecision::Word>(-1) >> 1) + 1;
                            auto half = MultiPrecision::FromEx<s_Normalization - 1>(nHalfMaxWord);
                            res += half;

                            res.Get<s_Normalization>(reward);
                        }

                        m_Pos0 = acc.m_Weighted;
                    }

                    return reward;
                }
            };
        };

        typedef MultiPrecision::Float Float;

        struct Global
        {
            Settings m_Settings;

            Amount m_TotakStakeNonJailed;
            Amount m_RewardPending; // not yet accounted in summa. Defer heavy calculation until needed.
            Amount m_RewardInPoolRemaining;
            Accumulator m_accReward;

            void FlushRewardPending()
            {
                if (m_RewardPending)
                {
                    m_accReward.Add(m_RewardPending, m_TotakStakeNonJailed);
                    Strict::Add(m_RewardInPoolRemaining, m_RewardPending);
                    m_RewardPending = 0;
                }
            }
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
                static const uint8_t Slash = 2; // not stored, only used in method
            };

            // this is the part of interest to the node.
            uint64_t m_Weight;
            uint8_t m_Flags;
        };

        struct ValidatorPlus
            :public Validator
        {
            Accumulator m_accReward;
            Accumulator::Cursor m_cuRewardGlobal;

            void Init(const Global&);

            void FlushRewardPending(Global& g)
            {
                Amount reward = m_cuRewardGlobal.Update(g.m_accReward, (Flags::Jail & m_Flags) ? 0 : m_Weight);
                if (reward)
                    m_accReward.Add(reward, m_Weight);
            }

            void StakeAdd(Amount val, Global& g)
            {
                Strict::Add(m_Weight, val);
                if (!(Flags::Jail & m_Flags))
                    Strict::Add(g.m_TotakStakeNonJailed, val);
            }

            void StakeDel(Amount val, Global& g)
            {
                assert(m_Weight >= val);
                m_Weight -= val;

                if (!(Flags::Jail & m_Flags))
                {
                    assert(g.m_TotakStakeNonJailed >= val);
                    g.m_TotakStakeNonJailed -= val;
                }
            }

            Float m_kStakeScale;

            struct Self {
                PubKey m_Delegator;
                Amount m_Reward;
            } m_Self;
        };

        struct Delegator
        {
            struct Key
            {
                uint8_t m_Tag = Tag::s_Delegator;
                Address m_Validator;
                PubKey m_Delegator;
            };

            struct Bonded
            {
                Float m_kStakeScaled;
                Accumulator::Cursor m_cuReward;
            } m_Bonded;

            Amount m_RewardRemaining;

            Amount Pop(ValidatorPlus& vp, Global& g)
            {
                Amount stake = 0;
                if (!m_Bonded.m_kStakeScaled.IsZero())
                {
                    (m_Bonded.m_kStakeScaled / vp.m_kStakeScale).Round(stake);
                    if (stake > vp.m_Weight)
                        stake = vp.m_Weight;

                    vp.StakeDel(stake, g);
                }

                Amount reward = m_Bonded.m_cuReward.Update(vp.m_accReward, stake);
                if (reward)
                {
                    if (reward > g.m_RewardInPoolRemaining)
                    {
                        reward = g.m_RewardInPoolRemaining;
                        g.m_RewardInPoolRemaining = 0;
                    }
                    else
                        g.m_RewardInPoolRemaining -= reward;

                    Amount commission = reward / 20; // current commission is fixed at 5%

                    Strict::Add(m_RewardRemaining, reward - commission);
                    Strict::Add(vp.m_Self.m_Reward, commission);
                }

                return stake;
            }

        };

        struct Unbonded
        {
            struct Key
            {
                uint8_t m_Tag = Tag::s_Unbonded;
                PubKey m_Delegator;
                Height m_hLock_BE;
            };

            Amount m_Amount;
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

        struct DelegatorUpdate
        {
            static const uint32_t s_iMethod = 4;

            Address m_Validator;
            PubKey m_Delegator;

            int64_t m_StakeDeposit;
            int64_t m_StakeBond;
            Amount m_RewardClaim;
        };
    }

#pragma pack (pop)

} // namespace PBFT
