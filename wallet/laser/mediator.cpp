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
#include "wallet/core//common_utils.h"
#include "wallet/laser/connection.h"
#include "wallet/laser/receiver.h"
#include "utility/helpers.h"
#include "utility/logger.h"

namespace
{
const beam::Timestamp kToleranceSeconds =
    60 * beam::wallet::laser::kMaxRolbackHeight;

bool IsValidTimeStamp(beam::Timestamp currentBlockTime_s)
{
    beam::Timestamp currentTime_s = beam::getTimestamp();

    if (currentTime_s > currentBlockTime_s + kToleranceSeconds)
    {
        LOG_DEBUG() << "It seems that node is not up to date";
        return false;
    }
    return true;
}

bool IsOutOfSync(beam::Timestamp currentBlockTime_s)
{
    beam::Timestamp currentTime_s = beam::getTimestamp();

    if (currentTime_s > currentBlockTime_s + kToleranceSeconds / 2)
    {
        LOG_DEBUG() << "Node is out of sync";
        return true;
    }
    return false;
}

inline bool CanBeHandled(int state)
{
    return state != beam::Lightning::Channel::State::None &&
           state != beam::Lightning::Channel::State::OpenFailed &&
           state != beam::Lightning::Channel::State::Closed;
}

inline bool CanBeClosed(int state)
{
    return state <= beam::Lightning::Channel::State::Closing1 &&
           state != beam::Lightning::Channel::State::OpenFailed &&
           state >= beam::Lightning::Channel::State::Opening1;
}

inline bool CanBeDeleted(int state)
{
    return state == beam::Lightning::Channel::State::None ||
           state == beam::Lightning::Channel::State::OpenFailed ||
           state == beam::Lightning::Channel::State::Closed;
}

inline bool CanBeLoaded(int state)
{
    return state >= beam::Lightning::Channel::State::Opening1 &&
           CanBeHandled(state);
}
}  // namespace

