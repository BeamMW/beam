#pragma once

// 'vault' request types
namespace Vault
{
    // Hash of the compiled shader bytecode
    static const ShaderID s_SID = { 0x8f,0xd0,0xf7,0xed,0x46,0xca,0x6a,0xab,0x12,0x3c,0x77,0x12,0x28,0x0d,0xf3,0x8f,0xaf,0x61,0xc1,0x74,0x88,0xa3,0x6b,0x80,0xa6,0x18,0x19,0x4a,0x13,0xb7,0x9f,0xd2 };
    
    // Hash of ShaderID + initialization parameters. Since vault does not have constructor parameters, this will always be Vault contract id
    static const ContractID s_CID = { 0xe7,0x99,0x78,0xc2,0xf3,0x8c,0xcf,0x5c,0x71,0xaa,0x16,0x4d,0x50,0x14,0xa5,0x78,0x0f,0x88,0xc9,0x3a,0x62,0x41,0x4b,0x06,0xba,0x00,0x0f,0x41,0xa1,0xea,0xfd,0x56 };

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
