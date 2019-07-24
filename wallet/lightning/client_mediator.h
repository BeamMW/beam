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

#include <memory>
#include <vector>

#include "client_interface.h"
#include "core/fly_client.h"
#include "laser_connection.h"
#include "laser_listener.h"
#include "lightning_channel.h"
#include "wallet/wallet_db.h"

namespace beam::wallet::lightning
{
class LightningChannel;

// using NetPtr=std::shared_ptr<proto::FlyClient::INetwork>;
class ClientMediator : public IClient
{
public:
    ClientMediator(IWalletDB::Ptr walletDB, const beam::io::Address& nodeAddr);
    void OnNewTip() final;
    Block::SystemState::IHistory& get_History() final;

    void OpenChannel(Amount aMy,
                     Amount aTrg,
                     Amount fee,
                     const WalletID& receiverWalletID,
                     Height locktime);
private:
    // Height get_TipHeight() const;

    IWalletDB::Ptr m_pWalletDB;
    std::unique_ptr<LaserListener> m_pListener;
    std::shared_ptr<LaserConnection> m_pConnection;
    std::vector<std::unique_ptr<LightningChannel> > m_channels;
};
}  // namespace beam::wallet::lightning
