// Copyright 2022 The Beam Team
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

#include "wallet/client/extensions/broadcast_gateway/broadcast_router.h"
#include "wallet/client/extensions/dex_board/dex_board.h"
#include "wallet/transactions/dex/dex_tx.h"

#include <memory>

namespace beam::wallet
{
class AssetsSwapCliHandler : private DexBoard::IObserver
{
private:
    DexBoard::Ptr _dexBoard;
    std::shared_ptr<BroadcastRouter> _broadcastRouter;
    using WalletDbSubscriber = ScopedSubscriber<IWalletDbObserver, IWalletDB>;
    std::unique_ptr<WalletDbSubscriber> _dexWDBSubscriber;
    using DexWalletSubscriber = ScopedSubscriber<ISimpleSwapHandler, Wallet>;
    std::unique_ptr<DexWalletSubscriber> _dexWalletSubscriber;
    bool _isInit = false;

    void onDexOrdersChanged(ChangeAction, const std::vector<DexOrder>&) override {}
    void onFindDexOrder(const DexOrder&) override {}
public:
    AssetsSwapCliHandler();
    void init(Wallet::Ptr, WalletDB::Ptr, proto::FlyClient::INetwork::Ptr, wallet::IWalletMessageEndpoint&);
    DexBoard::Ptr getDex() const;
};

}  // namespace beam::wallet
