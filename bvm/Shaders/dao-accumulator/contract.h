#pragma once
#include "../Math.h"
#include "../upgradable3/contract.h"

namespace DaoAccumulator
{
    static const ShaderID s_pSID[] = {
        { 0x8b,0xce,0x8c,0x1f,0x0a,0xd8,0xd9,0xd1,0x95,0x63,0x19,0x66,0x6a,0xf7,0x89,0xd7,0xfd,0x68,0xab,0x7e,0xc9,0xd4,0x62,0x15,0x6f,0xcb,0x03,0xc9,0xbd,0x79,0xcc,0x96 },
        { 0xcc,0xed,0x12,0x15,0xca,0x99,0x34,0x62,0x0b,0x02,0x0f,0xea,0x17,0xb3,0xf5,0x9e,0xff,0x58,0xca,0x6b,0x38,0xb3,0x97,0x62,0xbc,0x42,0xf0,0x95,0xc0,0xe5,0x23,0x3a },
        { 0x75,0xe5,0x01,0x73,0xfd,0xb5,0xcf,0xce,0x65,0x9a,0x70,0xf0,0x62,0x3d,0x69,0x23,0xce,0xb1,0x08,0x0a,0x96,0x05,0x18,0xe4,0x7d,0x5a,0x80,0x50,0xa8,0xc9,0xd0,0xe8 },
    };

#pragma pack (push, 1)

    using MultiPrecision::Float;

    struct Tags
    {
        static const uint8_t s_State = 0;
        static const uint8_t s_User = 0; // better to use different value, but not a problem really

        static const uint8_t s_PoolBeamNph = 1;
        static const uint8_t s_UserBeamNph = 2;
    };

    struct Pool
    {
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
            uint8_t m_Tag;
            Key(uint8_t tag) :m_Tag(tag) {}
            PubKey m_pk;
        };

        Pool::User m_PoolUser;
        Amount m_LpToken;
        Amount m_EarnedBeamX; // earned but not withdrawn yet

        Height m_hEnd;

        static const uint32_t s_LockPeriodBlocks = 1440 * 365 / 12; // 1 month
        static const uint8_t s_LockPeriodsMax = 12; // 1 year

        void set_Weight(uint64_t nLockPeriods, bool bPrePhase)
        {
            uint64_t val = m_LpToken * nLockPeriods / s_LockPeriodsMax; // don't care about overflow
            if (bPrePhase)
                val *= 2;

            m_PoolUser.m_Weight = val;
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

        struct UserLock
        {
            static const uint32_t s_iMethod = 4;

            struct Type {
                static const uint8_t BeamX = 0;
                static const uint8_t BeamX_PrePhase = 1;
                static const uint8_t Nph = 2;
            };

            PubKey m_pkUser;
            Amount m_LpToken;
            Height m_hEnd;
            uint8_t m_PoolType;
        };

        struct UserWithdraw_Base
        {
            PubKey m_pkUser;
            uint8_t m_WithdrawLPToken;
            Amount m_WithdrawBeamX;
        };

        struct UserWithdraw_FromBeamBeamX :public UserWithdraw_Base {
            static const uint32_t s_iMethod = 5;
        };
        struct UserWithdraw_FromBeamNph :public UserWithdraw_Base {
            static const uint32_t s_iMethod = 6;
        };

    }

#pragma pack (pop)

} // namespace DaoAccumulator
