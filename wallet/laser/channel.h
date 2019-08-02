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
#include "wallet/laser/i_channel_holder.h"
#include "wallet/laser/receiver.h"
#include "wallet/laser/types.h"
#include "wallet/wallet_db.h"
#include "wallet/common.h"

namespace beam::wallet::laser
{
class Channel : public Lightning::Channel, public ILaserChannelEntity
{
public:
    struct Codes
    {
        static const uint32_t Control0 = 1024 << 16;
        static const uint32_t MyWid = Control0 + 31;
    };

    Channel(IChannelHolder& holder,
            const WalletAddress& myAddr,
            const WalletID& trg,
            const Amount& fee,
            const Amount& aMy,
            const Amount& aTrg,
            Height locktime);
    Channel(IChannelHolder& holder,
            const ChannelIDPtr& chID,
            const WalletAddress& myAddr,
            const WalletID& trg,
            const Amount& fee,
            const Amount& aMy,
            const Amount& aTrg,
            Height locktime);
    Channel(const Channel&) = delete;
    void operator=(const Channel&) = delete;
    // LightningChannel(LightningChannel&& channel) { m_net = std::move(channel.m_net);};
    // void operator=(LightningChannel&& channel) { m_net = std::move(channel.m_net);};
    ~Channel();

    // beam::Lightning::Channel implementation
    Height get_Tip() const override;
    proto::FlyClient::INetwork& get_Net() override;
    void get_Kdf(Key::IKdf::Ptr&) override;
    void AllocTxoID(Key::IDV&) override;
    Amount SelectInputs(
            std::vector<Key::IDV>& vInp, Amount valRequired) override;
    void SendPeer(Negotiator::Storage::Map&& dataOut) override;

    // ILaserChannelEntity implementation
    const ChannelIDPtr& get_chID() const override;
    const WalletID& get_myWID() const override;
    const WalletID& get_trgWID() const override;
    int get_lastState() const override;
    const Amount& get_fee() const override;
    const Amount& get_amountMy() const override;
    const Amount& get_amountTrg() const override;
    const Amount& get_amountCurrentMy() const override;
    const Amount& get_amountCurrentTrg() const override;

    bool Open(HeightRange openWindow);
    const WalletAddress& getMyAddr() const;
    bool IsStateChanged();
    void LogNewState();

    bool m_SendMyWid = true;
    beam::Lightning::Channel::State::Enum m_LastState = State::None;
private:
    void Subscribe();
    void Unsubscribe();

    IChannelHolder& m_rHolder;

    ChannelIDPtr m_ID;
    WalletAddress m_myAddr;
    WalletID m_widTrg;
    Amount m_aMy;
    Amount m_aTrg;
    
    std::unique_ptr<Receiver> m_upReceiver;
};
}  // namespace beam::wallet::laser
