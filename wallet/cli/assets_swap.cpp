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

#include "assets_swap.h"

namespace beam::wallet
{
AssetsSwapCliHandler::AssetsSwapCliHandler()
{
}

void AssetsSwapCliHandler::init(
    Wallet::Ptr wallet, WalletDB::Ptr walletDb, 
    proto::FlyClient::INetwork::Ptr nnet, wallet::IWalletMessageEndpoint& wnet)
{
    _broadcastRouter = std::make_shared<BroadcastRouter>(
        nnet, wnet, std::make_shared<BroadcastRouter::BbsTsHolder>(walletDb));

    _dexBoard = std::make_shared<DexBoard>(*_broadcastRouter, *walletDb);
    _dexWDBSubscriber = std::make_unique<WalletDbSubscriber>(static_cast<IWalletDbObserver*>(_dexBoard.get()), walletDb);
    _dexWalletSubscriber = std::make_unique<DexWalletSubscriber>(
        static_cast<ISimpleSwapHandler*>(_dexBoard.get()), wallet);

    _isInit = true;
}

DexBoard::Ptr AssetsSwapCliHandler::getDex() const
{
    return _dexBoard;
}

}  // namespace beam::wallet
