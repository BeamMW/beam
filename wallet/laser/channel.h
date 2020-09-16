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

    using Ptr = std::unique_ptr<Channel>;
    static ChannelIDPtr ChannelIdFromString(const std::string& chIdStr);

    Channel(IChannelHolder& holder,
            const WalletAddress& myAddr,
            const WalletID& trg,
            const Amount& aMy,
            const Amount& aTrg,
            const Lightning::Channel::Params& params = {});
    Channel(IChannelHolder& holder,
            const ChannelIDPtr& chID,
            const WalletAddress& myAddr,
            const WalletID& trg,
            const Amount& aMy,
            const Amount& aTrg,
            const Lightning::Channel::Params& params = {});
    Channel(IChannelHolder& holder,
            const ChannelIDPtr& chID,
            const WalletAddress& myAddr,
            const TLaserChannelEntity& entity,
            const Lightning::Channel::Params& params = {});
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
    void OnCoin(const CoinID&,
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

    bool Open(Height hOpenTxDh);
    bool TransformLastState();
    int get_LastState() const;
    void UpdateRestorePoint();
    void LogState();
    void Subscribe();
    void Unsubscribe();
    bool IsSafeToClose() const;
    bool IsUpdateStuck() const;
    bool IsGracefulCloseStuck() const;
    bool IsSubscribed() const;

private:
    void RestoreInternalState(const ByteBuffer& data);

    IChannelHolder& m_rHolder;

    bool m_SendMyWid = true;
    int m_lastState = State::None;

    ChannelIDPtr m_ID;
    WalletAddress m_myAddr;
    WalletID m_widTrg;
    Amount m_aMy;
    Amount m_aTrg;
    Amount m_aCurMy;
    Amount m_aCurTrg;
    Height m_lockHeight = MaxHeight;
    Timestamp m_bbsTimestamp;
    Height m_lastUpdateStart = 0;
    
    std::unique_ptr<Receiver> m_upReceiver;
    ByteBuffer m_data;
    bool m_isSubscribed = false;
};
}  // namespace beam::wallet::laser
