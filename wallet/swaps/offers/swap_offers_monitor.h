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

#include "wallet/common.h"
#include "core/fly_client.h"
#include "wallet/wallet.h"
#include "utility/logger.h"

#include <unordered_map>

namespace beam::wallet
{
    using namespace beam::proto;

    using SwapOffer = TxDescription;

    class SwapOffersMonitor : public FlyClient::IBbsReceiver
    {
        
    public:
        SwapOffersMonitor(FlyClient::INetwork& network, IWalletObserver& observer, IWalletMessageEndpoint& messageEndpoint)
            : m_observer(observer),
              m_messageEndpoint(messageEndpoint)
        {
            network.BbsSubscribe(OffersBbsChannel, m_lastTimestamp, this);         
        };

        virtual void OnMsg(proto::BbsMsg&& msg) override
        {
            if (msg.m_Message.empty())
                return;

            SwapOffer offer;

            if(fromByteBuffer(msg.m_Message, offer))
            {
                m_offersCache[offer.m_txId] = offer;
                m_observer.onSwapOffersChanged(ChangeAction::Added, std::vector<SwapOffer>{offer});
            }
            else
            {
                LOG_WARNING() << "not crypted BBS deserialization failed";
            }
        };

        auto getOffersList() const -> std::vector<SwapOffer>
        {
            std::vector<SwapOffer> offers;

            for (auto offer : m_offersCache)
            {
                offers.push_back(offer.second);
            }

            return offers;
        };

        void sendSwapOffer(const SwapOffer&& offer)
        {            
            WalletID wId;
            wId.m_Channel = OffersBbsChannel;
            m_messageEndpoint.SendEncryptedMessage(wId, toByteBuffer(offer));
        }

        // implement smart cache to prevent load of all messages each time

    private:
        static constexpr BbsChannel OffersBbsChannel = proto::Bbs::s_MaxChannels - 1;
        Timestamp m_lastTimestamp = getTimestamp() - 60*60;

        IWalletObserver& m_observer;
        IWalletMessageEndpoint& m_messageEndpoint;

        std::unordered_map<TxID, SwapOffer> m_offersCache;

    };

} // namespace beam::wallet
