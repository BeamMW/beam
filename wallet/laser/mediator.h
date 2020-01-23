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

#include <functional>
#include <memory>
#include <vector>

#include "wallet/laser/i_channel_holder.h"
#include "wallet/laser/i_receiver_holder.h"
#include "core/fly_client.h"
#include "wallet/laser/channel.h"
#include "wallet/core/wallet_db.h"

namespace beam::wallet::laser
{

class Receiver;

class Mediator : public IChannelHolder, public proto::FlyClient
{
public:
    class Observer
    {
    public:
        virtual ~Observer() {};
        virtual void OnOpened(const ChannelIDPtr& chID) {};
        virtual void OnOpenFailed(const ChannelIDPtr& chID) {};
        virtual void OnClosed(const ChannelIDPtr& chID) {};
        virtual void OnCloseFailed(const ChannelIDPtr& chID) {};
        virtual void OnUpdateStarted(const ChannelIDPtr& chID) {}; 
        virtual void OnUpdateFinished(const ChannelIDPtr& chID) {};
    protected:
        friend class Mediator;
        Mediator* m_observable;
    };
    Mediator(const IWalletDB::Ptr& walletDB,
             const IPrivateKeyKeeper::Ptr& keyKeeper);
    ~Mediator();
    // proto::FlyClient
    void OnNewTip() override;
    void OnRolledBack() override;
    Block::SystemState::IHistory& get_History() override;
    void OnOwnedNode(const PeerID&, bool bUp) override;

    // IChannelHolder implementation;
    IWalletDB::Ptr getWalletDB() final;
    proto::FlyClient::INetwork& get_Net() final;
    // IReceiverHolder implementation;
    void OnMsg(const ChannelIDPtr& chID, Blob&& blob) final;
    bool Decrypt(const ChannelIDPtr& chID, uint8_t* pMsg, Blob* blob) final;
    
    void SetNetwork(const proto::FlyClient::NetworkStd::Ptr& net);

    void WaitIncoming(Amount aMy, Amount aTrg, Amount fee, Height locktime);
    WalletID getWaitingWalletID() const;
    
    void OpenChannel(Amount aMy,
                     Amount aTrg,
                     Amount fee,
                     const WalletID& receiverWalletID,
                     Height locktime);
    bool Serve(const std::string& channelID);
    bool Transfer(Amount amount, const std::string& channelID);
    bool Close(const std::string& channelID);
    bool GracefulClose(const std::string& channelID);
    bool Delete(const std::string& channelID);
    size_t getChannelsCount() const;

    void AddObserver(Observer* observer);
    void RemoveObserver(Observer* observer);

private:
    ECC::Scalar::Native get_skBbs(const ChannelIDPtr& chID);
    void OnIncoming(const ChannelIDPtr& chID,
                    Negotiator::Storage::Map& dataIn);
    void OpenInternal(const ChannelIDPtr& chID);
    void TransferInternal(Amount amount, const ChannelIDPtr& chID);
    void CloseInternal(const ChannelIDPtr& chID);
    void ForgetChannel(const ChannelIDPtr& chID);
    ChannelIDPtr RestoreChannel(const std::string& channelID);
    bool RestoreChannelInternal(const ChannelIDPtr& p_channelID);
    void UpdateChannels();
    void UpdateChannelExterior(const std::unique_ptr<Channel>& channel);
    bool ValidateTip();
    void PrepareToForget(const std::unique_ptr<Channel>& channel);
    bool IsEnoughCoinsAvailable(Amount required);

    IWalletDB::Ptr m_pWalletDB;
    IPrivateKeyKeeper::Ptr m_keyKeeper;
    proto::FlyClient::INetwork::Ptr m_pConnection;

    std::unique_ptr<Receiver> m_pInputReceiver;
    Amount m_myInAllowed = 0;
    Amount m_trgInAllowed = 0;
    Amount m_feeAllowed = 0;
    Height m_locktimeAllowed = kDefaultTxLifetime;
    WalletAddress m_myInAddr;

    std::unordered_map<ChannelIDPtr, std::unique_ptr<Channel>> m_channels;
    std::vector<std::function<void()>> m_actionsQueue;
    std::vector<ChannelIDPtr> m_readyForForgetChannels;
    std::vector<Observer*> m_observers;
};
}  // namespace beam::wallet::laser
