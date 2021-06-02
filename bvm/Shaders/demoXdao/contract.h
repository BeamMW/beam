#pragma once
#include "../Sort.h"
#include "../Math.h"

namespace DemoXdao
{
    // prev versions, deployed on masternet under 'upgradable' contract
    static const ShaderID s_SID_0 = { 0x43,0x5f,0x96,0x42,0xf9,0xf0,0x1d,0xa6,0x24,0xe1,0x77,0xd4,0x23,0x2b,0xea,0x2c,0x71,0xa2,0xb9,0x58,0x0a,0x97,0x6b,0x17,0x33,0xb6,0x6b,0x71,0xe9,0xea,0x5c,0xd7 };
    static const ShaderID s_SID_1 = { 0x37,0x07,0xa0,0x37,0x53,0xe5,0x19,0x79,0xfe,0xc8,0x53,0x9a,0x85,0x0f,0x0d,0x7c,0x98,0x95,0xde,0x57,0x3b,0x84,0xf8,0x22,0x10,0x2b,0xb6,0x08,0x84,0x94,0x27,0x9a };
    static const ShaderID s_SID_2 = { 0xc8,0xd9,0x7b,0xe0,0x99,0x77,0xb9,0x30,0xa8,0x88,0x1c,0x3c,0xb6,0xc8,0x7a,0x6b,0x62,0x07,0x2a,0x71,0xfa,0x34,0xf9,0x67,0x49,0x6b,0xbb,0x0d,0x0c,0xa1,0x62,0xfe };

    // current version
    static const ShaderID s_SID = { 0xc4,0xbb,0x64,0x18,0x86,0xc6,0x50,0x84,0x0e,0x27,0xf4,0x0e,0xf4,0x2d,0xbe,0x92,0x6b,0x39,0x00,0xda,0x83,0xc6,0x91,0x19,0x55,0x6a,0xb7,0xea,0xfc,0xf0,0xf5,0xf4 };

#pragma pack (push, 1)


    struct State
    {
        static const uint8_t s_Key = 0;

        AssetID m_Aid;

        static const Amount s_TotalEmission = g_Beam2Groth * 10000;
        static const Amount s_ReleasePerLock = g_Beam2Groth * 10;
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
            static const Amount s_Emission = g_Beam2Groth * 1000000;
            static const Height s_Duration = 1440 * 120;

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

                    Amount val = (((Amount) resVal.get_Val<5>()) << 32) | resVal.get_Val<4>();

                    res = std::min(res, val);
                }


                m_TotalDistributed += res;
                return res;
            }
        };

    } // namespace Farming

    struct LockAndGet
    {
        static const uint32_t s_iMethod = 3; // used to map to class methods to TX kernel. In the class implementation we expect to see Method_2

        static const Amount s_MinLockAmount = g_Beam2Groth * 2;

        PubKey m_Pk;
        Amount m_Amount;
    };

    struct UpdPosFarming
    {
        static const uint32_t s_iMethod = 4;

        static const Amount s_MinLockAmount = g_Beam2Groth * 2;

        PubKey m_Pk;
        Amount m_WithdrawBeamX;
        Amount m_Beam; // deposit or withdraw
        uint8_t m_BeamLock;
    };

#pragma pack (pop)

}
