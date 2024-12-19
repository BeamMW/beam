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
        static const uint32_t ChannelID = Control0 + 32;
    };

    using Ptr = std::unique_ptr<Channel>;
    static ChannelIDPtr ChannelIdFromString(const std::string& chIdStr);

    Channel(IChannelHolder& holder,
            uint64_t bbsID,
            const WalletID& trg,
            const Amount& aMy,
            const Amount& aTrg,
            const Lightning::Channel::Params& params = {});
    Channel(IChannelHolder& holder,
            const ChannelIDPtr& chID,
            uint64_t bbsID,
            const WalletID& trg,
            const Amount& aMy,
            const Amount& aTrg,
            const Lightning::Channel::Params& params = {});
    Channel(IChannelHolder& holder,
            const ChannelIDPtr& chID,
            uint64_t bbsID,
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
    uint64_t get_ownID() const override;
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
    
    void OnPeerDataEx(Negotiator::Storage::Map&);

    bool m_SendMyWid = false;
    bool m_SendChannelID = false;

private:
    void RestoreInternalState(const ByteBuffer& data);
    void EnsureMyBbsParams();
    void UnsubscribeInternal();

    IChannelHolder& m_rHolder;

    struct CommHandler
        :public IRawCommGateway::IHandler
    {
        void OnMsg(const Blob&) override;
        IMPLEMENT_GET_PARENT_OBJ(Channel, m_CommHandler)
    } m_CommHandler;

    int m_lastState = State::None;

    ChannelIDPtr m_ID;
    uint64_t m_bbsID;
    WalletID m_widTrg;
    Amount m_aMy;
    Amount m_aTrg;
    Amount m_aCurMy;
    Amount m_aCurTrg;
    Height m_lockHeight = MaxHeight;
    Timestamp m_bbsTimestamp;
    Height m_lastUpdateStart = 0;
    
    ByteBuffer m_data;
    bool m_isSubscribed = false;
    bool m_bHaveMyParams = false;

    WalletID m_widMy;
    ECC::Scalar::Native m_skMy;
};
}  // namespace beam::wallet::laser
