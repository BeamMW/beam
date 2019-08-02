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
#include "wallet/common_utils.h"
#include "wallet/laser/connection.h"
#include "wallet/laser/receiver.h"
#include "utility/helpers.h"
#include "utility/logger.h"

namespace
{

}  // namespace

namespace beam::wallet::laser
{
Mediator::Mediator(const IWalletDB::Ptr& walletDB,
                   const proto::FlyClient::NetworkStd::Ptr& net)
    : m_pWalletDB(walletDB)
    , m_pConnection(std::make_shared<Connection>(net))
{
    m_myOutAddr.m_walletID = m_myInAddr.m_walletID = Zero;
    m_pConnection->Connect();
}

Mediator::~Mediator()
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

    if (!chID)
    {
        OnIncoming(inChID, dataIn);
    }
    else
    {
        if (*chID != *inChID)
        {
            LOG_ERROR() << "Wrong channel ID: "
                        << to_hex(inChID->m_pData , inChID->nBytes);
            return;
        }
        inChID = chID;
    }

    auto it = m_channels.find(inChID);
    if (it == m_channels.end())
    {
        LOG_ERROR() << "Channel not found ID: "
                    << to_hex(inChID->m_pData , inChID->nBytes);
        return;
    }
    auto& ch = it->second;

    ch->OnPeerData(dataIn);
    ch->LogNewState();
    // TODO: save channel
    if (!ch->IsNegotiating() && ch->IsSafeToForget(8))
    {
        ch->Forget();
        ch.reset();
        // TODO: save channel
    }
}

bool  Mediator::Decrypt(const ChannelIDPtr& chID, uint8_t* pMsg, Blob* blob)
{
    if (!proto::Bbs::Decrypt(pMsg, blob->n, get_skBbs(chID)))
		return false;

	blob->p = pMsg;
    return true;
}

void Mediator::OnRolledBack()
{
    for (auto& it: m_channels)
    {
        it.second->OnRolledBack();
    }
}

void Mediator::OnNewTip()
{
    for (auto& it: m_channels)
    {
        auto& ch = it.second;
        ch->Update();
        ch->LogNewState();
        if (!ch->IsNegotiating() && ch->IsSafeToForget(8))
        {
            ch->Forget();
            ch.reset();
        }
    }
}

void Mediator::WaitIncoming()
{
    m_pInputReceiver = std::make_unique<Receiver>(*this, nullptr);
    m_myInAddr = GenerateNewAddress(
        m_pWalletDB,
        "laser_in",
        WalletAddress::ExpirationStatus::Never,
        false);

    BbsChannel ch;
    m_myInAddr.m_walletID.m_Channel.Export(ch);
    m_pConnection->BbsSubscribe(ch, getTimestamp(), m_pInputReceiver.get());
    LOG_INFO() << "beam::wallet::laser::Mediator subscribed: " << ch;
}

void Mediator::OpenChannel(Amount aMy,
                           Amount aTrg,
                           Amount fee,
                           const WalletID& receiverWalletID,
                           Height locktime)
{
    if (m_myOutAddr.m_walletID == Zero)
    {
        m_myOutAddr = GenerateNewAddress(
            m_pWalletDB,
            "laser_out",
            WalletAddress::ExpirationStatus::Never,
            false);        
    }

    auto channel = std::make_unique<Channel>(
        *this, m_myOutAddr, receiverWalletID,
        fee, aMy, aTrg, locktime);

    auto chIDPtr = channel->get_chID();
    m_channels[chIDPtr] = std::move(channel);
    
    auto& ch = m_channels[chIDPtr];

    Block::SystemState::Full tip;
    get_History().get_Tip(tip);

    HeightRange openWindow;
    openWindow.m_Min = tip.m_Height;
    openWindow.m_Max = openWindow.m_Min + kDefaultLaserOpenTime;

    if (ch->Open(openWindow))
    {
        LOG_INFO() << "Laser open start: "
                   << to_hex(ch->get_chID()->m_pData, ch->get_chID()->nBytes);
        if (ch->IsStateChanged())
        {
            m_pWalletDB->saveLaserChannel(*ch);
            m_pWalletDB->saveAddress(m_myOutAddr, true);
        }
    }
    else
    {
        LOG_ERROR() << "Laser open channel fail";
    }
}

void Mediator::Close(const std::string& channelIDStr)
{
    // if (m_lch)
    // {
    //     m_lch->Close();
    // }
}

Block::SystemState::IHistory& Mediator::get_History()
{
    return m_pWalletDB->get_History();
}

ECC::Scalar::Native Mediator::get_skBbs(const ChannelIDPtr& chID)
{
    auto& addr = chID ? m_channels[chID]->getMyAddr() : m_myInAddr;
    auto& wid = addr.m_walletID;
    if (wid != Zero)
    {    
        PeerID peerID;
        ECC::Scalar::Native sk;
        m_pWalletDB->get_MasterKdf()->DeriveKey(
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

    Amount aMy;
    if (!dataIn.Get(aMy, beam::Lightning::Codes::ValueYours))
        return;
    
    Amount aTrg;
    if (!dataIn.Get(aTrg, beam::Lightning::Codes::ValueMy))
        return;

    Height locktime;
    if (!dataIn.Get(locktime, beam::Lightning::Codes::HLock))
        return;           

    BbsChannel ch;
    m_myInAddr.m_walletID.m_Channel.Export(ch);
    m_pConnection->BbsSubscribe(ch, 0, nullptr);
    LOG_INFO() << "beam::wallet::laser::Mediator unsubscribed: " << ch;

    m_channels[chID] = std::make_unique<Channel>(
        *this, chID, m_myInAddr, trgWid, fee, aMy, aTrg, locktime);

    m_pInputReceiver.reset();
};

}  // namespace beam::wallet::laser
