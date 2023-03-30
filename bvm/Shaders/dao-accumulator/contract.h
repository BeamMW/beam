#pragma once
#include "../Math.h"
#include "../upgradable3/contract.h"

namespace DaoAccumulator
{
    static const ShaderID s_pSID[] = {
        { 0x6d,0x61,0x1e,0xc9,0xff,0x3b,0x18,0xb9,0xc7,0xf7,0x0d,0x2f,0x8e,0x03,0x99,0xd2,0xfa,0xd2,0xf5,0xfe,0xd8,0x7f,0x54,0xa5,0xfe,0xb3,0x14,0xd6,0xd4,0x84,0xd1,0xb4 },
        { 0xc6,0x9d,0x73,0x5f,0xf5,0xd5,0x64,0x56,0x27,0x44,0x33,0xbd,0x49,0xf0,0xd7,0x84,0x12,0xaa,0x08,0x29,0xcf,0xa1,0x07,0x0a,0x6c,0x21,0x9c,0x5f,0xdd,0xbd,0x78,0x8d },
        { 0xdf,0x15,0xe3,0x70,0x3e,0x23,0xdc,0x33,0x8b,0x58,0x1c,0xe4,0x05,0x20,0xa6,0xab,0xc6,0x6b,0x50,0xdd,0xaa,0x6f,0xf2,0xa6,0x00,0x9c,0xbb,0xc8,0xbb,0xcc,0x82,0x98 },
        { 0x79,0xa5,0xcf,0x6b,0xe5,0xc9,0x59,0x4f,0xd9,0xfb,0xb6,0xb1,0x9b,0x18,0x84,0x94,0x7c,0x22,0x64,0x5b,0x21,0x96,0x93,0x51,0x15,0xee,0xdc,0x45,0xb7,0x94,0xec,0x4b },
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

        void Add(User& u)
        {
            Strict::Add(m_Weight, u.m_Weight);
            u.m_Sigma0 = m_Sigma;
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
                m_Weight -= w;

                Float dSigma = m_Sigma - u.m_Sigma0;
                (dSigma * Float(w)).RoundDown(ret);

                if (ret > m_AmountInPool) // round-off errors?
                    ret = m_AmountInPool;
            }
            else
            {
                ret = m_AmountInPool;
                m_Weight = 0;
            }

            m_AmountInPool -= ret;
            return ret;
        }

    };

    struct State
    {
        Height m_hPreEnd;
        AssetID m_aidBeamX;
        AssetID m_aidLpToken; // set iff everything has been deposited into amm
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

        Height get_UnlockHeight(const State& s) const
        {
            return s.m_hPreEnd + s_PreLockPeriodBlocks * m_PrePhaseLockPeriods;
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
