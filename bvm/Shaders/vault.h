#pragma once

// 'vault' request types
namespace Vault
{
    static const ContractID s_ID = { 0x79,0x65,0xa1,0x8a,0xef,0xaf,0x30,0x50,0xcc,0xd4,0x04,0x48,0x2e,0xb9,0x19,0xf6,0x64,0x1d,0xaa,0xf1,0x11,0xc7,0xc4,0xa7,0x78,0x7c,0x2e,0x93,0x29,0x42,0xaa,0x91 };

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
