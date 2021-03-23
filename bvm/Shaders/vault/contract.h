#pragma once

// 'vault' request types
namespace Vault
{
    // Hash of the compiled shader bytecode
    static const ShaderID s_SID = { 0x16,0x99,0x06,0xe3,0x91,0xb8,0x43,0x8d,0x5e,0xfb,0xc9,0x28,0xe4,0x7c,0x40,0x5e,0x7e,0x68,0x2c,0x82,0x8f,0xe8,0x07,0x08,0x00,0x24,0x95,0x73,0xfa,0x7b,0xd0,0x78 };
    
    // Hash of ShaderID + initialization parameters. Since vault does not have constructor parameters, this will always be Vault contract id
    static const ContractID s_CID = { 0x61,0xf4,0x8b,0xe7,0x71,0x79,0xb1,0x4d,0x28,0xc1,0x55,0xb2,0xbd,0x46,0x96,0xc2,0x07,0x47,0x5b,0x3f,0xd6,0x92,0x60,0x7a,0x98,0xce,0x96,0x37,0x1a,0xda,0x11,0x67 };

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
