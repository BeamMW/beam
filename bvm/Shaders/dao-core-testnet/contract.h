#pragma once
#include "../Sort.h"
#include "../Math.h"

namespace DaoCore
{
    // current version
    static const ShaderID s_SID_0 = { 0xa0,0x43,0xee,0xdc,0x14,0xc1,0xe0,0x2e,0xf7,0xde,0xb8,0xb0,0x1f,0xaf,0xaa,0xa6,0x38,0x54,0x09,0x03,0x72,0x78,0xf7,0x36,0x36,0x77,0x5e,0x63,0xef,0xab,0xbd,0x86 };
    static const ShaderID s_SID_1 = { 0x47,0xba,0x60,0x4f,0xf2,0xc0,0xf9,0x58,0x2f,0xba,0x70,0x84,0x79,0xb0,0xc2,0x56,0xfc,0x7d,0x90,0x38,0x83,0x73,0xf7,0xa3,0xc1,0xcd,0x69,0xa7,0x16,0xbd,0xd6,0x31 };

#pragma pack (push, 1)


    struct State
    {
        static const uint8_t s_Key = 0;

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

        static const uint8_t s_Key = 2;

        struct UserPos
        {
            struct Key {
                uint8_t m_Tag = s_Key;
                PubKey m_Pk;
            };

            SigmaType m_SigmaLast;
            Amount m_Beam;
            Amount m_BeamX;
        };

        struct State
        {
            static const Amount s_Emission = g_Beam2Groth * 1'000'000;
            static const Height s_Duration = 1440 * 365 / 4; // 3 months

            static const uint32_t s_EmissionPerBlock = static_cast<uint32_t>(s_Emission / s_Duration);
            static_assert(s_Emission - s_EmissionPerBlock * s_Duration < g_Beam2Groth, "the round-off error should be low");

            uint64_t m_WeightTotal;
            Height m_hTotal;
            Height m_hLast;
            SigmaType m_Sigma;
            Amount m_TotalDistributed;

            void Update(Height h)
            {
                Height dh = h - m_hLast;
                if (dh && m_WeightTotal && (m_hTotal < s_Duration))
                {
                    if (dh > s_Duration - m_hTotal)
                        dh = s_Duration - m_hTotal;

                    m_hTotal += dh;

                    // Original formula: 
                    //      m_Sigma += s_Emission * dh / s_Duration / m_WeightTotal;
                    //
                    // replace (s_Emission / s_Duration) by s_EmissionPerBlock, we obtain
                    //      m_Sigma += s_EmissionPerBlock * dh / m_WeightTotal;
                    //
                    // Since we work in fixed point, and to minimize the precision loss, we effectively multiply this by a factor.
                    // Practically we left-shift the result by 96 bits (3 32-byte words)
                    // s_EmissionPerBlock * dh is limited to 64 bites (2 32-byte words).
                    //
                    SigmaType emission;
                    emission.Set<3>(((uint64_t) s_EmissionPerBlock) * dh);

                    SigmaType ds;
                    ds.SetDivResid(emission, MultiPrecision::UInt<2>(m_WeightTotal));

                    m_Sigma += ds;
                }
            }

            Amount get_EmissionSoFar() const
            {
                if (m_hTotal >= s_Duration)
                    return s_Emission; // at the end - compensate to the round-off error

                return m_hTotal * s_EmissionPerBlock;
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
            static const uint8_t s_Key = 1;

            struct Key {
                uint8_t m_Tag = s_Key;
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

#pragma pack (pop)

}
