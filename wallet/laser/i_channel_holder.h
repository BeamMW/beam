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

namespace beam::wallet::laser
{
class Channel;

class IChannelHolder
{
public:
    virtual IWalletDB::Ptr getWalletDB() = 0;
    virtual proto::FlyClient::INetwork& get_Net() = 0;
    virtual IRawCommGateway& get_Gateway() = 0;
    virtual void UpdateChannelExterior(Channel&) = 0;
};
}  // namespace beam::wallet::laser
