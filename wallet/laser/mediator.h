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

#include "wallet/laser/i_receiver_holder.h"
#include "core/fly_client.h"
#include "wallet/laser/channel.h"
#include "wallet/wallet_db.h"

namespace beam::wallet::laser
{
// class LightningChannel;
class Connection;
class Receiver;

class Mediator : public IReceiverHolder
{
public:
    Mediator(IWalletDB::Ptr walletDB, std::shared_ptr<proto::FlyClient::NetworkStd>& net);
    // IReceiverHolder implementation
    void OnNewTip() final;
    Block::SystemState::IHistory& get_History() final;
    void OnMsg(Blob&& blob) final;
    ECC::Scalar::Native get_skBbs() final;

    void Listen();
    void OpenChannel(Amount aMy,
                     Amount aTrg,
                     Amount fee,
                     const WalletID& receiverWalletID,
                     Height locktime);
private:
    // Height get_TipHeight() const;

    IWalletDB::Ptr m_pWalletDB;
    std::unique_ptr<Receiver> m_pReceiver;
    std::shared_ptr<Connection> m_pConnection;

    std::unique_ptr<Channel> m_lch;
    std::vector<std::unique_ptr<Channel> > m_channels;
    WalletAddress m_myAddr;
};
}  // namespace beam::wallet::laser
