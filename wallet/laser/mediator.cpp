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

#include "wallet/laser/mediator.h"

#include "core/negotiator.h"
#include "core/lightning_codes.h"
#include "wallet/core/common_utils.h"
#include "wallet/laser/connection.h"
#include "wallet/laser/receiver.h"
#include "utility/helpers.h"
#include "utility/logger.h"

namespace
{
inline bool CanBeHandled(int state)
{
    return state != beam::Lightning::Channel::State::None &&
           state != beam::Lightning::Channel::State::OpenFailed &&
           state != beam::Lightning::Channel::State::Closed;
}

inline bool CanBeCounted(int state)
{
    return state != beam::Lightning::Channel::State::None &&
           state != beam::Lightning::Channel::State::OpenFailed &&
           state < beam::Lightning::Channel::State::Closed;
}

inline bool CanBeClosed(int state)
{
    return state < beam::Lightning::Channel::State::Closing1 &&
           state != beam::Lightning::Channel::State::OpenFailed &&
           state >= beam::Lightning::Channel::State::Opening1;
}

inline bool CanBeDeleted(int state)
{
    return state < beam::Lightning::Channel::State::Opening1 ||
           state == beam::Lightning::Channel::State::OpenFailed ||
           state == beam::Lightning::Channel::State::Closed ||
           state == beam::Lightning::Channel::State::Expired;
}

const beam::Timestamp kDefaultLaserTolerance = 60 * (beam::Lightning::kMaxBlackoutTime - 1);
}  // namespace

