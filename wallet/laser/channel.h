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
#include "wallet/core/wallet_db.h"
#include "wallet/core/common.h"

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

    static ChannelIDPtr ChIdFromString(const std::string& chIdStr);

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
    Channel(IChannelHolder& holder,
            const ChannelIDPtr& chID,
            const WalletAddress& myAddr,
            const TLaserChannelEntity& entity);
    Channel(const Channel&) = delete;
    void operator=(const Channel&) = delete;
    Channel(Channel&& channel) = delete;
    void operator=(Channel&& channel) = delete;
    ~Channel();

    // beam::Lightning::Channel implementation
    Height get_Tip() const override;
    proto::FlyClient::INetwork& get_Net() override;
    void get_Kdf(Key::IKdf::Ptr&) override;
    void AllocTxoID(CoinID&) override;
    Amount SelectInputs(
            std::vector<CoinID>& vInp, Amount valRequired, Asset::ID nAssetID) override;
    void SendPeer(Negotiator::Storage::Map&& dataOut) override;
    void OnCoin(const CoinID& kidv,
                Height h,
                CoinState eState,
                bool bReverse) override;

    // ILaserChannelEntity implementation
    const ChannelIDPtr& get_chID() const override;
    const WalletID& get_myWID() const override;
    const WalletID& get_trgWID() const override;
    const Amount& get_fee() const override;
    const Height& getLocktime() const override;
    const Amount& get_amountMy() const override;
    const Amount& get_amountTrg() const override;
    const Amount& get_amountCurrentMy() const override;
    const Amount& get_amountCurrentTrg() const override;
    int get_State() const override;
    const Height& get_LockHeight() const override;
    const Timestamp& get_BbsTimestamp() const override;
    const ByteBuffer& get_Data() const override;
    const WalletAddress& get_myAddr() const override;

    bool Open(HeightRange openWindow);
    bool TransformLastState();
    Lightning::Channel::State::Enum get_LastState() const;
    void UpdateRestorePoint();
    void LogNewState();
    void Subscribe();
    void Unsubscribe();

protected:
    bool TransferInternal(
        Amount nMyNew, uint32_t iRole, bool bCloseGraceful) override;

private:
    void RestoreInternalState(const ByteBuffer& data);

    IChannelHolder& m_rHolder;

    bool m_SendMyWid = true;
    beam::Lightning::Channel::State::Enum m_lastState = State::None;
    beam::Lightning::Channel::State::Enum m_lastLoggedState = State::None;

    ChannelIDPtr m_ID;
    WalletAddress m_myAddr;
    WalletID m_widTrg;
    Amount m_aMy;
    Amount m_aTrg;
    Amount m_aCurMy;
    Amount m_aCurTrg;
    Height m_lockHeight = MaxHeight;
    Timestamp m_bbsTimestamp;
    
    std::unique_ptr<Receiver> m_upReceiver;
    ByteBuffer m_data;
    bool m_gracefulClose = false;
};
}  // namespace beam::wallet::laser
