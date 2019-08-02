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

#include "wallet/laser/i_channel_holder.h"
#include "wallet/laser/i_receiver_holder.h"
#include "core/fly_client.h"
#include "wallet/laser/channel.h"
#include "wallet/wallet_db.h"

namespace beam::wallet::laser
{

// class Connection;
class Receiver;

class Mediator : public IChannelHolder
{
public:
    Mediator(const IWalletDB::Ptr& walletDB,
             const proto::FlyClient::NetworkStd::Ptr& net);
    ~Mediator();
    // IChannelHolder implementation;
    IWalletDB::Ptr getWalletDB() final;
    proto::FlyClient::INetwork& get_Net() final;
    // IReceiverHolder implementation;
    void OnMsg(const ChannelIDPtr& chID, Blob&& blob) final;
    bool Decrypt(const ChannelIDPtr& chID, uint8_t* pMsg, Blob* blob) final;
    
    void OnRolledBack();
    void OnNewTip();

    void WaitIncoming();
    void OpenChannel(Amount aMy,
                     Amount aTrg,
                     Amount fee,
                     const WalletID& receiverWalletID,
                     Height locktime);
    void Close(const std::string& channelIDStr);

private:
    Block::SystemState::IHistory& get_History();
    ECC::Scalar::Native get_skBbs(const ChannelIDPtr& chID);
    void OnIncoming(const ChannelIDPtr& chID,
                    Negotiator::Storage::Map& dataIn);

    IWalletDB::Ptr m_pWalletDB;
    std::shared_ptr<proto::FlyClient::INetwork> m_pConnection;

    std::unique_ptr<Receiver> m_pInputReceiver;

    std::unordered_map<ChannelIDPtr, std::unique_ptr<Channel>> m_channels;

    WalletAddress m_myOutAddr;
    WalletAddress m_myInAddr;
};
}  // namespace beam::wallet::laser
