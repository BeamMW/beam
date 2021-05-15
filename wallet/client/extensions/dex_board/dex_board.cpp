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
#include "utility/logger.h"
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

    std::vector<DexOrder> DexBoard::getOrders() const
    {
        std::vector<DexOrder> result;
        result.reserve(_orders.size());

        for (auto pair: _orders)
        {
            result.push_back(pair.second);
        }

        return result;
    }

    boost::optional<DexOrder> DexBoard::getOrder(const DexOrderID& orderId) const
    {
        const auto it = _orders.find(orderId);
        if (it != _orders.end())
        {
            return it->second;
        }
        return boost::none;
    }

    /*void DexBoard::acceptOrder(const DexOrderID &id)
    {
        auto order = _orders.find(id);
        if (order == _orders.end())
        {
            LOG_WARNING() << "DexBoard::acceptOrder, order " << id.to_string() << " not found";
            return;
        }

        auto params = CreateDexTransactionParams();
        _wallet->startTransaction(std::move(params));
    }*/

    void DexBoard::publishOrder(const DexOrder &offer)
    {
        [[maybe_unused]] auto message = createMessage(offer);
        _gateway.sendMessage(BroadcastContentType::DexOffers, message);
    }

    bool DexBoard::onMessage(uint64_t, ByteBuffer&&)
    {
        assert(false);
        LOG_WARNING() << "DexBoard: deprecated protocol message";
        return false;
    }

    bool DexBoard::onMessage(uint64_t, BroadcastMsg&& msg)
    {
        auto order = parseMessage(msg);
        if (!order)
        {
            return false;
        }

        LOG_INFO() << "DexBoard oder message received: " << order->sellCoin << "->" << order->buyCoin << ":" << order->amount;

        auto it = _orders.find(order->orderID);
        _orders[order->orderID] = *order;

        if (it == _orders.end())
        {
            notifyObservers(ChangeAction::Added, std::vector<DexOrder>{ *order });
        }
        else
        {
            notifyObservers(ChangeAction::Updated, std::vector<DexOrder>{ *order });
        }

        // TODO:DEX remove unnecessary orders
        // TODO:DEX ChangeAction:Reset?
        return true;
    }

    BroadcastMsg DexBoard::createMessage(const DexOrder& order)
    {
        const auto kdf = _wdb.get_SbbsKdf();
        if (!kdf)
        {
            throw std::runtime_error("cannot get sbbs kdf");
        }

        using PrivateKey = ECC::Scalar::Native;
        using PublicKey = PeerID;

        PrivateKey sk;
        PublicKey pk;
        kdf->DeriveKey(sk, ECC::Key::ID(order.sbbsKeyIDX, Key::Type::Bbs));
        pk.FromSk(sk);

        auto buffer  = toByteBuffer(order);
        auto message = BroadcastMsgCreator::createSignedMessage(buffer, sk);

        return message;
    }

    boost::optional<DexOrder> DexBoard::parseMessage(const BroadcastMsg& msg)
    {
        try
        {
            DexOrder order;
            if (!fromByteBuffer(msg.m_content, order))
            {
                throw std::runtime_error("failed to parse order body");
            }

            if (order.version < DexOrder::getCurrentVersion())
            {
                throw std::runtime_error("Obsolette order version - ignoring");
            }

            SignatureHandler sig;
            sig.m_data = msg.m_content;

            if (!fromByteBuffer(msg.m_signature, sig.m_Signature))
            {
                throw std::runtime_error("failed to parse signature");
            }

            if (!sig.IsValid(order.sbbsID.m_Pk))
            {
                throw std::runtime_error("failed to verify signature");
            }

            //
            // Check is this is our offer
            //
            const auto kdf = _wdb.get_SbbsKdf();
            if (!kdf)
            {
                throw std::runtime_error("cannot get sbbs kdf");
            }

            using PrivateKey = ECC::Scalar::Native;
            using PublicKey = PeerID;

            PrivateKey sk;
            PublicKey pk;
            kdf->DeriveKey(sk, ECC::Key::ID(order.sbbsKeyIDX, Key::Type::Bbs));
            pk.FromSk(sk);
            order.isMy = pk == order.sbbsID.m_Pk;

            // TODO:DEX check order validity
            return order;
        }
        // TODO:DEX any non-std errors?
        catch(std::runtime_error& err)
        {
            LOG_WARNING() << "DexBoard parse error: " << err.what();
            return boost::none;
        }
    }

    void DexBoard::notifyObservers(ChangeAction action, const std::vector<DexOrder>& orders) const
    {
        for (const auto obs : _observers)
        {
            obs->onDexOrdersChanged(action, orders);
        }
    }
}
