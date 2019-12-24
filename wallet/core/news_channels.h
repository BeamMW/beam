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

namespace beam::wallet
{
    struct NewsMessage
    {
        std::string m_content;
    };

    /**
     *  Implementation of public news channels reader via bbs.
     */
    class NewsChannelsReader
        : public FlyClient::IBbsReceiver
    {
    public:
        NewsChannelsReader(FlyClient::INetwork& network);

        /**
         *  FlyClient::IBbsReceiver implementation
         *  Executed to process BBS messages received on subscribed channels
         */
        virtual void OnMsg(proto::BbsMsg&& msg) override;
        
        void Subscribe(INewsObserver* observer);
        void Unsubscribe(INewsObserver* observer);

    private:
		FlyClient::INetwork& m_network;                     /// source of incoming BBS messages
        std::vector<INewsObserver*> m_subscribers;          /// used to notify subscribers about offers changes
        std::vector<PeerID> m_publicKeys;                   /// publisher keys

        static const std::set<BbsChannel> m_channels;
        static constexpr BbsChannel BbsChannelsOffset = 1024u;
        static constexpr uint8_t MsgType = 1;
        static constexpr uint8_t m_protocolVersion = 1;
        Timestamp m_lastTimestamp = getTimestamp() - 12*60*60;

        void notifySubscribers(/**/) const;
    };

} // namespace beam::wallet
