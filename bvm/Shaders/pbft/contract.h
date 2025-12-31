#pragma once
#include "../Math.h"
#include "../Float.h"

namespace PBFT
{
#pragma pack (push, 1)

    static const ShaderID s_SID = { 0x49,0xe6,0x78,0x74,0xd3,0x2e,0xe9,0xf7,0x62,0x2b,0x04,0x6b,0xef,0x68,0xb2,0xf5,0xe0,0x7a,0x5d,0x8c,0xe1,0x43,0x09,0x2d,0xca,0x85,0xa2,0xdd,0x61,0xd7,0x0b,0x50 };

    struct Settings
    {
        AssetID m_aidStake;
        uint32_t m_hUnbondLock;
        Amount m_MinValidatorStake;
    };

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

            void RewardFixSubtract(Amount& val)
            {
                if (m_RewardInPoolRemaining >= val)
                    m_RewardInPoolRemaining -= val;
                else
                {
                    val = m_RewardInPoolRemaining;
                    m_RewardInPoolRemaining = 0;
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

        struct ValidatorPlus
            :public Validator
        {
            Accumulator m_accReward;
            Accumulator::Cursor m_cuRewardGlobal;
            Float m_kStakeScale;
            uint16_t m_Commission_cpc; // commission in centi-percents

            static const uint16_t s_CommissionMax = 8000; // 80%
            static const uint16_t s_CommissionTagTomb = 12000; // 80%


            //void Init(const Global&);

            void FlushRewardPending(Global& g)
            {
                Amount reward = m_cuRewardGlobal.Update(g.m_accReward, (Status::Active == m_Status) ? m_Weight : 0);
                if (reward)
                {
                    Amount commission = (MultiPrecision::From(reward) * MultiPrecision::From(m_Commission_cpc) / MultiPrecision::From(10000)).Get<0, Amount>(); // rounds down

                    g.RewardFixSubtract(commission);
                    Strict::Add(m_Self.m_Commission, commission);

                    reward -= commission;

                    Amount wScaled;
                    (m_kStakeScale * Float(m_Weight)).Round(wScaled);
                    m_accReward.Add(reward, wScaled);
                }
            }

            template <bool bAdd>
            static void StakeChangeAny(Amount& total, Amount delta)
            {
                if constexpr (bAdd)
                    Strict::Add(total, delta);
                else
                {
                    assert(total >= delta);
                    total -= delta;
                }
            }

            template <bool bAdd>
            void StakeChangeExternal(Global& g, Amount delta) const
            {
                if (Status::Active == m_Status)
                    StakeChangeAny<bAdd>(g.m_TotakStakeNonJailed, delta);
            }

            template <bool bAdd>
            void StakeChangeExternal(Global& g) const
            {
                StakeChangeExternal<bAdd>(g, m_Weight);
            }

            template <bool bAdd>
            void StakeChange(Global& g, Amount delta)
            {
                StakeChangeAny<bAdd>(m_Weight, delta);
                StakeChangeExternal<bAdd>(g, delta);
            }

            void ChangeStatus(Global& g, Status s)
            {
                StakeChangeExternal<false>(g);
                m_Status = s;
                StakeChangeExternal<true>(g);
            }

            struct Self {
                PubKey m_Delegator;
                Amount m_Commission;
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
                Amount stake = 0, wReward = 0;
                if (!m_Bonded.m_kStakeScaled.IsZero())
                {
                    m_Bonded.m_kStakeScaled.Round(wReward);
                    (m_Bonded.m_kStakeScaled / vp.m_kStakeScale).Round(stake);
                    if (stake > vp.m_Weight)
                        stake = vp.m_Weight;

                    vp.StakeChange<false>(g, stake);
                }

                Amount reward = m_Bonded.m_cuReward.Update(vp.m_accReward, wReward);
                if (reward)
                {
                    g.RewardFixSubtract(reward);
                    Strict::Add(m_RewardRemaining, reward);
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
        struct Create
        {
            static const uint32_t s_iMethod = 0;
            Settings m_Settings;
        };

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

        struct DelegatorUpdate
        {
            static const uint32_t s_iMethod = 5;

            Address m_Validator;
            PubKey m_Delegator;

            int64_t m_StakeDeposit;
            int64_t m_StakeBond;
            Amount m_RewardClaim;
        };

        struct ValidatorRegister
        {
            static const uint32_t s_iMethod = 6;

            Address m_Validator;
            PubKey m_Delegator;
            Amount m_Stake;
            uint16_t m_Commission_cpc;
        };

        struct ValidatorUpdate
        {
            static const uint32_t s_iMethod = 7;

            Address m_Validator;
            uint16_t m_Commission_cpc; // set to invalid value to trigger the termination
        };
    }

#pragma pack (pop)

} // namespace PBFT
