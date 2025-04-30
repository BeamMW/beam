#pragma once
#include "../upgradable3/contract.h"
#include "contract_l2.h"

namespace L2Tst1_L1
{
    static const ShaderID s_pSID[] = {
        { 0xc7,0xe0,0x02,0x18,0x91,0x04,0x54,0x5f,0x56,0xe6,0x0e,0x46,0x1b,0xbe,0xa6,0xb0,0x90,0xd6,0x6f,0x3e,0x69,0xfa,0x45,0x0a,0x25,0x5d,0xf7,0x13,0x9d,0xad,0x3d,0x3c },
    };

#pragma pack (push, 1)

    struct Tags
    {
        static const uint8_t s_State = 0;
        static const uint8_t s_User = 1;
        static const uint8_t s_BridgeExp = 2;
        static const uint8_t s_BridgeImp = 3;
        static const uint8_t s_Validators = 4;
    };

    struct Settings
    {
        AssetID m_aidStaking; // would be BeamX on mainnet L2
        AssetID m_aidLiquidity; // would be Beam on maiinet L2
        Height m_hPreEnd;
        uint32_t m_BridgeValidatorsMin;
    };

    struct Validator
    {
        static const uint32_t s_Max = 32;

        PubKey m_pk;
        //Amount m_Stake;
    };

    struct State
    {
        Settings m_Settings;
        // no more fields so far
    };


    struct User
    {
        struct Key {
            uint8_t m_Tag = Tags::s_User;
            PubKey m_pk;
        };

        Amount m_Stake;
    };

    struct BridgeOpSave
    {
        struct Key {
            uint8_t m_Tag;
            PubKey m_pk;
        };

        Height m_Height;
    };

    typedef L2Tst1_L2::Cookie Cookie;

    namespace Method
    {
        struct Create
        {
            static const uint32_t s_iMethod = 0;

            Upgradable3::Settings m_Upgradable;
            Settings m_Settings;

            uint32_t m_Validators;
            // followed by array of Validator
        };

        struct UserStake
        {
            static const uint32_t s_iMethod = 3;

            PubKey m_pkUser;
            Amount m_Amount;
        };

        typedef L2Tst1_L2::Method::BridgeOp BridgeOp;

        struct BridgeExport
            :public BridgeOp
        {
            static const uint32_t s_iMethod = 4;
        };

        struct BridgeImport
            :public BridgeOp
        {
            static const uint32_t s_iMethod = 5;

            uint32_t m_ApproveMask;
        };
    }

    namespace Msg
    {
        // comm with the validators
        static const uint8_t s_ProtoVer = 1;
        static const Height s_dh = 120;

        struct Base
        {
            uint8_t m_OpCode;
        };

#define L2Tst1_Msg_GetNonce(macro) \
            macro(uint8_t, ProtoVer) \
            macro(PubKey, pkOwner) \
            macro(PubKey, pkBbs)

#define L2Tst1_Msg_Nonce(macro) \
            macro(Secp_point_data, m_Nonce)

#define L2Tst1_Msg_GetSignature(macro) \
            macro(PubKey, pkOwner) \
            macro(Cookie, Cookie) \
            macro(uint32_t, nApproveMask) \
            macro(Secp_point_data, Commitment) \
            macro(Secp_point_data, TotalNonce) \
            macro(Height, hMin) \
            macro(uint32_t, nCharge)

#define L2Tst1_Msg_Signature(macro) \
            macro(Secp_scalar_data, k)

#define L2Tst1_Msgs_ToValidator(macro) \
            macro(1, GetNonce) \
            macro(3, GetSignature) \

#define L2Tst1_Msgs_FromValidator(macro) \
            macro(2, Nonce) \
            macro(4, Signature)

#define MACRO_PARDECL(type, name) type m_##name;
#define THE_MACRO(opcode, name) \
        struct name \
            :public Base \
        { \
            static const uint8_t s_OpCode = opcode; \
            L2Tst1_Msg_##name(MACRO_PARDECL) \
        };

        L2Tst1_Msgs_ToValidator(THE_MACRO)
        L2Tst1_Msgs_FromValidator(THE_MACRO)

#undef MACRO_PARDECL
#undef THE_MACRO
    }

#pragma pack (pop)

} // namespace L2Tst1_L1
