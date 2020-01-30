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

#include "broadcast_router.h"

#include "utility/logger.h"

namespace beam
{

BroadcastRouter::BroadcastRouter(proto::FlyClient::INetwork& bbsNetwork)
    : m_bbsNetwork(bbsNetwork)
    , m_protocol(m_protocol_version_0,
                 m_protocol_version_1,
                 m_protocol_version_2,
                 m_maxMessageTypes,
                 *this,
                 0)     // TODO: serializer is not used
    , m_msgReader(m_protocol,
                  0,    // uint64_t streamId
                  m_defaultMessageSize)
    , m_lastTimestamp(getTimestamp() - 12*60*60)
{
    m_msgReader.disable_all_msg_types();
}

/// TODO review BBS channels mapping before fork
const std::map<BroadcastRouter::ContentType, std::vector<BbsChannel>> BroadcastRouter::m_bbsChannelsMapping =
{
    { BroadcastRouter::ContentType::SwapOffers,         { proto::Bbs::s_MaxWalletChannels,
                                                                  proto::Bbs::s_MaxWalletChannels + 1,
                                                                  proto::Bbs::s_MaxWalletChannels + 2 } },
    { BroadcastRouter::ContentType::SoftwareUpdates,    { proto::Bbs::s_MaxWalletChannels + 1024u } },
    { BroadcastRouter::ContentType::ExchangeRates,      { proto::Bbs::s_MaxWalletChannels + 1024u } }
};

const std::map<BroadcastRouter::ContentType, MsgType> BroadcastRouter::m_messageTypeMapping =
{
    { BroadcastRouter::ContentType::SwapOffers, 0 },
    { BroadcastRouter::ContentType::SoftwareUpdates, 1 },
    { BroadcastRouter::ContentType::ExchangeRates, 1 },
};

/**
 *  ContentType used like key because Protocol has only ability to add message handler,
 *  but MsgReader able both enable and disable message types.
 */
void BroadcastRouter::registerListener(ContentType type, IBroadcastListener* listener)
{
    auto it = m_listeners.find(type);
    if (it != std::cend(m_listeners)) return;

    auto msgType = m_messageTypeMapping.find(type);
    assert(msgType != std::cend(m_messageTypeMapping));

    m_protocol.add_message_handler< IBroadcastListener,
                                    ByteBuffer,
                                    &IBroadcastListener::onMessage >
                                    (*msgType, listener, 0, 1024*1024*10);
    m_msgReader.enable_msg_type(*msgType);
    
    for (BbsChannel channel : m_bbsChannelsMapping[type])
    {
        m_bbsNetwork.BbsSubscribe(channel, m_lastTimestamp, this);
    }

    m_listeners[type] = listener;
}

void BroadcastRouter::unregisterListener(ContentType type)
{
    auto it = m_listeners.find(type);
    if (it == std::cend(m_listeners)) return;

    auto msgType = m_messageTypeMapping.find(type);
    assert(msgType != std::cend(m_messageTypeMapping));

    m_msgReader.disable_msg_type(*msgType);

    m_listeners.erase(it);
}

/**
 *  Dispatches BBS message data to MsgReader.
 *  MsgReader use Protocol to process data and finally passes to the message handler.
 */
void BroadcastRouter::OnMsg(proto::BbsMsg&& bbsMsg) override
{
    const void * data = bbsMsg.m_Message.data();
    size_t size = bbsMsg.m_Message.size();

    m_msgReader.new_data_from_stream(EC_OK, data, size);
}

virtual void BroadcastRouter::on_protocol_error(uint64_t fromStream, ProtocolError error) override
{
    std::string description; 
    switch (error)
    {
        case ProtocolError::no_error:
            description = "ok";
            break;

        case ProtocolError::version_error:
            description = "wrong protocol version (first 3 bytes)"
            break;

        case ProtocolError::msg_type_error:
            description = "msg type is not handled by this protocol"
            break;

        case ProtocolError::msg_size_error:
            description = "msg size out of allowed range"
            break;

        case ProtocolError::message_corrupted:
            description = "deserialization error"
            break;

        case ProtocolError::unexpected_msg_type:
            description = "receiving of msg type disabled for this stream"
            break;
        
        default:
            description = "receiving of msg type disabled for this stream"
            break;
    }
    LOG_WARNING() << "BbsMessagesRouter error: " << description;
}

virtual void on_connection_error(uint64_t fromStream, io::ErrorCode errorCode) override
{   /// unused
}


} // namespace beam
