#pragma once

namespace Pipe
{
#pragma pack (push, 1) // the following structures will be stored in the node in binary form

    struct Cfg
    {
        HashValue m_RulesRemote;
        Amount m_StakeForRemote;
        Amount m_ComissionPerMsg;
        Height m_hCommitPeriod;
    };

    struct Create
    {
        static const uint32_t s_iMethod = 0;
        Cfg m_Cfg;
    };

    struct PushLocal0
    {
        static const uint32_t s_iMethod = 2;

        uint32_t m_MsgSize;
        // followed by the message
    };

    struct PushRemote0
    {
        static const uint32_t s_iMethod = 2;

        uint32_t m_Count;
        // followed by hashes
    };

    struct Global
    {
        Cfg m_Cfg;
    };

    struct OutState
    {
        HashValue m_Checksum;
        uint32_t m_Count;
    };

#pragma pack (pop)

}
