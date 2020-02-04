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

/// TODO review BBS channels mapping before next fork
const std::vector<BbsChannel> BroadcastRouter::m_bbsChannelsList =
{
    proto::Bbs::s_BtcSwapOffersChannel,
    proto::Bbs::s_LtcSwapOffersChannel,
    proto::Bbs::s_QtumSwapOffersChannel,
    proto::Bbs::s_BroadcastChannel
};

const std::map<BroadcastRouter::ContentType, MsgType> BroadcastRouter::m_messageTypeMapping =
{
    { BroadcastRouter::ContentType::SwapOffers, MsgType(0) },
    { BroadcastRouter::ContentType::SoftwareUpdates, MsgType(1) },
    { BroadcastRouter::ContentType::ExchangeRates, MsgType(2) }
};

MsgType BroadcastRouter::getMsgType(ContentType type)
{
    auto it = m_messageTypeMapping.find(type);
    assert(it != std::cend(m_messageTypeMapping));

    return it->second;
}

BroadcastRouter::BroadcastRouter(proto::FlyClient::INetwork& bbsNetwork)
    : m_bbsNetwork(bbsNetwork)
    , m_protocol(m_protocol_version_0,
                 m_protocol_version_1,
                 m_protocol_version_2,
                 m_maxMessageTypes,
                 *this,
                 MsgHeader::SIZE+1)     // TODO: serializer is not used
    , m_msgReader(m_protocol,
                  0,                    // uint64_t streamId
                  m_defaultMessageSize)
    , m_lastTimestamp(getTimestamp() - m_bbsTimeWindow)
{
    m_msgReader.disable_all_msg_types();
    for (const auto& channel : m_bbsChannelsList)
    {
        m_bbsNetwork.BbsSubscribe(channel, m_lastTimestamp, this);
    }
}

/**
 *  Only one listener of each type can be registered.
 */
void BroadcastRouter::registerListener(ContentType type, IBroadcastListener* listener)
{
    auto it = m_listeners.find(type);
    assert(it == std::cend(m_listeners));
    m_listeners[type] = listener;

    auto msgType = getMsgType(type);

    // For SwapOffer and Broadcast MsgReader serializer is not used.
    // Otherwise Router will need to know about top layer abstractions.
    m_protocol.add_message_handler_wo_deserializer
        < IBroadcastListener,
          &IBroadcastListener::onMessage >
        (msgType, listener, m_minMessageSize, m_maxMessageSize);

    m_msgReader.enable_msg_type(msgType);
}

void BroadcastRouter::unregisterListener(ContentType type)
{
    auto it = m_listeners.find(type);
    assert(it != std::cend(m_listeners));
    m_listeners.erase(it);

    m_msgReader.disable_msg_type(getMsgType(type));
}

/**
 *  Dispatches BBS message data to MsgReader.
 *  MsgReader use Protocol to process data and finally passes to the message handler.
 */
void BroadcastRouter::OnMsg(proto::BbsMsg&& bbsMsg)
{
    const void * data = bbsMsg.m_Message.data();
    size_t size = bbsMsg.m_Message.size();

    m_msgReader.reset();    // Here MsgReader used in stateless mode. State from previous message can cause error.
    m_msgReader.new_data_from_stream(io::EC_OK, data, size);
}

void BroadcastRouter::on_protocol_error(uint64_t fromStream, ProtocolError error)
{
    std::string description; 
    switch (error)
    {
        case ProtocolError::no_error:
            description = "ok";
            break;

        case ProtocolError::version_error:
            description = "wrong protocol version (first 3 bytes)";
            break;

        case ProtocolError::msg_type_error:
            description = "msg type is not handled by this protocol";
            break;

        case ProtocolError::msg_size_error:
            description = "msg size out of allowed range";
            break;

        case ProtocolError::message_corrupted:
            description = "deserialization error";
            break;

        case ProtocolError::unexpected_msg_type:
            description = "receiving of msg type disabled for this stream";
            break;
        
        default:
            description = "receiving of msg type disabled for this stream";
            break;
    }
    LOG_WARNING() << "BbsMessagesRouter error: " << description;
}

/// unused
void BroadcastRouter::on_connection_error(uint64_t fromStream, io::ErrorCode errorCode)
{
}

} // namespace beam