namespace beam::wallet::laser
{
Mediator::Mediator(const IWalletDB::Ptr& walletDB)
    : m_pWalletDB(walletDB)
{
    m_myInAddr.m_walletID = Zero;
}

Mediator::~Mediator()
{

}

void Mediator::OnNewTip()
{
    LOG_DEBUG() << "LASER OnNewTip";
    if (!ValidateTip())
    {
        return;
    }

    UpdateChannels();

    for (auto& sceduledAction : m_actionsQueue)
    {
        sceduledAction();
    }
    m_actionsQueue.clear();

    for (const auto& readyForCloseChannel : m_readyForForgetChannels)
    {
        ForgetChannel(readyForCloseChannel);
        LOG_INFO() << "ForgetChannel: "
                   << to_hex(readyForCloseChannel->m_pData,
                             readyForCloseChannel->nBytes);
    }
    m_readyForForgetChannels.clear();
}

void Mediator::OnRolledBack()
{
    LOG_DEBUG() << "LASER OnRolledBack";
    for (auto& it: m_channels)
    {
        auto& ch = it.second;
        ch->OnRolledBack();
        if (ch->TransformLastState())
        {
            ch->UpdateRestorePoint();
            m_pWalletDB->saveLaserChannel(*ch);
        }
    }
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
        LOG_INFO() << "Create incoming: "
                   << to_hex(inChID->m_pData , inChID->nBytes);
        OnIncoming(inChID, dataIn);
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
    auto& ch = it->second;

    ch->OnPeerData(dataIn);
    ch->LogNewState();
    UpdateChannelExterior(ch);
}

bool  Mediator::Decrypt(const ChannelIDPtr& chID, uint8_t* pMsg, Blob* blob)
{
    if (!proto::Bbs::Decrypt(pMsg, blob->n, get_skBbs(chID)))
		return false;

	blob->p = pMsg;
    return true;
}

void Mediator::SetNetwork(const proto::FlyClient::NetworkStd::Ptr& net)
{
    m_pConnection = std::make_shared<Connection>(net);
}

void Mediator::WaitIncoming(Amount aMy, Amount aTrg, Amount fee, Height locktime)
{
    m_myInAllowed = aMy;
    m_trgInAllowed = aTrg;
    m_feeAllowed = fee;
    m_locktimeAllowed = locktime;

    m_pInputReceiver = std::make_unique<Receiver>(*this, nullptr);
    m_myInAddr = GenerateNewAddress(
        m_pWalletDB,
        "laser_in",
        WalletAddress::ExpirationStatus::Never,
        false);

    BbsChannel ch;
    m_myInAddr.m_walletID.m_Channel.Export(ch);
    m_pConnection->BbsSubscribe(ch, getTimestamp(), m_pInputReceiver.get());
    LOG_DEBUG() << "LASER WAIT IN subscribed: " << ch;
}

bool Mediator::Serve(const std::string& channelID)
{
    LOG_DEBUG() << "Channel: " << channelID << " restore process started";
    auto p_channelID = RestoreChannel(channelID);

    if (!p_channelID)
    {
        LOG_DEBUG() << "Channel: " << channelID << " restore failed";
        return false;
    }


    LOG_DEBUG() << "Channel: " << channelID << " restore process finished";
    auto& channel = m_channels[p_channelID];
    if (channel && CanBeHandled(channel->get_State()))
    {
        channel->Subscribe();
        return true;
    }

    LOG_DEBUG() << "Channel: " << channelID << " is inactive";
    return false;
}

void Mediator::OpenChannel(Amount aMy,
                           Amount aTrg,
                           Amount fee,
                           const WalletID& receiverWalletID,
                           Height locktime)
{
    auto myOutAddr = GenerateNewAddress(
            m_pWalletDB,
            "laser_out",
            WalletAddress::ExpirationStatus::Never,
            false);        

    auto channel = std::make_unique<Channel>(
        *this, myOutAddr, receiverWalletID,
        fee, aMy, aTrg, locktime);
    channel->Subscribe();

    auto chID = channel->get_chID();
    m_channels[chID] = std::move(channel);
    
    m_actionsQueue.emplace_back([this, chID] () {
        OpenInternal(chID);
    });
}

bool Mediator::Close(const std::string& channelID)
{
    auto p_channelID = RestoreChannel(channelID);
    if (!p_channelID)
    {
        LOG_DEBUG() << "Channel " << channelID << " restored with error";
        return false;
    }

    auto& channel = m_channels[p_channelID];
    if (!channel)
    {
        LOG_DEBUG() << "Channel " << channelID << " unexpected error";
        return false;
    }

    channel->Subscribe();
    m_actionsQueue.emplace_back([this, p_channelID] () {
        CloseInternal(p_channelID);
    });
    
    LOG_DEBUG() << "Closing channel: " << channelID << " is sceduled";
    return true;
}

bool Mediator::GracefulClose(const std::string& channelID)
{
    auto p_channelID = RestoreChannel(channelID);
    if (!p_channelID)
    {
        LOG_DEBUG() << "Channel " << channelID << " restored with error";
        return false;
    }

    auto& channel = m_channels[p_channelID];
    if (!channel)
    {
        LOG_DEBUG() << "Channel " << channelID << " unexpected error";
        return false;
    }

    channel->Subscribe();

    Block::SystemState::Full tip;
    get_History().get_Tip(tip);

    if (IsOutOfSync(tip.m_TimeStamp))
    {
        m_actionsQueue.emplace_back([this, &channel, p_channelID] () {
            Block::SystemState::Full tip;
            get_History().get_Tip(tip);

            if (tip.m_Height <= channel->get_LockHeight())
            {
                channel->Transfer(0, true);
            }
            else
            {
                CloseInternal(p_channelID);
            }
        });
        LOG_DEBUG() << "Closing channel: " << channelID << " is sceduled";
    }
    else
    {
        if (tip.m_Height <= channel->get_LockHeight())
        {
            channel->Transfer(0, true);
        }
        else
        {
            CloseInternal(p_channelID);
        }
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

    TLaserChannelEntity chDBEntity;
    if (!m_pWalletDB->getLaserChannel(p_channelID, &chDBEntity))
    {
        LOG_ERROR() << "Not found channel with ID: " << channelID;
        return false;
    }

    if (!CanBeDeleted(std::get<LaserFields::LASER_STATE>(chDBEntity)))
    {
        LOG_ERROR() << "Channel: " << channelID << " in active state now";
        return false;
    }

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
        return CanBeLoaded(ch->get_State());
    });
}

