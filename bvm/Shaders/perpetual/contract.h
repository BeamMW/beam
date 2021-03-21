#pragma once

namespace Perpetual
{
#pragma pack (push, 1) // the following structures will be stored in the node in binary form

    // Hash of the compiled shader bytecode
    static const ShaderID s_SID = { 0x60,0x53,0x42,0x8e,0x6b,0xc5,0x58,0xb7,0x70,0xc7,0xe7,0xab,0xf1,0xe2,0xc9,0x38,0xdd,0xee,0xf7,0x28,0xec,0x15,0xa0,0xf4,0x8d,0x80,0x04,0xaa,0xb1,0xfc,0x29,0x91 };

    struct Create
    {
        static const uint32_t s_iMethod = 0; // C'tor

        uint32_t m_MarginRequirement_mp; // milli-percentage
        ContractID m_Oracle;
    };


    // D'tor - absent

    struct CreateOffer
    {
        static const uint32_t s_iMethod = 2;

        PubKey m_Account;

        Amount m_TotalBeams;
        Amount m_AmountBeam;
        Amount m_AmountToken;
    };

    struct CancelOffer
    {
        static const uint32_t s_iMethod = 3;

        PubKey m_Account;
    };

    typedef Create Global;

    struct OfferState
    {
        PubKey m_User2; // User1 is the key

        Amount m_AmountBeam;
        Amount m_AmountToken;

        Amount m_Amount1;
        Amount m_Amount2;
    };

#pragma pack (pop)
}
