#pragma once

// 'vault' request types
namespace Vault
{
    // Hash of the compiled shader bytecode
    static const ShaderID s_SID = { 0xeb,0xdb,0xa9,0xa1,0xcc,0xc2,0xcf,0xcf,0x14,0x7a,0xfb,0xb3,0x6e,0x3a,0x94,0x1e,0x54,0x5a,0xba,0xbc,0xf8,0xe2,0xfc,0x9c,0xc7,0x54,0xc4,0xf5,0x40,0x15,0xe1,0x5a };
    
    // Hash of ShaderID + initialization parameters. Since vault does not have constructor parameters, this will always be Vault contract id
    static const ContractID s_CID = { 0x79,0x65,0xa1,0x8a,0xef,0xaf,0x30,0x50,0xcc,0xd4,0x04,0x48,0x2e,0xb9,0x19,0xf6,0x64,0x1d,0xaa,0xf1,0x11,0xc7,0xc4,0xa7,0x78,0x7c,0x2e,0x93,0x29,0x42,0xaa,0x91 };

#pragma pack (push, 1) // the following structures will be stored in the node in binary form

    // Account key consists of the public key and asset id
    // The value will be the Amount of coins stored in the account
    struct Key
    {
        PubKey m_Account;
        AssetID m_Aid;
    };

    // Base Request structure used for deposit and withdraw methods
    struct Request
        :public Key
    {
        Amount m_Amount;
    };

    struct Deposit :public Request {
        static const uint32_t s_iMethod = 2; // used to map to class methods to TX kernel. In the class implementation we expect to see Method_2
    };

    struct Withdraw :public Request {
        static const uint32_t s_iMethod = 3; // used to map to class methods to TX Kernel
    };

#pragma pack (pop)

}
