#pragma once
#include "../Sort.h"
#include "../Math.h"
#include "../upgradable3/contract.h"

namespace DaoCore2
{
    static const ShaderID s_pSID[] = {
        { 0x77,0x03,0x8b,0x81,0x0c,0x1f,0x76,0x54,0xa8,0xda,0x42,0x8a,0x9c,0xd0,0x04,0x1c,0x22,0x11,0x64,0x6d,0x3a,0xd1,0xac,0xa3,0x34,0x31,0x76,0x8f,0x24,0x18,0x2d,0xa6 },
    };

#pragma pack (push, 1)

    struct Tags
    {
        static const uint8_t s_State = 0;
        static const uint8_t s_Preallocated = 1;
        static const uint8_t s_Farm = 2;
        static const uint8_t s_WithdrawReserve = 3;
    };


    struct State
    {
        AssetID m_Aid;
    };

    namespace Farming
    {
        namespace Weight
        {
            typedef uint32_t Type;

            static const Amount s_LutX[] = { 1600000000,3125854492,6177516910,12280655489,24486187635,48894272061,97698522904,195259364190,390190498244,779291316610,1554453103198,3092664773460,6121018999289,11988455311833,22989898397853,42242423798389,71121211899194,100000000000000 };
            static const Type s_LutY[] = { 439,702,1131,1830,2966,4813,7814,12689,20601,33434,54213,87747,141506,226534,357335,547046,787769,1000000 };

            inline Type Calculate(Amount val)
            {
                static_assert(_countof(s_LutX) == _countof(s_LutY));
                if (val < s_LutX[0])
                    return 0; // minimum required

                return LutCalculate(s_LutX, s_LutY, _countof(s_LutX), val);
            }
        }

        typedef MultiPrecision::UInt<5> SigmaType; // sum (period_emission / period_weight) * normalization

        struct UserPos
        {
            struct Key {
                uint8_t m_Tag = Tags::s_Farm;
                PubKey m_Pk;
            };

            SigmaType m_SigmaLast;
            Amount m_Beam;
            Amount m_BeamX;
        };

        struct State
        {
            static const Amount s_Emission = g_Beam2Groth * 1'000'000;

            uint64_t m_WeightTotal;
            Height m_hTotal;
            Height m_hLast;
            SigmaType m_Sigma;
            Amount m_TotalDistributed;

            static Amount get_EmissionSoFar()
            {
                return s_Emission;
            }

            Amount RemoveFraction(const UserPos& up)
            {
                Weight::Type w = Weight::Calculate(up.m_Beam);
                if (!w)
                    return 0;

                Amount res = get_EmissionSoFar();
                assert(res >= m_TotalDistributed);
                res -= m_TotalDistributed;

                assert(m_WeightTotal >= w);
                m_WeightTotal -= w;

                // last one gets all the round-off residuals, otherwise do the math
                if (m_WeightTotal)
                {
                    SigmaType dSigma = m_Sigma - up.m_SigmaLast;
                    auto resVal = dSigma * MultiPrecision::UInt<1>(w);

                    // ignore the 3 least-significant words, those are normalization factor
                    // The result is the next 2 words.
                    auto val = resVal.Get<3, Amount>();
                    res = std::min(res, val);
                }


                m_TotalDistributed += res;
                return res;
            }
        };

    } // namespace Farming

    struct Preallocated
    {
        struct User
        {
            struct Key {
                uint8_t m_Tag = Tags::s_Preallocated;
                PubKey m_Pk;
            };

            Height m_Vesting_h0; // start
            Height m_Vesting_dh; // duration

            Amount m_Total;
            Amount m_Received;
        };

        static const Amount s_Emission = g_Beam2Groth * 99'000'000;

        static const Amount s_Unassigned = g_Beam2Groth * (
            35'000'000 + // liquidity mining
            20'000'000 // Dao treasury 
        );
    };

    namespace Method
    {
        struct GetPreallocated
        {
            static const uint32_t s_iMethod = 3;

            PubKey m_Pk;
            Amount m_Amount;
        };

        struct UpdPosFarming
        {
            static const uint32_t s_iMethod = 4;

            PubKey m_Pk;
            Amount m_WithdrawBeamX;
            Amount m_Beam; // deposit or withdraw
            uint8_t m_BeamLock;
        };

        struct AdminWithdraw
        {
            static const uint32_t s_iMethod = 5;

            uint32_t m_ApproveMask;
            Amount m_BeamX;
        };
    }

#pragma pack (pop)

} // namespace DaoCore2
