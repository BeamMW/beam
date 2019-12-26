// Copyright 2019 The Beam Team
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

#include "wallet/core/wallet.h"
#include "utility/logger.h"

using namespace beam::proto;

namespace beam::wallet
{
    /**
     *  Message item broadcasted using NewsChannels
     */
    struct NewsMessage
    {
        std::string m_content;
        SERIALIZE(m_content);
    };

    /**
     *  Interface for news channels observers. 
     */
    struct INewsObserver
    {
        virtual void onNewsUpdate(NewsMessage msg) = 0;
    };

    /**
     *  Implementation of public news channels reader via bulletin board system (BBS).
     */
    class NewsEndpoint
        : public FlyClient::IBbsReceiver
    {
    public:
        NewsEndpoint(FlyClient::INetwork& network);

        /**
         *  FlyClient::IBbsReceiver implementation
         *  Executed to process BBS messages received on subscribed channels
         */
        virtual void OnMsg(proto::BbsMsg&& msg) override;
        
        // INewsObserver interface
        void Subscribe(INewsObserver* observer);
        void Unsubscribe(INewsObserver* observer);

        void setPublicKeys(std::vector<PeerID> keys);

        static constexpr BbsChannel BbsChannelsOffset = 1024u;

    private:
		FlyClient::INetwork& m_network;                     /// source of incoming BBS messages
        std::vector<INewsObserver*> m_subscribers;          /// fresh news subscribers
        std::vector<PeerID> m_publicKeys;                      /// publisher keys

        static const std::set<BbsChannel> m_channels;
        static constexpr uint8_t MsgType = 1;
        static constexpr uint8_t m_protocolVersion = 1;
        Timestamp m_lastTimestamp = getTimestamp() - 12*60*60;

        void verifyPublisher(std::function<bool(PeerID)> signPredicate);
        void notifySubscribers(NewsMessage msg) const;
    };

} // namespace beam::wallet
