// Copyright 2020 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "p2p/protocol.h"
#include "p2p/msg_reader.h"

#include "core/block_crypt.h"   // BbsChannel
#include "core/fly_client.h"    // INetwork
#include "wallet/core/wallet.h" // IWalletMessageEndpoint
#include "wallet/core/wallet_network.h"

#include "interface.h"

namespace beam
{
    /**
     *  Dispatches broadcast messages between network and listeners.
     *  Current implementation uses the specified scope of BBS channels as a tunnel for messages.
     *  Encapsulates transport protocol.
     */
    class BroadcastRouter
        : public IBroadcastMsgGateway
        , IErrorHandler  // Error handling for Protocol
        , wallet::BbsProcessor
    {
    public:

        struct BbsTsHolder : public wallet::TimestampHolder
        {
            BbsTsHolder(wallet::IWalletDB::Ptr db);
        };

        BroadcastRouter(proto::FlyClient::INetwork::Ptr, wallet::IWalletMessageEndpoint&, wallet::ITimestampHolder::Ptr);
        virtual ~BroadcastRouter();

        // IBroadcastMsgGateway
        void registerListener(BroadcastContentType, IBroadcastListener*) override;
        void unregisterListener(BroadcastContentType) override;
        void sendMessage(BroadcastContentType type, const BroadcastMsg&) override;

        // BbsProcessor
        void OnMsg(const proto::BbsMsg&) override;

        // IErrorHandler
        void on_protocol_error(uint64_t fromStream, ProtocolError error) override;
        void on_connection_error(uint64_t fromStream, io::ErrorCode errorCode) override; /// unused

        static constexpr std::array<uint8_t, 3> m_ver_2 = { 0, 0, 2 };  // verison after 2nd fork: will has common deserialization and signatures type for all BBS-based broadcasting.
    protected:
        void sendRawMessage(BroadcastContentType type, const ByteBuffer&) override;
    private:
        static constexpr size_t m_maxMessageTypes = 6;
        static constexpr size_t m_defaultMessageSize = 200;         // set experimentally
        static constexpr size_t m_minMessageSize = 1;
        static constexpr size_t m_maxMessageSize = 1024*1024*10;

        static const std::vector<BbsChannel> m_incomingBbsChannels;
        static const std::map<BroadcastContentType, BbsChannel> m_outgoingBbsChannelsMap;
        static const std::map<BroadcastContentType, MsgType> m_messageTypeMap;

        MsgType getMsgType(BroadcastContentType);
        BbsChannel getBbsChannel(BroadcastContentType);

        wallet::IWalletMessageEndpoint& m_bbsMessageEndpoint;

        Protocol m_protocol;
        MsgReader m_msgReader;
        std::map<BroadcastContentType, IBroadcastListener*> m_listeners;
    };
} // namespace beam
