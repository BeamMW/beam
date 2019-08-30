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

#include "wallet/wallet.h"
#include "utility/logger.h"
#include "utility/std_extension.h"

#include <unordered_map>

namespace beam::wallet
{
    using namespace beam::proto;

    /**
     *  Implementation of public swap offers bulletin board using not crypted BBS broadcasting.
     */
    class SwapOffersBoard : public FlyClient::IBbsReceiver
    {
        
    public:
        SwapOffersBoard(FlyClient::INetwork& network, IWalletObserver &observer, IWalletMessageEndpoint& messageEndpoint);

        virtual void OnMsg(proto::BbsMsg&& msg) override;           /// FlyClient::IBbsReceiver implementation

        void subscribe(AtomicSwapCoin coinType);

        auto getOffersList() const -> std::vector<SwapOffer>;
        void publishOffer(const SwapOffer& offer) const;

    private:
		FlyClient::INetwork& m_network;
        IWalletObserver& m_observer;
        IWalletMessageEndpoint& m_messageEndpoint;

        static const std::map<AtomicSwapCoin, BbsChannel> m_channelsMap;
        Timestamp m_lastTimestamp = getTimestamp() - 12*60*60;
        boost::optional<BbsChannel> m_activeChannel;

        auto getChannel(const SwapOffer& offer) const -> boost::optional<BbsChannel>;

        std::unordered_map<TxID, SwapOffer> m_offersCache;
    };

} // namespace beam::wallet
