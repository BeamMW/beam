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

#include "broadcast_listener.h"

namespace beam
{
    class BroadcastRouter
        : public IErrorHandler  /// Error handling for Protocol
        , proto::FlyClient::IBbsReceiver
    {
    public:
        BroadcastRouter(proto::FlyClient::INetwork&);

        // needed to map Listeners and BBS channels
        enum class ContentType
        {
            SwapOffers,
            SoftwareUpdates,
            ExchangeRates
        };

        void registerListener(ContentType, IBroadcastListener*);
        void unregisterListener(ContentType);

        // IBbsReceiver
        virtual void OnMsg(proto::BbsMsg&&) override;

        // IErrorHandler
        virtual void on_protocol_error(uint64_t fromStream, ProtocolError error) override;
        virtual void on_connection_error(uint64_t fromStream, io::ErrorCode errorCode) override; /// unused

    private:
        static constexpr uint8_t m_protocol_version_0 = 0;
        static constexpr uint8_t m_protocol_version_1 = 0;
        static constexpr uint8_t m_protocol_version_2 = 1;
        static constexpr size_t m_maxMessageTypes = 2;
        static constexpr size_t m_defaultMessageSize = 200; // TODO: experimentally check

        /// replace with usual array
        static const std::map<ContentType, std::vector<BbsChannel>> m_bbsChannelsMapping;
        static const std::map<ContentType, MsgType> m_messageTypeMapping;

        std::vector<BbsChannel> getBbsChannels(ContentType);
        MsgType getMsgType(ContentType);

        proto::FlyClient::INetwork& m_bbsNetwork;

        // TODO: create own MsgReader for each listening BBS channel

        Protocol m_protocol;
        MsgReader m_msgReader;
        Timestamp m_lastTimestamp;
        std::map<ContentType, IBroadcastListener*> m_listeners;

    };

} // namespace beam
