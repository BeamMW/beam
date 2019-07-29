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

#include "core/lightning.h"
#include "wallet/laser/receiver.h"
#include "wallet/wallet_db.h"
#include "wallet/common.h"

namespace beam::wallet::laser
{
class Channel : public Lightning::Channel
{
public:
    struct Codes
    {
        static const uint32_t Control0 = 1024 << 16;
        static const uint32_t MyWid = Control0 + 31;
    };
    using FieldMap = std::map<uint32_t, ByteBuffer>;

    Channel(const std::shared_ptr<proto::FlyClient::INetwork>& net,
            const IWalletDB::Ptr& walletDB,
            Receiver& receiver)
            : m_net(net), m_WalletDB(walletDB), m_rReceiver(receiver) {};
    Channel(const Channel&) = delete;
    void operator=(const Channel&) = delete;
    // LightningChannel(LightningChannel&& channel) { m_net = std::move(channel.m_net);};
    // void operator=(LightningChannel&& channel) { m_net = std::move(channel.m_net);};
    ~Channel();

    Height get_Tip() const override;
    proto::FlyClient::INetwork& get_Net() override;
    void get_Kdf(Key::IKdf::Ptr&) override;
    void AllocTxoID(Key::IDV&) override;
    Amount SelectInputs(
            std::vector<Key::IDV>& vInp, Amount valRequired) override;
    void SendPeer(Negotiator::Storage::Map&& dataOut) override;
    void LogNewState();

    uintBig_t<16> m_ID;
    WalletID m_widTrg;
    WalletID m_widMy;
    std::shared_ptr<proto::FlyClient::INetwork> m_net;
    IWalletDB::Ptr m_WalletDB;
    bool m_SendMyWid = true;
    Receiver& m_rReceiver;

    beam::Lightning::Channel::State::Enum m_LastState = State::None;
};
}  // namespace beam::wallet::laser
