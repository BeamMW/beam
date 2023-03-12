#pragma once
#include "../Math.h"
#include "../upgradable3/contract.h"

namespace DaoAccumulator
{
    static const ShaderID s_pSID[] = {
        { 0x19,0xf1,0x27,0x96,0xa1,0xf8,0xfe,0xf9,0x82,0xcf,0x0f,0xe3,0x8b,0xcf,0xc7,0x6a,0x4a,0x56,0xd1,0x37,0x28,0x14,0x77,0x5d,0x79,0x36,0xec,0xf8,0xdf,0x60,0xfc,0x18 },
    };

#pragma pack (push, 1)

    using MultiPrecision::Float;

    struct Tags
    {
        static const uint8_t s_State = 0;
        static const uint8_t s_User = 0;
    };


    struct Pool
    {
//        static const Amount s_Emission = g_Beam2Groth * 6'000'000;
//        static const Height s_Duration = 1440 * 365 * 2; // 2 years

        Height m_hLast;
        Height m_hRemaining;
        Amount m_AmountRemaining;
        Amount m_AmountInPool;
        uint64_t m_Weight;
        Float m_Sigma;

        struct User
        {
            Float m_Sigma0;
            uint64_t m_Weight;
        };

        void Update(Height h)
        {
            if (h == m_hLast)
                return;

            Height dh = h - m_hLast;
            m_hLast = h;

            if (!m_Weight || !m_hRemaining)
                return;

            Amount valDelta;
            Float fDelta;

            if (dh < m_hRemaining)
            {
                fDelta = Float(m_AmountRemaining) * Float(dh) / Float(m_hRemaining);
                fDelta.RoundDown(valDelta);
                assert(valDelta <= m_AmountRemaining);

                m_AmountRemaining -= valDelta;
                m_hRemaining -= dh;
            }
            else
            {
                valDelta = m_AmountRemaining;
                fDelta = valDelta;

                m_AmountRemaining = 0;
                m_hRemaining = 0;
            }

            m_AmountInPool += valDelta;

            m_Sigma = m_Sigma + fDelta / Float(m_Weight);
        }

        Amount Remove(const User& u)
        {
            uint64_t w = u.m_Weight;
            if (!w)
                return 0;
            assert(w <= m_Weight);

            Amount ret;
            if (w < m_Weight)
            {
                (Float(m_AmountInPool) * Float(w) / Float(m_Weight)).RoundDown(ret);
                assert(ret <= m_AmountInPool);

                m_AmountInPool -= ret;
                m_Weight -= w;
            }
            else
            {
                ret = m_AmountInPool;
                m_AmountInPool = 0;
                m_Weight = 0;
            }

            return ret;
        }

    };

    struct State
    {
        Height m_hPreEnd;
        AssetID m_aidBeamX;
        AssetID m_aidLpToken; // set iff everything has been deposited into amm
        Amount m_InitialLpTokenRemaining;
        Pool m_Pool;

        static const uint32_t s_InitialRatio = 2; // 1 beamX + 2 beam
        // assuming the amount of initial LP-tokens traded in DEX is equal to amount of accumulated Beams
    };


    struct User
    {
        struct Key {
            uint8_t m_Tag = Tags::s_User;
            PubKey m_pk;
        };

        Pool::User m_PoolUser;
        Amount m_LpTokenPrePhase;
        Amount m_LpTokenPostPhase; // how much LP-Token did the user deposit AFTER the pre-phase
        Amount m_EarnedBeamX; // earned but not withdrawn yet

        uint8_t m_PrePhaseLockPeriods;

        static const uint32_t s_PreLockPeriodBlocks = 1440 * 365 / 4; // 3 months
        static const uint8_t s_PreLockPeriodsMax = 4; // 1 year

        uint64_t get_WeightPrePhase() const
        {
            uint64_t val = m_LpTokenPrePhase * 2; // don't care about overflow
            uint64_t extra = (m_LpTokenPrePhase / 2) * m_PrePhaseLockPeriods;

            return val + extra;
        }

        uint64_t get_WeightPostPhase() const
        {
            return m_LpTokenPostPhase; // assume it's 1/2 of Pre-Phase weight
        }
    };

    namespace Method
    {
        struct Create
        {
            static const uint32_t s_iMethod = 0;

            Upgradable3::Settings m_Upgradable;
            Height m_hPrePhaseEnd;
            AssetID m_aidBeamX;
        };

        struct FarmStart
        {
            static const uint32_t s_iMethod = 3;

            uint32_t m_ApproveMask;
            AssetID m_aidLpToken;
            Amount m_FarmBeamX; // total value to farm
            Height m_hFarmDuration;

        };

        struct UserLockPrePhase
        {
            static const uint32_t s_iMethod = 4;

            PubKey m_pkUser;
            Amount m_AmountBeamX;
            uint8_t m_PrePhaseLockPeriods;
        };

        struct UserUpdate
        {
            static const uint32_t s_iMethod = 5;

            // withdraw lp-tokens locked during pre-phase
            // lock/unlock more lp-tokens
            // withdraw earned beamX
            PubKey m_pkUser;
            Amount m_NewLpToken;
            Amount m_WithdrawBeamX;
        };

    }

#pragma pack (pop)

} // namespace DaoAccumulator
