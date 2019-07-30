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
#include "wallet/common_utils.h"
#include "wallet/laser/connection.h"
#include "wallet/laser/receiver.h"
#include "utility/logger.h"

namespace
{

}  // namespace

namespace beam::wallet::laser
{
Mediator::Mediator(IWalletDB::Ptr walletDB,
                   std::shared_ptr<proto::FlyClient::NetworkStd>& net)
    : m_pWalletDB(walletDB)
    , m_pReceiver(std::make_unique<Receiver>(*this))
    , m_pConnection(std::make_shared<Connection>(net))
{
    m_myAddr.m_walletID = Zero;
    m_pConnection->Connect();
}

void Mediator::OnMsg(Blob&& blob)
{
    uintBig_t<16> key;
	beam::Negotiator::Storage::Map dataIn;

	try {
		Deserializer der;
		der.reset(blob.p, blob.n);

		der & key;
		der & Cast::Down<Channel::FieldMap>(dataIn);
	}
	catch (const std::exception&) {
		return;
	}

    if (!m_lch)
    {
        WalletID wid;
        if (!dataIn.Get(wid, Channel::Codes::MyWid))
            return;

        m_lch = std::make_unique<Channel>(m_pConnection, m_pWalletDB, *m_pReceiver);
        m_lch->m_ID = key;
        m_lch->m_widTrg = wid;
    }

    m_lch->OnPeerData(dataIn);
    m_lch->LogNewState();
    // TODO: save channel
    if (!m_lch->IsNegotiating() && m_lch->IsSafeToForget(8))
    {
        m_lch->Forget();
        m_lch.reset();
        // TODO: save channel
    }
}

bool  Mediator::Decrypt(uint8_t* pMsg, Blob* blob)
{
    if (!proto::Bbs::Decrypt(pMsg, blob->n, get_skBbs()))
		return false;

	blob->p = pMsg;
    return true;
}

void Mediator::OnRolledBack()
{
    if (m_lch)
    {
        m_lch->OnRolledBack();
    }
}

void Mediator::OnNewTip()
{
    if (m_lch)
    {
        m_lch->Update();
        m_lch->LogNewState();
        if (!m_lch->IsNegotiating() && m_lch->IsSafeToForget(8))
        {
            m_lch->Forget();
            m_lch.reset();
        }
    }
}

void Mediator::Listen()
{
    m_myAddr = GenerateNewAddress(
        m_pWalletDB,
        "laser_in",
        WalletAddress::ExpirationStatus::Never,
        false);

    BbsChannel ch;
    m_myAddr.m_walletID.m_Channel.Export(ch);
    m_pConnection->BbsSubscribe(ch, getTimestamp(), m_pReceiver.get());
}

void Mediator::OpenChannel(Amount aMy,
                           Amount aTrg,
                           Amount fee,
                           const WalletID& receiverWalletID,
                           Height locktime)
{
    // auto& ch = m_channels.emplace_back(
    //         std::make_unique<Channel>(m_pConnection, m_pWalletDB, *m_pReceiver, *m_pReceiver));
    if (m_myAddr.m_walletID == Zero)
    {
        m_myAddr = GenerateNewAddress(
            m_pWalletDB,
            "laser_out",
            WalletAddress::ExpirationStatus::Never,
            false);        
    }

    m_is_opener = true;

    m_lch = std::make_unique<Channel>(m_pConnection, m_pWalletDB, *m_pReceiver);
    auto* ch = m_lch.get();
    ECC::GenRandom(ch->m_ID);
    ch->m_widMy = m_myAddr.m_walletID;
    ch->m_widTrg = receiverWalletID;
    ch->m_Params.m_hLockTime = locktime;
    ch->m_Params.m_Fee = fee;

    Block::SystemState::Full tip;
    get_History().get_Tip(tip);

    HeightRange openWindow;
    openWindow.m_Min = tip.m_Height;
    openWindow.m_Max = openWindow.m_Min + 8;

    m_initial_height = openWindow.m_Min;

    if (ch->Open(aMy, aTrg, openWindow))
    {
        LOG_INFO() << "Laser open";
        // TODO: save channel
    }
    else
    {
        LOG_INFO() << "Laser fail";
    }
}

void Mediator::Close()
{
    if (m_lch)
    {
        m_lch->Close();
    }
}

Block::SystemState::IHistory& Mediator::get_History()
{
    return m_pWalletDB->get_History();
}

ECC::Scalar::Native Mediator::get_skBbs()
{
    auto& wid = m_myAddr.m_walletID;
    if (wid != Zero)
    {    
        PeerID peerID;
        ECC::Scalar::Native sk;
        m_pWalletDB->get_MasterKdf()->DeriveKey(sk, Key::ID(m_myAddr.m_OwnID, Key::Type::Bbs));
        proto::Sk2Pk(peerID, sk);
        return m_myAddr.m_walletID.m_Pk == peerID ? sk : Zero;        
    }
    return Zero;
}

}  // namespace beam::wallet::laser
