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
    using namespace wallet;

const std::vector<BbsChannel> BroadcastRouter::m_incomingBbsChannels =
{
    proto::Bbs::s_SwapOffersChannel,     // can be renamed to s_SwapOffersChannel after fork 2
    proto::Bbs::s_BroadcastChannel,
    proto::Bbs::s_DexOffersChannel
};

const std::map<BroadcastContentType, BbsChannel> BroadcastRouter::m_outgoingBbsChannelsMap =
{
    { BroadcastContentType::SwapOffers, proto::Bbs::s_SwapOffersChannel },
    { BroadcastContentType::SoftwareUpdates, proto::Bbs::s_BroadcastChannel },
    { BroadcastContentType::ExchangeRates, proto::Bbs::s_BroadcastChannel },
    { BroadcastContentType::WalletUpdates, proto::Bbs::s_BroadcastChannel },
    { BroadcastContentType::DexOffers, proto::Bbs::s_DexOffersChannel },
    { BroadcastContentType::AssetVerification, proto::Bbs::s_BroadcastChannel }
};

const std::map<BroadcastContentType, MsgType> BroadcastRouter::m_messageTypeMap =
{
    {BroadcastContentType::SwapOffers,         MsgType(0)},
    {BroadcastContentType::SoftwareUpdates,    MsgType(1)},
    {BroadcastContentType::ExchangeRates,      MsgType(2)},
    {BroadcastContentType::WalletUpdates,      MsgType(3)},
    {BroadcastContentType::DexOffers,          MsgType(4)},
    {BroadcastContentType::AssetVerification,  MsgType(5)},
};

/**
 *  Get protocol MsgType for specified content type
 */
MsgType BroadcastRouter::getMsgType(BroadcastContentType type)
{
    auto it = m_messageTypeMap.find(type);
    assert(it != std::cend(m_messageTypeMap));

    return it->second;
}

/**
 *  Get BBS channel number to route outgoing message
 */
BbsChannel BroadcastRouter::getBbsChannel(BroadcastContentType type)
{
    auto it = m_outgoingBbsChannelsMap.find(type);
    assert(it != std::cend(m_outgoingBbsChannelsMap));

    return it->second;
}

BroadcastRouter::BbsTsHolder::BbsTsHolder(wallet::IWalletDB::Ptr db)
    : wallet::TimestampHolder(db, "BroadcastBbsTimestamps")
{
}

/**
 *  @bbsNetwork         used as incoming broadcast messages source
 *  @bbsEndpoint        transport for outgoing broadcast messages
 */
BroadcastRouter::BroadcastRouter(proto::FlyClient::INetwork::Ptr bbsNetwork, IWalletMessageEndpoint& bbsEndpoint, ITimestampHolder::Ptr timestampHolder)
    : BbsProcessor(bbsNetwork, timestampHolder)
    , m_bbsMessageEndpoint(bbsEndpoint)
    , m_protocol(m_ver_2[0],
                 m_ver_2[1],
                 m_ver_2[2],
                 m_maxMessageTypes,
                 *this,
                 MsgHeader::SIZE+1)
    , m_msgReader(m_protocol,
                  0,                    // uint64_t streamId is not used here
                  m_defaultMessageSize)
{
    m_msgReader.disable_all_msg_types();

    for (const auto& ch : m_incomingBbsChannels)
    {
        SubscribeChannel(ch);
    }
}

BroadcastRouter::~BroadcastRouter()
{
    for (const auto& ch : m_incomingBbsChannels)
    {
        UnsubscribeChannel(ch);
    }
}

/**
 *  Only one listener of each type can be registered.
 */
void BroadcastRouter::registerListener(BroadcastContentType type, IBroadcastListener* listener)
{
    assert(m_listeners.find(type) == std::cend(m_listeners));
    m_listeners[type] = listener;

    auto msgType = getMsgType(type);

    m_protocol.add_message_handler
        < IBroadcastListener,
          BroadcastMsg,
          &IBroadcastListener::onMessage >
        (msgType, listener, m_minMessageSize, m_maxMessageSize);

    m_msgReader.enable_msg_type(msgType);
}

void BroadcastRouter::unregisterListener(BroadcastContentType type)
{
    auto it = m_listeners.find(type);
    assert(it != std::cend(m_listeners));
    m_listeners.erase(it);

    m_msgReader.disable_msg_type(getMsgType(type));
}

/**
 *  Deprecated method. Send without packing into common data object before serialization. Used in SwapOffersBoard.
 */
void BroadcastRouter::sendRawMessage(BroadcastContentType type, const ByteBuffer& msg)
{
    // Route to BBS channel
    wallet::WalletID dummyWId;
    dummyWId.m_Channel = getBbsChannel(type);
    m_bbsMessageEndpoint.SendRawMessage(dummyWId, msg);
}

/**
 *  Send broadcast message.
 *  Message will be dispatched according to content type.
 */
void BroadcastRouter::sendMessage(BroadcastContentType type, const BroadcastMsg& msg)
{
    ByteBuffer content = wallet::toByteBuffer(msg);
    size_t packSize = MsgHeader::SIZE + content.size();
    assert(packSize <= proto::Bbs::s_MaxMsgSize);

    // Prepare Protocol header
    ByteBuffer packet(packSize);
    MsgHeader header(m_ver_2[0], m_ver_2[1], m_ver_2[2]);

    
    header.type = getMsgType(type);
    header.size = static_cast<uint32_t>(content.size());
    header.write(packet.data());

    std::copy(std::begin(content),
              std::end(content),
              std::begin(packet) + MsgHeader::SIZE);

    sendRawMessage(type, packet);
}

/**
 *  Dispatches BBS message data to MsgReader.
 *  MsgReader use Protocol to process data and finally passes to the message handler.
 */
void BroadcastRouter::OnMsg(const proto::BbsMsg& bbsMsg)
{
    const void * data = bbsMsg.m_Message.data();
    size_t size = bbsMsg.m_Message.size();

    // Here MsgReader used in stateless mode. State from previous message can cause error.
    m_msgReader.reset();
    m_msgReader.new_data_from_stream(io::EC_OK, data, size);
}

/// unused
void BroadcastRouter::on_protocol_error(uint64_t fromStream, ProtocolError error)
{
     /*std::string description;
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
     BEAM_LOG_DEBUG() << "BroadcastRouter protocol: " << description;*/
}

/// unused
void BroadcastRouter::on_connection_error(uint64_t fromStream, io::ErrorCode errorCode) {}

} // namespace beam
