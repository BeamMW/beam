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

#include "interface.h"

namespace beam
{
    class BroadcastRouter
        : public IBroadcastMsgsGateway
        , IErrorHandler  // Error handling for Protocol
        , proto::FlyClient::IBbsReceiver
    {
    public:
        BroadcastRouter(proto::FlyClient::INetwork&, wallet::IWalletMessageEndpoint&);

        // IBroadcastMsgsGateway
        void registerListener(BroadcastContentType, IBroadcastListener*) override;
        void unregisterListener(BroadcastContentType) override;
        void sendRawMessage(BroadcastContentType type, const ByteBuffer&) override; // deprecated. used in SwapOffersBoard.
        void sendMessage(BroadcastContentType type, const BroadcastMsg&) override;

        // IBbsReceiver
        virtual void OnMsg(proto::BbsMsg&&) override;

        // IErrorHandler
        virtual void on_protocol_error(uint64_t fromStream, ProtocolError error) override;
        virtual void on_connection_error(uint64_t fromStream, io::ErrorCode errorCode) override; /// unused

    private:
        static constexpr uint8_t m_protocol_version_0 = 0;
        static constexpr uint8_t m_protocol_version_1 = 0;
        static constexpr uint8_t m_protocol_version_2 = 1;
        static constexpr size_t m_maxMessageTypes = 3;
        static constexpr size_t m_defaultMessageSize = 200;         // TODO: experimentally check
        static constexpr size_t m_minMessageSize = 1;               // TODO: experimentally check
        static constexpr size_t m_maxMessageSize = 1024*1024*10;    // TODO: experimentally check
        static constexpr uint32_t m_bbsTimeWindow = 12*60*60;       // BBS message lifetime is 12 hours

        static const std::vector<BbsChannel> m_incomingBbsChannels;
        static const std::map<BroadcastContentType, BbsChannel> m_outgoingBbsChannelsMap;
        static const std::map<BroadcastContentType, MsgType> m_messageTypeMap;

        MsgType getMsgType(BroadcastContentType);
        BbsChannel getBbsChannel(BroadcastContentType);

        proto::FlyClient::INetwork& m_bbsNetwork;
        wallet::IWalletMessageEndpoint& m_bbsMessageEndpoint;

        // TODO: think about the creation of own MsgReader for each BBS channel

        Protocol m_protocol;
        MsgReader m_msgReader;
        Timestamp m_lastTimestamp;
        std::map<BroadcastContentType, IBroadcastListener*> m_listeners;
    };

} // namespace beam
