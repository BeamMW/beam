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

#include <memory>

#include "wallet/laser/i_receiver_holder.h"
#include "wallet/laser/types.h"
#include "core/fly_client.h"

namespace beam::wallet::laser
{
class Receiver
    : public proto::FlyClient::Request::IHandler
    , public proto::FlyClient::IBbsReceiver
{
public:
    using UPtr = std::unique_ptr<Receiver>;
    explicit Receiver(IReceiverHolder& holder, const ChannelIDPtr& chID);
    virtual ~Receiver();
    // proto::FlyClient::Request::IHandler
    virtual void OnComplete(proto::FlyClient::Request&) override;
    // proto::FlyClient::IBbsReceiver
    virtual void OnMsg(proto::BbsMsg&&) override;
private:
    IReceiverHolder& m_rHolder;
    ChannelIDPtr m_chID;
};

}  // namespace beam::wallet::laser
