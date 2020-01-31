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

#include "news_message.h"
#include "news_observer.h"
#include "newscast_protocol_parser.h"

#include "wallet/client/extensions/broadcast_router.h"

using namespace beam::proto;

namespace beam::wallet
{
    /**
     *  Implementation of public news channels reader via bulletin board system (BBS).
     */
    class Newscast
        : public IBroadcastListener
    {
    public:
        Newscast(BroadcastRouter&, NewscastProtocolParser&);

        /**
         *  IBroadcastListener implementation
         *  Processes broadcast messages
         */
        virtual bool onMessage(uint64_t unused, ByteBuffer&&) override;
        
        // INewsObserver interface
        void Subscribe(INewsObserver* observer);
        void Unsubscribe(INewsObserver* observer);

        static constexpr BbsChannel BbsChannelsOffset = Bbs::s_MaxWalletChannels + 1024u;
        
    private:
		BroadcastRouter& m_broadcastRouter;                 /// source of incoming messages
        NewscastProtocolParser& m_parser;                   /// news protocol parser
        std::vector<INewsObserver*> m_subscribers;          /// fresh news subscribers

        void notifySubscribers(NewsMessage msg) const;
    };

} // namespace beam::wallet
