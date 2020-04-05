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
#include "core/fly_client.h"
#include "wallet/core/bbs_miner.h"

namespace beam::wallet::laser
{
using proto::FlyClient;
class Connection final : public FlyClient::INetwork
{
public:
    explicit Connection(const FlyClient::NetworkStd::Ptr& net, bool mineOutgoing = true);
    ~Connection();

    virtual void Connect() override;
    virtual void Disconnect() override;
    virtual void PostRequestInternal(FlyClient::Request& r) override;
    virtual void BbsSubscribe(
            BbsChannel ch,
            Timestamp timestamp,
            FlyClient::IBbsReceiver* receiver) override;
private:
    void OnMined();
    void MineBbsRequest(FlyClient::RequestBbsMsg& r);

    FlyClient::NetworkStd::Ptr m_pNet;

    BbsMiner m_Miner;
    bool m_MineOutgoing;
    std::unordered_map<
        BbsMiner::Task::Ptr,
        proto::FlyClient::Request::IHandler*> m_handlers;
};
}  // namespace beam::wallet::laser
