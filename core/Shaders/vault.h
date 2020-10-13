#pragma once

// 'vault' request types
namespace Vault
{
#pragma pack (push, 1)

    struct Key
    {
        PubKey m_Account;
        AssetID m_Aid;

        // methods are for internal use
        Amount Load() const;
        void Save(Amount) const;
    };

    // same param for deposit and withdraw methods
    struct Request
    {
        Key m_Key;
        Amount m_Amount;
    };

    struct Deposit :public Request {
        static const uint32_t s_iMethod = 2;
    };

    struct Withdraw :public Request {
        static const uint32_t s_iMethod = 3;
    };

#pragma pack (pop)

}
