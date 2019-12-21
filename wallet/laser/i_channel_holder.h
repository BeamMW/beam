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

#include "wallet/core/wallet_db.h"
#include "core/fly_client.h"
#include "wallet/laser/i_receiver_holder.h"

namespace beam::wallet::laser
{
class IChannelHolder : public IReceiverHolder
{
public:
    virtual IWalletDB::Ptr getWalletDB() = 0;
    virtual proto::FlyClient::INetwork& get_Net() = 0;
    virtual void OnMsg(const ChannelIDPtr& chID, Blob&& blob) override = 0;
    virtual bool Decrypt(const ChannelIDPtr& chID,
                         uint8_t* pMsg,
                         Blob* blob) override = 0;
};
}  // namespace beam::wallet::laser