void Mediator::AddObserver(Observer* observer)
{
    observer->m_observable = this;
    m_observers.push_back(observer);
}

void Mediator::RemoveObserver(Observer* observer)
{
    m_observers.erase(
        m_observers.begin(),
        std::remove(m_observers.begin(), m_observers.end(), observer));
}

bool Mediator::Transfer(Amount amount, const std::string& channelID)
{
    auto p_channelID = RestoreChannel(channelID);
    if (!p_channelID) 
    {
        LOG_ERROR() << "Channel " << channelID << " restored with error";
        return false;
    }


    auto& channel = m_channels[p_channelID];
    auto myChannelAmount = channel->get_amountCurrentMy();
    if (myChannelAmount < amount)
    {
        LOG_ERROR() << "Transfer: " << PrintableAmount(amount, true)
                    << " to channel: " << channelID << " failed\n"
                    << "My current channel balance is: "
                    << PrintableAmount(myChannelAmount, true);
        return false;
    }

    if (channel && channel->get_State() ==
        beam::Lightning::Channel::State::Open)
    {
        channel->Subscribe();

        Block::SystemState::Full tip;
        get_History().get_Tip(tip);
        if (IsOutOfSync(tip.m_TimeStamp))
        {
            m_actionsQueue.emplace_back([this, amount, p_channelID] () {
                TransferInternal(amount, p_channelID);
            });
        
            LOG_INFO() << "Sync in progress...";
            LOG_DEBUG() << "Transfer: " << PrintableAmount(amount, true)
                        << " to channel: " << channelID << " is sceduled";
        }
        else
        {
            TransferInternal(amount, p_channelID);
        }

        return true;
    }

    LOG_ERROR() << "Channel " << channelID << " is not OPEN";
    return false;
}

ECC::Scalar::Native Mediator::get_skBbs(const ChannelIDPtr& chID)
{
    auto& addr = chID ? m_channels[chID]->get_myAddr() : m_myInAddr;
    auto& wid = addr.m_walletID;
    if (wid != Zero)
    {    
        PeerID peerID;
        ECC::Scalar::Native sk;
        m_pWalletDB->get_SbbsKdf()->DeriveKey(
            sk, Key::ID(addr.m_OwnID, Key::Type::Bbs));
        proto::Sk2Pk(peerID, sk);
        return wid.m_Pk == peerID ? sk : Zero;        
    }
    return Zero;
}

void Mediator::OnIncoming(const ChannelIDPtr& chID,
                          Negotiator::Storage::Map& dataIn)
{
    WalletID trgWid;
    if (!dataIn.Get(trgWid, Channel::Codes::MyWid))
        return;

    Amount fee;
    if (!dataIn.Get(fee, beam::Lightning::Codes::Fee))
        return;
    if (fee != m_feeAllowed)
    {
        LOG_ERROR() << "Incoming with incorrect FEE detected";
        return;
    }

    Amount aMy;
    if (!dataIn.Get(aMy, beam::Lightning::Codes::ValueYours))
        return;
    if (aMy != m_myInAllowed)
    {
        LOG_ERROR() << "Incoming with incorrect 'laser_my_locked_amount' AMOUNT detected";
        return;
    }
    
    Amount aTrg;
    if (!dataIn.Get(aTrg, beam::Lightning::Codes::ValueMy))
        return;
    if (aTrg != m_trgInAllowed)
    {
        LOG_ERROR() << "Incoming with incorrect 'laser_remote_locked_amount' AMOUNT detected";
        return;
    }

    Height locktime;
    if (!dataIn.Get(locktime, beam::Lightning::Codes::HLock))
        return; 
    if (locktime != m_locktimeAllowed)
    {
        LOG_ERROR() << "Incoming with incorrect LOCKTIME detected";
        return;
    }          

    BbsChannel ch;
    m_myInAddr.m_walletID.m_Channel.Export(ch);
    m_pConnection->BbsSubscribe(ch, 0, nullptr);
    LOG_DEBUG() << "LASER WAIT IN unsubscribed: " << ch;

    auto channel = std::make_unique<Channel>(
        *this, chID, m_myInAddr, trgWid, fee, aMy, aTrg, locktime);
    channel->Subscribe();
    m_channels[channel->get_chID()] = std::move(channel);

    m_pInputReceiver.reset();
}

