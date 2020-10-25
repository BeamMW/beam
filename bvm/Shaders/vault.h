#pragma once

// 'vault' request types
namespace Vault
{
    static const ContractID s_ID = { 0x45,0x64,0xe7,0xab,0xc7,0x09,0x23,0x8a,0x6b,0x3d,0x78,0x16,0x35,0xef,0x02,0x06,0xda,0x78,0x84,0x9e,0x12,0xba,0xd2,0x1d,0x3a,0xee,0x84,0x75,0xd0,0x3f,0xf2,0x7f };

#pragma pack (push, 1)

    struct Key
    {
        PubKey m_Account;
        AssetID m_Aid;

        // methods are for internal use
        Amount Load() const;
        void Save(Amount) const;

        template <bool bToShader>
        void Convert()
        {
            ConvertOrd<bToShader>(m_Aid);
        }
    };

    // same param for deposit and withdraw methods
    struct Request
        :public Key
    {
        Amount m_Amount;

        template <bool bToShader>
        void Convert()
        {
            Key::Convert<bToShader>();
            ConvertOrd<bToShader>(m_Amount);
        }
    };

    struct Deposit :public Request {
        static const uint32_t s_iMethod = 2;
    };

    struct Withdraw :public Request {
        static const uint32_t s_iMethod = 3;
    };

#pragma pack (pop)

}
