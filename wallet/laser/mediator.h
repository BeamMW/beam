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
#include <mutex>
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
        virtual void OnTransferFailed(const ChannelIDPtr& chID) {};
        virtual void OnExpired(const ChannelIDPtr& chID) {};
    protected:
        friend class Mediator;
        Mediator* m_observable;
    };
    Mediator(const IWalletDB::Ptr& walletDB,
             const Lightning::Channel::Params& params = {});
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
    
    void SetNetwork(const proto::FlyClient::NetworkStd::Ptr& net, bool mineOutgoing = true);
    void ListenClosedChannelsWithPossibleRollback();

    void WaitIncoming(Amount aMy, Amount aTrg, Amount fee);
    void StopWaiting();
    WalletID getWaitingWalletID() const;
    
    void OpenChannel(Amount aMy,
                     Amount aTrg,
                     Amount fee,
                     const WalletID& receiverWalletID,
                     Height hOpenTxDh = beam::Lightning::kDefaultOpenTxDh);
    bool Serve(const std::string& channelID);
    bool Transfer(Amount amount, const std::string& channelID);
    bool Close(const std::string& channelID);
    bool GracefulClose(const std::string& channelID);
    bool Delete(const std::string& channelID);
    size_t getChannelsCount() const;
    const Channel::Ptr& getChannel(const ChannelIDPtr& p_channelID);

    void AddObserver(Observer* observer);
    void RemoveObserver(Observer* observer);

private:
    bool get_skBbs(ECC::Scalar::Native&, const ChannelIDPtr& chID);
    bool OnIncoming(const ChannelIDPtr& channelID,
                    Negotiator::Storage::Map& dataIn);
    void OpenInternal(const ChannelIDPtr& chID, Height hOpenTxDh = beam::Lightning::kDefaultOpenTxDh);
    void TransferInternal(Amount amount, const Channel::Ptr& channel);
    void GracefulCloseInternal(const Channel::Ptr& channel);
    void CloseInternal(const ChannelIDPtr& chID);
    void ClosingCompleted(const ChannelIDPtr& p_channelID);
    void HandleOpenedWithFailChannel(const ChannelIDPtr& p_channelID);
    ChannelIDPtr LoadChannel(const std::string& channelID);
    Channel::Ptr LoadChannelInternal(
        const ChannelIDPtr& p_channelID);
    bool LoadAndStoreChannelInternal(const ChannelIDPtr& p_channelID);
    void UpdateChannels();
    void UpdateChannelExterior(const Channel::Ptr& channel);
    bool ValidateTip();
    bool IsEnoughCoinsAvailable(Amount required);
    void Subscribe();
    void Unsubscribe();
    bool IsInSync();
    void ExpireChannel(const Channel::Ptr& channel);

    IWalletDB::Ptr m_pWalletDB;
    proto::FlyClient::INetwork::Ptr m_pConnection;

    std::unique_ptr<Receiver> m_pInputReceiver;
    Amount m_myInAllowed = 0;
    Amount m_trgInAllowed = 0;
    Amount m_feeAllowed = 0;
    WalletAddress m_myInAddr;

    std::unordered_map<ChannelIDPtr, Channel::Ptr> m_channels;
    std::vector<std::function<void()>> m_actionsQueue;
    std::vector<ChannelIDPtr> m_readyForCloseChannels;
    std::vector<ChannelIDPtr> m_openedWithFailChannels;
    std::vector<Channel::Ptr> m_closedChannels;
    std::vector<Observer*> m_observers;

    Lightning::Channel::Params m_Params;
    mutable std::mutex m_mutex;
    using Lock = std::unique_lock<decltype(m_mutex)>;
};
}  // namespace beam::wallet::laser
