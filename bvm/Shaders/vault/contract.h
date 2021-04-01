#pragma once

// 'vault' request types
namespace Vault
{
    // Hash of the compiled shader bytecode
    static const ShaderID s_SID = { 0xc0,0xb9,0x7d,0x73,0x43,0x95,0xf7,0xe3,0xec,0x99,0xce,0xec,0x16,0xe7,0xae,0xe6,0x9c,0x0f,0xab,0xd5,0xcb,0x0c,0x52,0xc1,0x32,0x7c,0x7d,0x59,0x4b,0xf4,0xac,0xf4 };
    
    // Hash of ShaderID + initialization parameters. Since vault does not have constructor parameters, this will always be Vault contract id
    static const ContractID s_CID = { 0xf5,0xd1,0xfb,0xa1,0xb6,0xe6,0x58,0xed,0x63,0x8e,0x3a,0xfb,0xa0,0xdf,0x35,0x81,0x80,0x90,0xe9,0x84,0x23,0xf6,0x54,0x9b,0x35,0xfc,0x23,0xd6,0x7e,0x4a,0x35,0xcb };

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
