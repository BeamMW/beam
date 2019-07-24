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

#include "client_mediator.h"
// #include "lightning_channel.h"
#include "utility/logger.h"

namespace beam::wallet::lightning
{
ClientMediator::ClientMediator(IWalletDB::Ptr walletDB,
                               const beam::io::Address& nodeAddr)
    : m_pWalletDB(walletDB)
    , m_pListener(std::make_unique<LaserListener>(*this))
    , m_pConnection(std::make_shared<LaserConnection>(*m_pListener))
{
    // m_listener = std::make_unique<LaserListener>(*this);
    // m_connection = std::make_unique<LaserConnection>(*m_listener);
}

void ClientMediator::OnNewTip()
{
    std::cout << "OnNewTip" << std::endl;
}

Block::SystemState::IHistory& ClientMediator::get_History()
{
    return m_pWalletDB->get_History();
}

void ClientMediator::OpenChannel(Amount aMy,
                                 Amount aTrg,
                                 Amount fee,
                                 const WalletID& receiverWalletID,
                                 Height locktime)
{
    auto& ch = m_channels.emplace_back(
            std::make_unique<LightningChannel>(m_pConnection, m_pWalletDB, *m_pListener));
    ECC::GenRandom(ch->m_ID);
    ch->m_widTrg = receiverWalletID;
    ch->m_Params.m_hLockTime = 15;
    ch->m_Params.m_Fee = fee;

    Block::SystemState::Full tip;
    get_History().get_Tip(tip);

    HeightRange hr;
    hr.m_Min = tip.m_Height;
    hr.m_Max = hr.m_Min + locktime;

    if (ch->Open(aMy, aTrg, hr))
    {
        LOG_INFO() << "Laser open";
    }
    else
    {
        LOG_INFO() << "Laser fail";
    }
}

// Height ClientMediator::get_TipHeight() const
// {

// }
}  // namespace beam::wallet::lightning
