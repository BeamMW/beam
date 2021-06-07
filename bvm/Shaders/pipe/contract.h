#pragma once
#include "../BeamDifficulty.h"

namespace Pipe
{
#pragma pack (push, 1) // the following structures will be stored in the node in binary form

    static const ShaderID s_SID = { 0x0e,0x62,0x05,0x20,0xda,0x78,0xe9,0x87,0xe0,0x05,0x9b,0x8d,0x30,0xe4,0x75,0x9e,0x36,0x67,0x7d,0xdd,0x3d,0x2d,0xad,0xfb,0xa4,0xff,0x5c,0x0c,0x85,0xdc,0x26,0xff };

    struct Cfg
    {
        struct Out {
            uint32_t m_CheckpointMaxMsgs;
            uint32_t m_CheckpointMaxDH;
        } m_Out;

        struct In {
            HashValue m_RulesRemote;
            Amount m_ComissionPerMsg;
            Amount m_StakeForRemote;
            Height m_hDisputePeriod;
            Height m_hContenderWaitPeriod;
            uint8_t m_FakePoW; // for tests
        } m_In;
    };

    struct Create
    {
        static const uint32_t s_iMethod = 0;
        Cfg m_Cfg;
    };

    struct SetRemote
    {
        static const uint32_t s_iMethod = 2;
        ContractID m_cid;
    };

    struct PushLocal0
    {
        static const uint32_t s_iMethod = 3;

        ContractID m_Receiver;
        uint32_t m_MsgSize;
        // followed by the message
    };

    struct PushRemote0
    {
        static const uint32_t s_iMethod = 4;

        PubKey m_User;

        struct Flags {
            static const uint8_t Msgs = 1; // has being-transferred message hashes
            static const uint8_t Hdr0 = 2; // has root header and proof of the checkpoint
            static const uint8_t HdrsUp = 4; // has new headers above hdr0
            static const uint8_t HdrsDown = 8; // has new headers below hdr0
            static const uint8_t Reset = 0x10; // drop all the current headers. Needed if attacker deliberately added fake header on top of the honest chain
        };

        uint8_t m_Flags;

        // followed by message variable data
    };

    struct FinalyzeRemote
    {
        static const uint32_t s_iMethod = 5;
        uint8_t m_DepositStake; // instead of immediate withdraw. Can be used by other user to enforce checkpoint finalization
    };

    struct ReadRemote0
    {
        static const uint32_t s_iMethod = 6;

        uint32_t m_iCheckpoint;
        uint32_t m_iMsg;
        uint32_t m_MsgSize; // on input: buf size, on output: actual msg size. The msg would be truncated if necessary
        uint8_t m_Wipe; // wipe the message after verification. Allowed only for private messages (i.e. sent specifically to the caller contract)
        // out
        uint8_t m_IsPrivate;
        ContractID m_Sender;
        // followed by the message bufer
    };

    struct Withdraw
    {
        static const uint32_t s_iMethod = 7;

        PubKey m_User;
        Amount m_Amount;
    };

    struct KeyType
    {
        static const uint8_t OutMsg = 1;
        static const uint8_t OutCheckpoint = 2;
        static const uint8_t UserInfo = 3;
        static const uint8_t VariantHdr = 5;
        static const uint8_t InpCheckpoint = 6;
        static const uint8_t Variant = 7;
        static const uint8_t StateIn = 8;
        static const uint8_t StateOut = 9;
        static const uint8_t InMsg = 10;
    };


    struct MsgHdr
    {
        struct KeyBase {
            uint8_t m_Type;
            // big-endian, for simpler enumeration by app shader
            uint32_t m_iCheckpoint_BE;
            uint32_t m_iMsg_BE;
        };

        struct KeyOut :public KeyBase {
            KeyOut() { m_Type = KeyType::OutMsg; }
        };

        struct KeyIn :public KeyBase {
            KeyIn() { m_Type = KeyType::InMsg; }
        };

        ContractID m_Sender;
        ContractID m_Receiver; // zero if no sender, would be visible for everyone

        template <typename THashProcessor>
        void UpdateStateEx(THashProcessor& hp, HashValue& res, uint32_t nMsg) const
        {
            hp
                << "b.msg"
                << res
                << nMsg;

            hp.Write(this, nMsg);

            hp >> res;
        }

        void UpdateState(HashValue& res, uint32_t nMsg) const
        {
            HashProcessor::Sha256 hp;
            UpdateStateEx(hp, res, nMsg);
        }
    };

    struct InpCheckpoint
    {
        struct Key
        {
            uint8_t m_Type = KeyType::InpCheckpoint;
            uint32_t m_iCheckpoint_BE;
        };

        PubKey m_User;
    };


    struct OutCheckpoint
    {
        struct Key
        {
            uint8_t m_Type = KeyType::OutCheckpoint;
            uint32_t m_iCheckpoint_BE;
        };

        typedef HashValue ValueType;
    };

    struct StateOut
    {
        struct Key
        {
            uint8_t m_Type = KeyType::StateOut;
        };

        Cfg::Out m_Cfg;

        struct Checkpoint {
            uint32_t m_iIdx;
            uint32_t m_iMsg;
            Height m_h0;
        } m_Checkpoint;

        bool IsCheckpointClosed(Height h) const
        {
            return (m_Checkpoint.m_iMsg == m_Cfg.m_CheckpointMaxMsgs) || (h - m_Checkpoint.m_h0 >= m_Cfg.m_CheckpointMaxDH);
        }
    };

    struct StateIn
    {
        struct Key
        {
            uint8_t m_Type = KeyType::StateIn;
        };

        Cfg::In m_Cfg;
        ContractID m_cidRemote;

        struct Dispute
        {
            uint32_t m_iIdx;
            Height m_Height;
            Amount m_Stake;
            HashValue m_hvBestVariant;
            uint32_t m_Variants;
        } m_Dispute;

        bool CanFinalyze(Height h) const
        {
            return h - m_Dispute.m_Height >= m_Cfg.m_hDisputePeriod;
        }
    };

    struct Variant
    {
        struct Key
        {
            uint8_t m_Type = KeyType::Variant;
            HashValue m_hvVariant;
        };

        Height m_hLastLoose;
        uint32_t m_iDispute;

        struct Ending {
            Height m_Height;
            BeamDifficulty::Raw m_Work;
            HashValue m_hvPrev;
        };

        Ending m_Begin;
        Ending m_End;

        PubKey m_User;
    };

    struct UserInfo
    {
        struct Key
        {
            uint8_t m_Type = KeyType::UserInfo;
            PubKey m_Pk;
        };

        Amount m_Balance;
    };

    struct VariantHdr
    {
        struct Key
        {
            uint8_t m_Type = KeyType::VariantHdr;
            HashValue m_hvVariant;
            Height m_Height;
        };

        HashValue m_hvHeader;
    };

#pragma pack (pop)

}