namespace beam::wallet::laser
{
Mediator::Mediator(const IWalletDB::Ptr& walletDB,
                   const Lightning::Channel::Params& params)
    : m_pWalletDB(walletDB)
    , m_Params(params)
{
    m_myInAddr.m_walletID = Zero;
}

Mediator::~Mediator()
{

}

void Mediator::OnNewTip()
{
    Lock lock(m_mutex);

    LOG_DEBUG() << "LASER OnNewTip";
    if (!ValidateTip())
        return;

    UpdateChannels();

    for (auto& sceduledAction : m_actionsQueue)
        sceduledAction();
    m_actionsQueue.clear();

    for (const auto& readyForCloseChannel : m_readyForCloseChannels)
        ClosingCompleted(readyForCloseChannel);
    m_readyForCloseChannels.clear();

    for (const auto& openedWithFailChannel : m_openedWithFailChannels)
        HandleOpenedWithFailChannel(openedWithFailChannel);
    m_openedWithFailChannels.clear();
}

void Mediator::OnRolledBack()
{
    LOG_DEBUG() << "LASER OnRolledBack";
    for (auto& it: m_channels)
    {
        auto& channel = it.second;
        channel->OnRolledBack();
        if (channel->TransformLastState())
        {
            channel->UpdateRestorePoint();
            m_pWalletDB->saveLaserChannel(*channel);
        }
    }

    for (auto& channel: m_closedChannels)
    {
        channel->OnRolledBack();
        if (channel->TransformLastState())
        {
            channel->UpdateRestorePoint();
            m_pWalletDB->saveLaserChannel(*channel);
            channel->Subscribe();

            auto chID = channel->get_chID();
            if (chID != nullptr)
                m_channels[chID] =
                    std::unique_ptr<Channel>(channel.release());
        }
    }

    m_closedChannels.erase(
        std::remove(m_closedChannels.begin(),
                    m_closedChannels.end(),
                    nullptr),
        m_closedChannels.end());
}

Block::SystemState::IHistory& Mediator::get_History()
{
    return m_pWalletDB->get_History();
}

void Mediator::OnOwnedNode(const PeerID&, bool bUp)
{

}

IWalletDB::Ptr Mediator::getWalletDB()
{
    return m_pWalletDB;
}

proto::FlyClient::INetwork& Mediator::get_Net()
{
    return *m_pConnection;
}

void Mediator::OnMsg(const ChannelIDPtr& chID, Blob&& blob)
{
    Lock lock(m_mutex);

    auto inChID = std::make_shared<ChannelID>(Zero);
    beam::Negotiator::Storage::Map dataIn;

    try
    {
        Deserializer der;
        der.reset(blob.p, blob.n);

        der & (*inChID);
        der & Cast::Down<FieldMap>(dataIn);
    }
    catch (const std::exception&)
    {
        return;
    }

    if (!chID && m_pInputReceiver)
    {
        if (!OnIncoming(inChID, dataIn)) return;
    }
    else
    {
        if (*chID != *inChID)
        {
            LOG_ERROR() << "Unknown channel ID: "
                        << to_hex(inChID->m_pData , inChID->nBytes);
            return;
        }
        inChID = chID;
    }

    LOG_DEBUG() << "Mediator::OnMsg "
                << to_hex(inChID->m_pData , inChID->nBytes);
    auto it = m_channels.find(inChID);
    if (it == m_channels.end())
    {
        LOG_DEBUG() << "Channel not found ID: "
                    << to_hex(inChID->m_pData , inChID->nBytes);
        return;
    }
    auto& channel = it->second;

    channel->OnPeerData(dataIn);
    auto state = channel->get_State();
    if (state == Lightning::Channel::State::Closing1)
    {
        channel->UpdateRestorePoint();
        m_pWalletDB->saveLaserChannel(*channel);
    }
    UpdateChannelExterior(channel);
}

bool Mediator::Decrypt(const ChannelIDPtr& chID, uint8_t* pMsg, Blob* blob)
{
    ECC::Scalar::Native sk;
    if (!get_skBbs(sk, chID))
        return false;

    if (!proto::Bbs::Decrypt(pMsg, blob->n, sk))
        return false;

    blob->p = pMsg;
    return true;
}

void Mediator::SetNetwork(const proto::FlyClient::NetworkStd::Ptr& net, bool mineOutgoing)
{
    m_pConnection = std::make_shared<Connection>(net, mineOutgoing);
}

void Mediator::ListenClosedChannelsWithPossibleRollback()
{
    auto channelEntities =
        m_pWalletDB->loadLaserChannels(Lightning::Channel::State::Closed);

    for (const auto& channelEntity : channelEntities)
    {
        const auto& chID = std::get<LaserFields::LASER_CH_ID>(channelEntity);
        auto channelIdStr = beam::to_hex(chID.m_pData, chID.nBytes);
        auto p_channelID = Channel::ChannelIdFromString(channelIdStr);
        if (!p_channelID)
        {
            LOG_ERROR() << "Incorrect channel ID format: " << channelIdStr;
            return;
        }
        auto channel = LoadChannelInternal(p_channelID);
        if (channel->IsSafeToForget()) continue;

        m_closedChannels.push_back(std::move(channel));
    }
}

void Mediator::WaitIncoming(Amount aMy, Amount aTrg, Amount fee)
{
    if (!IsEnoughCoinsAvailable(aMy + fee))
    {
        LOG_ERROR() << "Your available amount less then required.";
        auto zeroID = std::make_shared<ChannelID>(Zero);
        for (auto observer : m_observers)
            observer->OnOpenFailed(zeroID);
        return;
    }

    m_myInAllowed = aMy;
    m_trgInAllowed = aTrg;
    m_feeAllowed = fee;

    m_pInputReceiver = std::make_unique<Receiver>(*this, nullptr);
    m_pWalletDB->createAddress(m_myInAddr);
    m_myInAddr.setLabel("laser_in");
    m_myInAddr.setExpirationStatus(WalletAddress::ExpirationStatus::Never);
    m_pWalletDB->saveAddress(m_myInAddr, true);

    Subscribe();
}

void Mediator::StopWaiting()
{
    Unsubscribe();
    m_myInAllowed = 0;
    m_trgInAllowed = 0;
    m_feeAllowed = 0;
    m_pInputReceiver.reset();
    m_myInAddr.m_walletID = Zero;
}

WalletID Mediator::getWaitingWalletID() const
{
    return m_myInAddr.m_walletID;
}

bool Mediator::Serve(const std::string& channelID)
{
    LOG_DEBUG() << "Channel: " << channelID << " restore process started";
    auto p_channelID = LoadChannel(channelID);

    if (!p_channelID)
    {
        LOG_DEBUG() << "Channel: " << channelID << " restore failed";
        return false;
    }


    LOG_DEBUG() << "Channel: " << channelID << " restore process finished";
    auto& channel = m_channels[p_channelID];
    if (!channel) return false;

    if (channel->get_State() == beam::Lightning::Channel::State::Expired)
    {
        ExpireChannel(channel);
        return false;
    }

    if (CanBeHandled(channel->get_State()))
    {
        m_actionsQueue.emplace_back([this, p_channelID] () {
            auto& channel = m_channels[p_channelID];

            auto channelIdStr = to_hex(p_channelID->m_pData, p_channelID->nBytes);
            if (!channel)
            {
                LOG_ERROR() << "Unknown channel ID:  " << channelIdStr;
                return;
            }

            LOG_INFO() << "Channel " << channelIdStr <<" valid till: " << channel->get_LockHeight();
            LOG_INFO() << "Channel " << channelIdStr <<" lock time for one side actions: " << channel->getLocktime();
            LOG_INFO() << "Channel " << channelIdStr <<" expire after: " 
                       << channel->get_LockHeight() + channel->getLocktime();

            if (channel->get_State() == beam::Lightning::Channel::State::Expired)
            {
                ExpireChannel(channel);
                for (auto observer : m_observers)
                    observer->OnExpired(p_channelID);
                return;
            }

            channel->Subscribe();

        });
        return true;
    }

    LOG_DEBUG() << "Channel: " << channelID << " is inactive";
    return false;
}

void Mediator::OpenChannel(Amount aMy,
                           Amount aTrg,
                           Amount fee,
                           const WalletID& receiverWalletID,
                           Height hOpenTxDh)
{
    if (!IsEnoughCoinsAvailable(aMy + fee))
    {
        LOG_ERROR() << "Your available amount less then required.";
        auto zeroID = std::make_shared<ChannelID>(Zero);
        for (auto observer : m_observers)
            observer->OnOpenFailed(zeroID);
        return;
    }

    auto addresses = m_pWalletDB->getAddresses(true, true);
    if (storage::isMyAddress(addresses, receiverWalletID))
    {
        LOG_ERROR() << "Can't open channel on same DB as receiver";
        auto zeroID = std::make_shared<ChannelID>(Zero);
        for (auto observer : m_observers)
            observer->OnOpenFailed(zeroID);
        return;
    }

    WalletAddress myOutAddr("laser_out");
    m_pWalletDB->createAddress(myOutAddr);
    myOutAddr.setExpirationStatus(WalletAddress::ExpirationStatus::Never);

    auto params = m_Params;
    params.m_Fee = fee;
    auto channel = std::make_unique<Channel>(
        *this, myOutAddr, receiverWalletID, aMy, aTrg, params);

    auto chID = channel->get_chID();
    m_channels[chID] = std::move(channel);
    
    m_actionsQueue.emplace_back([this, chID, hOpenTxDh] () {
        OpenInternal(chID, hOpenTxDh);
    });
}

bool Mediator::Close(const std::string& channelID)
{
    auto p_channelID = LoadChannel(channelID);
    if (!p_channelID)
    {
        LOG_ERROR() << "Channel " << channelID << " restored with error";
        return false;
    }

    auto& channel = m_channels[p_channelID];
    if (!channel)
    {
        LOG_ERROR() << "Channel " << channelID << " unexpected error";
        return false;
    }

    if (channel->get_State() == beam::Lightning::Channel::State::Expired)
    {
        ExpireChannel(channel);
        return false;
    }

    if (channel->get_State() != Lightning::Channel::State::Open)
    {
        LOG_ERROR() << "Previous action with channel: " << channelID
                    << " is unfinished. Please, listen this channel till action complete.";
        return false;
    }

    m_actionsQueue.emplace_back([this, p_channelID] () {
        CloseInternal(p_channelID);
    });
    
    LOG_DEBUG() << "Closing channel: " << channelID << " is sceduled";
    return true;
}

bool Mediator::GracefulClose(const std::string& channelID)
{
    auto p_channelID = LoadChannel(channelID);
    if (!p_channelID)
    {
        LOG_ERROR() << "Channel " << channelID << " restored with error";
        return false;
    }

    auto& channel = m_channels[p_channelID];
    if (!channel)
    {
        LOG_ERROR() << "Channel " << channelID << " unexpected error";
        return false;
    }


    if (channel->get_State() == Lightning::Channel::State::Expired)
    {
        ExpireChannel(channel);
        return false;
    }

    if (channel->get_State() != Lightning::Channel::State::Open)
    {
        LOG_ERROR() << "Previous action with channel: " << channelID
                    << " is unfinished. Please, listen this channel till action complete.";
        return false;
    }

    if (!IsInSync())
    {
        m_actionsQueue.emplace_back([this, &channel] () {
            GracefulCloseInternal(channel);
        });
        LOG_DEBUG() << "Closing channel: " << channelID << " is sceduled";
    }
    else
    {
        GracefulCloseInternal(channel);
    }

    return true;
}

bool Mediator::Delete(const std::string& channelID)
{
    auto p_channelID = Channel::ChannelIdFromString(channelID);
    if (!p_channelID)
    {
        LOG_ERROR() << "Incorrect channel ID format: " << channelID;
        return false;
    }
    auto channel = LoadChannelInternal(p_channelID);
    if (!p_channelID) return false;

    auto state = channel->get_State();
    if (!CanBeDeleted(state))
    {
        LOG_ERROR() << "Channel: " << channelID << " in active state now";
        return false;
    }

    if (state == Lightning::Channel::State::Closed && !channel->IsSafeToForget())
    {
        LOG_ERROR() << "Channel: " << channelID << " can be rolled back";
        return false;
    }

    if (state == Lightning::Channel::State::OpenFailed)
    {
        channel->Forget();
    }

    m_channels.erase(p_channelID);
    if (m_pWalletDB->removeLaserChannel(p_channelID))
    {
        LOG_INFO() << "Channel: " << channelID << " deleted";
        return true;
    }

    LOG_INFO() << "Channel: " << channelID << " not deleted";
    return false;
}

size_t Mediator::getChannelsCount() const
{
    return std::count_if(
            m_channels.begin(),
            m_channels.end(),
            [] (const auto& it) {
        const auto& ch = it.second;
        return CanBeCounted(ch->get_State());
    });
}

const Channel::Ptr& Mediator::getChannel(const ChannelIDPtr& p_channelID)
{
    return m_channels[p_channelID];
}

void Mediator::AddObserver(Observer* observer)
{
    observer->m_observable = this;
    m_observers.push_back(observer);
}

void Mediator::RemoveObserver(Observer* observer)
{
    m_observers.erase(
        std::remove(m_observers.begin(), m_observers.end(), observer),
        m_observers.end());
}

bool Mediator::Transfer(Amount amount, const std::string& channelID)
{
    auto p_channelID = LoadChannel(channelID);
    if (!p_channelID) 
    {
        LOG_ERROR() << "Channel " << channelID << " restored with error";
        return false;
    }


    auto& channel = m_channels[p_channelID];
    if (!channel) return false;

    auto myChannelAmount = channel->get_amountCurrentMy();
    if (myChannelAmount < amount)
    {
        LOG_ERROR() << "Transfer: " << PrintableAmount(amount, true)
                    << " to channel: " << channelID << " failed\n"
                    << "My current channel balance is: "
                    << PrintableAmount(myChannelAmount, true);
        return false;
    }

    if (channel->get_State() == beam::Lightning::Channel::State::Expired)
    {
        ExpireChannel(channel);
        return false;
    }

    if (channel->get_State() == Lightning::Channel::State::Open)
    {
        if (!IsInSync())
        {
            m_actionsQueue.emplace_back([this, amount, &channel] () {
                if (channel->get_State() == beam::Lightning::Channel::State::Expired)
                {
                    ExpireChannel(channel);
                    for (auto observer : m_observers)
                        observer->OnExpired(channel->get_chID());
                    return;
                }
                TransferInternal(amount, channel);
            });
        
            LOG_INFO() << "Sync in progress...";
            LOG_DEBUG() << "Transfer: " << PrintableAmount(amount, true)
                        << " to channel: " << channelID << " is sceduled";
        }
        else
        {
            Lock lock(m_mutex);
            TransferInternal(amount, channel);
        }

        return true;
    }
    else if (channel && channel->get_State() == Lightning::Channel::State::Updating)
    {
        LOG_ERROR() << "Previous transfer for channel: " << channelID << " is not completed.";
        return false;
    }

    LOG_ERROR() << "Channel " << channelID << " is not OPEN.";
    return false;
}

bool Mediator::get_skBbs(ECC::Scalar::Native& sk, const ChannelIDPtr& chID)
{
    auto& addr = chID ? m_channels[chID]->get_myAddr() : m_myInAddr;
    auto& wid = addr.m_walletID;
    if (wid == Zero)
        return false;

    PeerID peerID;
    m_pWalletDB->get_SbbsPeerID(sk, peerID, addr.m_OwnID);

    return wid.m_Pk == peerID;
}

bool Mediator::OnIncoming(const ChannelIDPtr& channelID,
                          Negotiator::Storage::Map& dataIn)
{
    WalletID trgWid;
    if (!dataIn.Get(trgWid, Channel::Codes::MyWid))
        return false;

    Amount fee;
    if (!dataIn.Get(fee, Lightning::Codes::Fee) || fee != m_feeAllowed)
    {
        LOG_ERROR() << "Incoming connection with incorrect 'laser_fee' detected";
        return false;
    }

    Amount aMy;
    if (!dataIn.Get(aMy, Lightning::Codes::ValueYours) || aMy != m_myInAllowed)
    {
        LOG_ERROR() << "Incoming connection with incorrect 'laser_my_locked_amount' detected";
        return false;
    }
    
    Amount aTrg;
    if (!dataIn.Get(aTrg, Lightning::Codes::ValueMy) || aTrg != m_trgInAllowed)
    {
        LOG_ERROR() << "Incoming connection with incorrect 'laser_remote_locked_amount' detected";
        return false;
    }

    Height locktime;
    if (!dataIn.Get(locktime, Lightning::Codes::HLock) || locktime != m_Params.m_hLockTime)
    {
        LOG_ERROR() << "Incoming connection with incorrect 'HLock' detected";
        return false;
    }          

    Unsubscribe();

    LOG_INFO() << "Create channel by incoming connection: "
               << to_hex(channelID->m_pData , channelID->nBytes);

    auto params = m_Params;
    params.m_Fee = fee;
    auto channel = std::make_unique<Channel>(
        *this, channelID, m_myInAddr, trgWid, aMy, aTrg, params);
    channel->Subscribe();
    m_channels[channel->get_chID()] = std::move(channel);

    m_pInputReceiver.reset();
    return true;
}

void Mediator::OpenInternal(const ChannelIDPtr& chID, Height hOpenTxDh)
{
    auto& channel = m_channels[chID];
    if (!channel)
    {
        LOG_ERROR() << "Unknown channel ID:  " << to_hex(chID->m_pData, chID->nBytes);
        return;
    }

    channel->Subscribe();
    if (channel->Open(hOpenTxDh))
    {
        LOG_INFO() << "Opening channel: "
                   << to_hex(chID->m_pData, chID->nBytes);
        UpdateChannelExterior(channel);
        return;
    }

    LOG_ERROR() << "Opening channel "
                << to_hex(chID->m_pData, chID->nBytes) << " fail";

    channel->Unsubscribe();
    channel->Forget();
    m_channels.erase(chID);

    for (auto observer : m_observers)
        observer->OnOpenFailed(chID);
}

void Mediator::TransferInternal(Amount amount, const Channel::Ptr& channel)
{
    const auto& chID = channel->get_chID();
    std::string channelIdStr = to_hex(chID->m_pData, chID->nBytes);
    channel->Subscribe();

    Block::SystemState::Full tip;
    get_History().get_Tip(tip);
    Height channelLockHeight = channel->get_LockHeight();

    if (tip.m_Height + 1 < channelLockHeight)
    {
        if (channel->Transfer(amount))
        {
            LOG_INFO() << "Transfer: " << PrintableAmount(amount, true)
                    << " to channel: " << channelIdStr
                    << " started";
            UpdateChannelExterior(channel);
            return;
        }
        else
        {
            LOG_ERROR() << "Transfer: " << PrintableAmount(amount, true)
                        << " to channel: " << channelIdStr
                        << " failed";
        }
    }
    else
    {
        LOG_ERROR() << "You can't transfer: " << PrintableAmount(amount, true)
                    << " to channel: " << channelIdStr;
        LOG_ERROR() << "Current height: " << tip.m_Height + 1
                    << " overtop channel lock height: " << channelLockHeight;
    }
       
    
    for (auto observer : m_observers)
    {
        observer->OnTransferFailed(chID);
        observer->OnUpdateFinished(chID);
    }
}

void Mediator::GracefulCloseInternal(const Channel::Ptr& channel)
{
    Block::SystemState::Full tip;
    get_History().get_Tip(tip);

    if (tip.m_Height < channel->get_LockHeight())
    {
        if (!channel->Transfer(0, true))
        {
            auto& p_channelID = channel->get_chID();
            for (auto observer : m_observers)
                observer->OnCloseFailed(p_channelID);
        }
        UpdateChannelExterior(channel);
        channel->Subscribe();
    }
    else
    {
        CloseInternal(channel->get_chID());
    }
}

void Mediator::CloseInternal(const ChannelIDPtr& chID)
{
    auto& channel = m_channels[chID];
    if (channel && CanBeClosed(channel->get_State()))
    {
        channel->Close();
        UpdateChannelExterior(channel);
        channel->Subscribe();
        return;
    }
    LOG_ERROR() << "Can't close channel: " << to_hex(chID->m_pData, chID->nBytes);
    for (auto observer : m_observers)
    {
        observer->OnCloseFailed(chID);
    }
}

void Mediator::ClosingCompleted(const ChannelIDPtr& p_channelID)
{
    auto it = m_channels.find(p_channelID);
    if (it == m_channels.end()) return;

    LOG_INFO() << "ClosingCompleted: "
               << to_hex(p_channelID->m_pData, p_channelID->nBytes);
    m_closedChannels.push_back(std::move(it->second));
    m_channels.erase(it);
    for (auto observer : m_observers)
        observer->OnClosed(p_channelID);
}

void Mediator::HandleOpenedWithFailChannel(const ChannelIDPtr& p_channelID)
{
    auto it = m_channels.find(p_channelID);
    if (it == m_channels.end()) return;

    m_channels.erase(it);
    for (auto observer : m_observers)
        observer->OnOpenFailed(p_channelID);
}

ChannelIDPtr Mediator::LoadChannel(const std::string& channelID)
{
    auto p_channelID = Channel::ChannelIdFromString(channelID);
    if (!p_channelID)
    {
        LOG_ERROR() << "Incorrect channel ID format: " << channelID;
        return nullptr;
    }

    for (const auto& it : m_channels)
    {
        if (*(it.first) == *p_channelID)
        {
            LOG_DEBUG() << "Channel " << channelID << " loaded previously";
            return it.first;
        }
    }

    if (!LoadAndStoreChannelInternal(p_channelID)) return nullptr;

    return p_channelID;
}

Channel::Ptr Mediator::LoadChannelInternal(const ChannelIDPtr& p_channelID)
{
    TLaserChannelEntity chDBEntity;
    if (!m_pWalletDB->getLaserChannel(p_channelID, &chDBEntity))
    {
        LOG_INFO() << "Channel "
                   << to_hex(p_channelID->m_pData , p_channelID->nBytes)
                   << " not saved in DB";
        return nullptr;
    }

    auto& myWID = std::get<LaserFields::LASER_MY_WID>(chDBEntity);
    auto myAddr = m_pWalletDB->getAddress(myWID, true);
    if (!myAddr)
    {
        LOG_ERROR() << "Can't load address from DB: "
                    << std::to_string(myWID);
        return nullptr;
    }

    return std::make_unique<Channel>(*this, p_channelID, *myAddr, chDBEntity, m_Params);
}

bool Mediator::LoadAndStoreChannelInternal(const ChannelIDPtr& p_channelID)
{
    auto channel = LoadChannelInternal(p_channelID);
    if (channel && CanBeHandled(channel->get_State()))
    {
        m_channels[p_channelID] = std::move(channel);
        return true;
    }

    LOG_DEBUG() << "Channel "
                << to_hex(p_channelID->m_pData , p_channelID->nBytes)
                << " has inactive state or loaded with failure";
    return false;
}

void Mediator::UpdateChannels()
{
    for (auto& it: m_channels)
    {
        auto& channel = it.second;
        auto state = channel->get_State();

        bool revisionDiscarded = false;
        if (state == Lightning::Channel::State::Updating && channel->IsUpdateStuck())
        {
            LOG_WARNING() << "Update stuck, discarding last revision...";
            channel->DiscardLastRevision();
            revisionDiscarded = true;
        }

        bool closingDiscarded = false;
        if (state == Lightning::Channel::State::Closing1 && channel->IsGracefulCloseStuck())
        {
            LOG_WARNING() << "Closing stuck, discarding last revision...";
            channel->DiscardLastRevision();
            closingDiscarded = true;
        }

        if (state != Lightning::Channel::State::None &&
            state != Lightning::Channel::State::Closed &&
            state != Lightning::Channel::State::OpenFailed &&
            channel->IsSubscribed())
        {
            channel->Update();
        }

        UpdateChannelExterior(channel);

        if (revisionDiscarded)
            for (auto observer : m_observers)
                observer->OnTransferFailed(channel->get_chID());

        if (closingDiscarded)
            for (auto observer : m_observers)
                observer->OnCloseFailed(channel->get_chID());

    }
}

void Mediator::UpdateChannelExterior(const Channel::Ptr& channel)
{
    auto lastState = channel->get_LastState();
    if (!channel->TransformLastState()) return;

    channel->LogState();
    auto state = channel->get_State();
    if (state == Lightning::Channel::State::None) return;
    
    channel->UpdateRestorePoint();
    m_pWalletDB->saveLaserChannel(*channel);

    if (state == Lightning::Channel::State::Open)
    {
        if (lastState <= Lightning::Channel::State::Opening2 &&
            lastState != Lightning::Channel::State::None)
        {
            for (auto observer : m_observers)
                observer->OnOpened(channel->get_chID());
        }
        else if (lastState == Lightning::Channel::State::Updating)
        {
            for (auto observer : m_observers)
                observer->OnUpdateFinished(channel->get_chID());
        }
    }
    else if (state == Lightning::Channel::State::Updating)
    {
        if (lastState == Lightning::Channel::State::Open)
        {
            for (auto observer : m_observers)
                observer->OnUpdateStarted(channel->get_chID());
        }
    }
    else if (state == Lightning::Channel::State::Closed)
    {
        if (lastState != Lightning::Channel::State::Closed)
            channel->Unsubscribe();

        if (!channel->IsNegotiating() && channel->IsSafeToClose())
            m_readyForCloseChannels.push_back(channel->get_chID());
    }
    else if (state == Lightning::Channel::State::OpenFailed &&
             lastState != Lightning::Channel::State::OpenFailed)
    {
        channel->Unsubscribe();
        channel->Forget();

        auto channelID = channel->get_chID();
        m_openedWithFailChannels.push_back(channelID);
    }
}

bool Mediator::ValidateTip()
{
    Block::SystemState::Full tip;
    get_History().get_Tip(tip);
    if (!IsValidTimeStamp(tip.m_TimeStamp, kDefaultLaserTolerance) || !tip.m_Height)
        return false;

    Block::SystemState::ID id;
    tip.get_ID(id);
    m_pWalletDB->setSystemStateID(id);
    LOG_INFO() << "LASER Current state is " << id;
    return true;
}

bool Mediator::IsEnoughCoinsAvailable(Amount required)
{
    storage::Totals totalsCalc(*m_pWalletDB);
    const auto& totals = totalsCalc.GetBeamTotals();
    return AmountBig::get_Lo(totals.Avail) >= required;
}

void Mediator::Subscribe()
{
    BbsChannel ch;
    m_myInAddr.m_walletID.m_Channel.Export(ch);
    m_pConnection->BbsSubscribe(ch, getTimestamp(), m_pInputReceiver.get());
    LOG_DEBUG() << "LASER WAIT IN subscribed: " << ch;
}

void Mediator::Unsubscribe()
{
    BbsChannel ch;
    m_myInAddr.m_walletID.m_Channel.Export(ch);
    m_pConnection->BbsSubscribe(ch, 0, nullptr);
    LOG_DEBUG() << "LASER WAIT IN unsubscribed: " << ch;
}

bool Mediator::IsInSync()
{
    Block::SystemState::Full tip;
    get_History().get_Tip(tip);

    return IsValidTimeStamp(tip.m_TimeStamp, kDefaultLaserTolerance);
}

void Mediator::ExpireChannel(const Channel::Ptr& channel)
{
    const auto& p_channelID = channel->get_chID();
    LOG_ERROR() << "Channel ID:  "
                << to_hex(p_channelID->m_pData, p_channelID->nBytes) << " lock height expired";
    channel->LogState();
    channel->UpdateRestorePoint();
    m_pWalletDB->saveLaserChannel(*channel);
}

}  // namespace beam::wallet::laser
