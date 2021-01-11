#pragma once

namespace Perpetual
{
#pragma pack (push, 1) // the following structures will be stored in the node in binary form

    // Hash of the compiled shader bytecode
    static const ShaderID s_SID = { 0x18,0x37,0x4d,0xbd,0x38,0x29,0x36,0xea,0xea,0x07,0x95,0x16,0xf9,0xf7,0xc8,0x42,0x58,0x5e,0xdd,0xcc,0xe2,0x35,0x05,0x0c,0x56,0xcd,0xcb,0xd3,0xd7,0x80,0xe9,0x51, };

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
