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
#include "dex_board.h"
#include "wallet/transactions/dex/dex_tx.h"
#include "wallet/client/extensions/broadcast_gateway/broadcast_msg_creator.h"
#include "wallet/client/wallet_client.h"

namespace beam::wallet {

    DexBoard::DexBoard(IBroadcastMsgGateway& gateway, IWalletModelAsync::Ptr wallet, IWalletDB& wdb)
        : _gateway(gateway)
        , _wallet(std::move(wallet))
        , _wdb(wdb)
    {
        _gateway.registerListener(BroadcastContentType::DexOffers, this);
    }

    std::vector<AssetSwapOrder> DexBoard::getAssetSwapOrders() const
    {
        std::vector<AssetSwapOrder> result;
        result.reserve(_assetOrders.size());

        for (auto pair: _assetOrders)
        {
            result.push_back(pair.second);
        }

        return result;
    }

    boost::optional<AssetSwapOrder> DexBoard::getAssetSwapOrder(const DexOrderID& orderId) const
    {
        const auto it = _assetOrders.find(orderId);
        if (it != _assetOrders.end())
        {
            return it->second;
        }
        return boost::none;
    }

    void DexBoard::publishOrder(const AssetSwapOrder &offer)
    {
        auto message = createMessage(offer);
        _gateway.sendMessage(BroadcastContentType::DexOffers, message);
    }

    bool DexBoard::handleAssetSwap(const boost::optional<AssetSwapOrder>& order)
    {
        LOG_INFO() << "DexBoard oder message received";

        auto it = _assetOrders.find(order->getID());
        _assetOrders[order->getID()] = *order;

        if (it == _assetOrders.end())
        {
            notifyObservers(ChangeAction::Added, std::vector<AssetSwapOrder>{ *order });
        }
        else
        {
            notifyObservers(ChangeAction::Updated, std::vector<AssetSwapOrder>{ *order });
        }

        return true;
    }

    bool DexBoard::onMessage(uint64_t, BroadcastMsg&& msg)
    {
        auto order = parseAssetSwapMessage(msg);
        return order ? handleAssetSwap(order) : false;
    }

    BroadcastMsg DexBoard::createMessage(const AssetSwapOrder& order)
    {
        const auto pkdf = _wdb.get_SbbsKdf();

        auto sk = order.derivePrivateKey(pkdf);
        auto buffer = toByteBuffer(order);
        auto message = BroadcastMsgCreator::createSignedMessage(buffer, sk);

        return message;
    }

    boost::optional<AssetSwapOrder> DexBoard::parseAssetSwapMessage(const BroadcastMsg& msg)
    {
        try
        {
            const auto pkdf = _wdb.get_SbbsKdf();
            AssetSwapOrder order(msg.m_content, msg.m_signature, pkdf);
            return order;
        }
        catch(std::runtime_error& err)
        {
            LOG_WARNING() << "DexBoard parse error: " << err.what();
            return boost::none;
        }
        catch(...)
        {
            LOG_WARNING() << "DexBoard message type parse error";
            return boost::none;
        }
    }

    void DexBoard::notifyObservers(ChangeAction action, const std::vector<AssetSwapOrder>& orders) const
    {
        for (const auto obs : _observers)
        {
            obs->onAssetSwapOrdersChanged(action, orders);
        }
    }

    bool DexBoard::acceptIncomingDexSS(const SetTxParameter& msg)
    {
        // TODO:DEX perform real check
        // always true is only for tests
        return true;
    }

    void DexBoard::onDexTxCreated(const SetTxParameter& msg, BaseTransaction::Ptr)
    {
        // TODO:DEX associate with the real order
    }
}
