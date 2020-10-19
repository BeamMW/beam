#pragma once

namespace StableCoin
{
#pragma pack (push, 1)

    template <uint32_t nMeta>
    struct Ctor
    {
        static const uint32_t s_iMethod = 0;

        ContractID m_RateOracle;             // should return beam-to-coin ratio, i.e. 1coin == 1beam * ratio
        uint64_t m_CollateralizationRatio;   // collateral >= coins * m_CollateralizationRatio
        Height m_BiddingDuration;            // (in blocks)
        uint32_t m_nMetaData;                // size of metadata for the coin asset
        uint8_t m_pMetaData[nMeta]; // variable size

        template <bool bToShader>
        void Convert()
        {
            ConvertOrd<bToShader>(m_CollateralizationRatio);
            ConvertOrd<bToShader>(m_BiddingDuration);
            ConvertOrd<bToShader>(m_nMetaData);
        }
    };

    struct Balance
    {
        Amount m_Beam;
        Amount m_Asset;

        struct Direction {
            uint8_t m_BeamAdd;   // adding more beams to position, or withdrawing?
            uint8_t m_AssetAdd;  // paying the debt, or taking it?
        };

        template <bool bToShader>
        void Convert()
        {
            ConvertOrd<bToShader>(m_Beam);
            ConvertOrd<bToShader>(m_Asset);
        }
    };

    struct UpdatePosition
    {
        static const uint32_t s_iMethod = 2;

        PubKey m_Pk;      // The account
        Balance m_Change; // The amounts (beams + coins) to be moved in/out
        Balance::Direction m_Direction; // funds move directions

        template <bool bToShader>
        void Convert()
        {
            m_Change.Convert<bToShader>();
        }
    };

    struct PlaceBid
    {
        static const uint32_t s_iMethod = 3;

        PubKey m_PkTarget; // The weak position we're trying to buy
        PubKey m_PkBidder; // My (bidder) pk
        Balance m_Bid;     // Amounts (beam/coins) for the bid. Upon successful bid they are locked in the contract. If no win - the amounts will be returned via Vault.

        template <bool bToShader>
        void Convert()
        {
            m_Bid.Convert<bToShader>();
        }
    };

    struct Grab
    {
        static const uint32_t s_iMethod = 4;

        PubKey m_PkTarget; // The weak position for which the bid is over
        // Upon successful completion this position is terminated, and the funds moved into winner's position
    };

#pragma pack (pop)

}
