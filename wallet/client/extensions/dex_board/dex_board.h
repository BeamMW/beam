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

#include "dex_order.h"
#include "wallet/client/extensions/broadcast_gateway/interface.h"
#include "wallet/core/wallet.h"
// #include "wallet/core/dex.h"
#include "wallet/client/wallet_model_async.h"

namespace beam::wallet {
    class WalletClient;
    class DexBoard
       : public IBroadcastListener
       , public ISimpleSwapHandler
       , public IWalletDbObserver
    {
    public:
        struct IObserver
        {
            virtual void onDexOrdersChanged(ChangeAction action, const std::vector<DexOrder>& orders) = 0;
            virtual void onFindDexOrder(const DexOrder& order) = 0;
        };

        DexBoard(IBroadcastMsgGateway& gateway, IWalletModelAsync::Ptr wallet, IWalletDB& wdb);

        [[nodiscard]] std::vector<DexOrder> getDexOrders() const;
        [[nodiscard]] boost::optional<DexOrder> getDexOrder(const DexOrderID&) const;

        void publishOrder(const DexOrder&);
        void acceptOrder(const DexOrderID& id);

        void Subscribe(IObserver* observer)
        {
            assert(std::find(_observers.begin(), _observers.end(), observer) == _observers.end());
            _observers.push_back(observer);
        }

        void Unsubscribe(IObserver* observer)
        {
            auto it = std::find(_observers.begin(), _observers.end(), observer);
            assert(it != _observers.end());
            _observers.erase(it);
        }

    private:
        bool handleDexOrder(const boost::optional<DexOrder>&);
        //
        // IBroadcastListener
        //
        bool onMessage(uint64_t, BroadcastMsg&&) override;

        //
        // ISimpleSwapHandler
        //
        bool acceptIncomingDexSS(const SetTxParameter& msg) override;
        void onDexTxCreated(const SetTxParameter& msg, BaseTransaction::Ptr) override;

        //
        // Serialization
        //
        BroadcastMsg createMessage(const DexOrder&);
        boost::optional<DexOrder> parseAssetSwapMessage(const BroadcastMsg& msg);

        //
        // Subscribers
        //
        void notifyObservers(ChangeAction action, const std::vector<DexOrder>&) const;
        std::vector<IObserver*> _observers;

        IBroadcastMsgGateway& _gateway;
        IWalletModelAsync::Ptr _wallet;
        IWalletDB& _wdb;
        std::map<DexOrderID, DexOrder> _orders;
    };
}