void Mediator::OpenInternal(const ChannelIDPtr& chID)
{
    Block::SystemState::Full tip;
    get_History().get_Tip(tip);

    HeightRange openWindow;
    openWindow.m_Min = tip.m_Height;
    openWindow.m_Max = openWindow.m_Min + kDefaultTxLifetime;

    auto& ch = m_channels[chID];
    if (ch && ch->Open(openWindow))
    {
        LOG_INFO() << "Opening channel: "
                   << to_hex(chID->m_pData, chID->nBytes);
        return;
    }

    LOG_ERROR() << "Opening channel "
                << to_hex(chID->m_pData, chID->nBytes) << " fail";
    m_readyForForgetChannels.push_back(chID);
}

void Mediator::TransferInternal(Amount amount, const ChannelIDPtr& chID)
{
    auto& ch = m_channels[chID];
    std::string channelIdStr = to_hex(chID->m_pData, chID->nBytes);

    if (!ch)
    {
        LOG_ERROR() << "Channel: " << channelIdStr << " not found";
        return;
    }

    Block::SystemState::Full tip;
    get_History().get_Tip(tip);
    Height channelLockHeight = ch->get_LockHeight();

    if (tip.m_Height <= channelLockHeight)
    {
        if (ch->Transfer(amount))
        {
            LOG_INFO() << "Transfer: " << PrintableAmount(amount, true)
                    << " to channel: " << channelIdStr
                    << " started";
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
        LOG_ERROR() << "Current height: " << tip.m_Height
                    << " overtop channel lock height: " << channelLockHeight;
    }
       
    
    for (auto observer : m_observers)
    {
        observer->OnUpdateFinished(chID);
    }
}

void Mediator::CloseInternal(const ChannelIDPtr& chID)
{
    auto& ch = m_channels[chID];
    if (ch && CanBeClosed(ch->get_State()))
    {
        ch->Close();
        ch->LogNewState();
        if (ch->TransformLastState())
        {
            m_pWalletDB->saveLaserChannel(*ch);
        }
        return;
    }
    LOG_ERROR() << "Can't close channel: "
                << to_hex(chID->m_pData, chID->nBytes);
    for (auto observer : m_observers)
    {
        observer->OnCloseFailed(chID);
    }
}

void Mediator::ForgetChannel(const ChannelIDPtr& chID)
{
    auto it = m_channels.find(chID);
    if (it != m_channels.end())
    {
        auto& ch = it->second;
        auto state = ch->get_State();
        ch->Forget();
        if (state == Lightning::Channel::State::OpenFailed ||
            state == Lightning::Channel::State::None)
        {
            for (auto observer : m_observers)
            {
                observer->OnOpenFailed(chID);
            }
        }
        if (state == Lightning::Channel::State::Closed)
        {
            for (auto observer : m_observers)
            {
                observer->OnClosed(chID);
            }
        }
        m_channels.erase(it);
    }
}

ChannelIDPtr Mediator::RestoreChannel(const std::string& channelID)
{
    auto p_channelID = Channel::ChannelIdFromString(channelID);
    if (!p_channelID)
    {
        LOG_ERROR() << "Incorrect channel ID format: " << channelID;
        return nullptr;
    }

    bool isConnected = false;
    for (const auto& it : m_channels)
    {
        isConnected = *(it.first) == *p_channelID;
        if (isConnected)
        {
            p_channelID = it.first;
            LOG_INFO() << "Channel " << channelID << " already connected";
            break;
        }
    }

    if (!isConnected)
    {
        if (!RestoreChannelInternal(p_channelID))
        {
            return nullptr;
        }
    }

    return p_channelID;
}

bool Mediator::RestoreChannelInternal(const ChannelIDPtr& p_channelID)
{
    TLaserChannelEntity chDBEntity;
    if (m_pWalletDB->getLaserChannel(p_channelID, &chDBEntity) &&
        *p_channelID == std::get<LaserFields::LASER_CH_ID>(chDBEntity))
    {
        auto& myWID = std::get<LaserFields::LASER_MY_WID>(chDBEntity);
        auto myAddr = m_pWalletDB->getAddress(myWID, true);
        if (!myAddr) {
            LOG_ERROR() << "Can't load address from DB: "
                        << std::to_string(myWID);
            return false;
        }

        auto state = std::get<LaserFields::LASER_STATE>(chDBEntity);
        if (CanBeLoaded(state))
        {
            m_channels[p_channelID] = std::make_unique<Channel>(
                *this, p_channelID, *myAddr, chDBEntity);
            return true;
        }
        else
        {
            LOG_DEBUG() << "Channel "
                        << to_hex(p_channelID->m_pData , p_channelID->nBytes)
                        << " was closed or opened with failure";
            return false;
        }
    }
    LOG_INFO() << "Channel "
               << to_hex(p_channelID->m_pData , p_channelID->nBytes)
               << " not saved in DB";
    return false;
}

void Mediator::UpdateChannels()
{
    for (auto& it: m_channels)
    {
        auto& ch = it.second;
        auto state = ch->get_State();
        if (state == beam::Lightning::Channel::State::None) continue;
        if (state == beam::Lightning::Channel::State::Closed)
        {
            ch->LogNewState();
            PrepareToForget(ch);
            if (!ch->IsNegotiating() && ch->IsSafeToForget(kMaxRolbackHeight))
            {
                m_readyForForgetChannels.push_back(ch->get_chID());
            }
            continue;
        }
        if (state == Lightning::Channel::State::OpenFailed)
        {
            ch->LogNewState();
            m_readyForForgetChannels.push_back(ch->get_chID());
            continue;
        }

        ch->Update();
        ch->LogNewState();
        UpdateChannelExterior(ch);
    }
}

void Mediator::UpdateChannelExterior(const std::unique_ptr<Channel>& channel)
{
    auto lastState = channel->get_LastState();
    auto state = channel->get_State();
    if (channel->TransformLastState() &&
        state >= Lightning::Channel::State::Opening1)
    {
        channel->UpdateRestorePoint();
        m_pWalletDB->saveLaserChannel(*channel);
        
        if (state == Lightning::Channel::State::Open)
        {
            for (auto observer : m_observers)
            {
                observer->OnOpened(channel->get_chID());
            }
        }
        if (lastState == Lightning::Channel::State::Open &&
            state == Lightning::Channel::State::Updating)
        {
            for (auto observer : m_observers)
            {
                observer->OnUpdateStarted(channel->get_chID());
            }
        }
        if (lastState == Lightning::Channel::State::Updating &&
            state == Lightning::Channel::State::Open)
        {
            for (auto observer : m_observers)
            {
                observer->OnUpdateFinished(channel->get_chID());
            }
        }
    }
}

bool Mediator::ValidateTip()
{
    Block::SystemState::Full tip;
    get_History().get_Tip(tip);
    if (!IsValidTimeStamp(tip.m_TimeStamp))
    {
        LOG_ERROR() << "Tip timestamp not valid.";
        return false;
    }

    if (!tip.m_Height)
    {
        LOG_ERROR() << "Tip height is Zero.";
        return false;
    }

    Block::SystemState::ID id;
    tip.get_ID(id);
    m_pWalletDB->setSystemStateID(id);
    LOG_INFO() << "LASER Current state is " << id;
    return true;
}

void Mediator::PrepareToForget(const std::unique_ptr<Channel>& channel)
{
    if (channel->TransformLastState())
    {
        channel->Unsubscribe();
        channel->UpdateRestorePoint();
        m_pWalletDB->saveLaserChannel(*channel);
    }
}

}  // namespace beam::wallet::laser
