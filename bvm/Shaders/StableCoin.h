#pragma once

namespace StableCoin
{
#pragma pack (push, 1)

    template <uint32_t nMeta>
    struct Ctor
    {
        static const uint32_t s_iMethod = 0;

        ContractID m_RateOracle; // should return beam-to-coin ratio, i.e. 1coin == 1beam * ratio
        uint64_t m_CollateralizationRatio;
        uint32_t m_nMetaData;
        uint8_t m_pMetaData[nMeta]; // variable size
    };

    struct FundsIO
    {
        Amount m_Beam;
        Amount m_Asset;
        uint8_t m_BeamAdd;
        uint8_t m_AssetAdd;
    };

    struct UpdatePosition
        :public FundsIO
    {
        static const uint32_t s_iMethod = 2;
        PubKey m_Pk;
    };

#pragma pack (pop)

}
