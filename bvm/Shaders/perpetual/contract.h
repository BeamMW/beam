#pragma once

namespace Perpetual
{
#pragma pack (push, 1) // the following structures will be stored in the node in binary form

    // Hash of the compiled shader bytecode
    static const ShaderID s_SID = { 0xeb,0xdb,0xa9,0xa1,0xcc,0xc2,0xcf,0xcf,0x14,0x7a,0xfb,0xb3,0x6e,0x3a,0x94,0x1e,0x54,0x5a,0xba,0xbc,0xf8,0xe2,0xfc,0x9c,0xc7,0x54,0xc4,0xf5,0x40,0x15,0xe1,0x5a };

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

        Amount m_Amount1;
        Amount m_Amount2;
    };

#pragma pack (pop)
}
