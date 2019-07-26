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

    class SwapOffersMonitor : public FlyClient::IBbsReceiver
    {

    public:
        SwapOffersMonitor(FlyClient::INetwork& network, IWalletObserver& observer, IWalletMessageEndpoint& messageEndpoint)
            : m_observer(observer),
              m_messageEndpoint(messageEndpoint)
        {
            network.BbsSubscribe(OffersBbsChannel, m_lastTimestamp, this);

            // test on dummy data
            for (uint8_t i = 0; i < 10; ++i) {

                TxID id {0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,i};
                TxDescription txDesc;

                txDesc.m_txId = id;
                txDesc.m_txType = wallet::TxType::Simple;
                txDesc.m_amount = i*11;
                txDesc.m_fee=0;
                txDesc.m_change=0;
                txDesc.m_minHeight=0;
                txDesc.m_peerId = Zero;
                txDesc.m_myId = Zero;
                txDesc.m_message = {'m','e','s','s','a','g','e'};
                txDesc.m_createTime=0;
                txDesc.m_modifyTime=0;
                txDesc.m_sender=false;
                txDesc.m_selfTx = false;
                txDesc.m_status=TxStatus::Pending;
                txDesc.m_kernelID = Zero;
                txDesc.m_failureReason = TxFailureReason::Unknown;

                m_offersCache.emplace(std::make_pair(id,txDesc));
            };
            
        };

        virtual void OnMsg(proto::BbsMsg&& msg) override
        {
            if (msg.m_Message.empty())
                return;

            Blob blob;

            uint8_t* pMsg = &msg.m_Message.front();
            uint32_t nSize = static_cast<uint32_t>(msg.m_Message.size());

            // SetTxParameter deserializedObj;
            TxID deserializedObj;
            TxDescription tx;

            try
            {
                Deserializer der;
                der.reset(pMsg, nSize);
                der & deserializedObj;

                tx.m_txId = deserializedObj;
                m_offersCache[tx.m_txId] = tx;

                m_observer.onSwapOffersChanged(ChangeAction::Added, std::vector<TxDescription>{tx});
            }
            catch (const std::exception&)
            {
                LOG_WARNING() << "not crypted BBS deserialization failed";
            }
        };

        auto getOffersList() const -> std::vector<TxDescription>
        {
            std::vector<TxDescription> offers;

            for (auto offer : m_offersCache)
            {
                offers.push_back(offer.second);
            }

            return offers;
        };

        void sendOffer(TxID id)
        {
            WalletID wId;
            ByteBuffer msg;

            wId.m_Channel = OffersBbsChannel;

            for (auto byte : id)
            {
                msg.push_back(byte);
            }
            
            m_messageEndpoint.SendEncryptedMessage(wId, msg);
        }

        // implement smart cache to prevent load of all mesages each time

    private:
        static constexpr BbsChannel OffersBbsChannel = proto::Bbs::s_MaxChannels - 1;
        Timestamp m_lastTimestamp = 0;  /// for test

        IWalletObserver& m_observer;
        IWalletMessageEndpoint& m_messageEndpoint;

        std::unordered_map<TxID, TxDescription> m_offersCache;

    };

} // namespace beam::wallet
