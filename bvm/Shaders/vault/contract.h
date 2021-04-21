#pragma once

// 'vault' request types
namespace Vault
{
    // Hash of the compiled shader bytecode
    static const ShaderID s_SID = { 0x97,0xc7,0xde,0xe6,0xf3,0x6e,0x28,0x15,0xab,0xe0,0x92,0x6b,0xa4,0x73,0x3b,0x40,0x6b,0xc0,0x09,0x02,0xc9,0xac,0x66,0x94,0xae,0xde,0x62,0x1b,0x74,0xa8,0x63,0x66 };
    
    // Hash of ShaderID + initialization parameters. Since vault does not have constructor parameters, this will always be Vault contract id
    static const ContractID s_CID = { 0xd9,0xc5,0xd1,0x78,0x2b,0x2d,0x2b,0x6f,0x73,0x34,0x86,0xbe,0x48,0x0b,0xb0,0xd8,0xbc,0xf3,0x4d,0x5f,0xdc,0x63,0xbb,0xac,0x99,0x6e,0xd7,0x6a,0xf5,0x41,0xcc,0x14 };

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
