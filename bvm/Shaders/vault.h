#pragma once

// 'vault' request types
namespace Vault
{
    static const ContractID s_ID = { 0x6d,0xfc,0x83,0x27,0xb8,0x0e,0x12,0xf9,0x5b,0xa5,0x0d,0xf4,0xf4,0x46,0x39,0x39,0xbc,0x08,0x92,0x64,0x96,0xfb,0xbd,0x71,0xac,0x8f,0x44,0x23,0x97,0x55,0xbd,0x0d };

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
