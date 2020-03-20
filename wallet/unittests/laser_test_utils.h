// Copyright 2020 The Beam Team
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

#include <functional>
#include <memory>

#include "wallet/core/wallet_network.h"
#include "wallet/laser/mediator.h"

using namespace beam::wallet;

const uint16_t kDefaultTestNodePort = 32125;
const Height kMaxTestHeight = 360;
const Height kRevisionMaxLifeTime = 71;
const Height kLockTime = 5;
const Height kPostLockReserve = 30;
const Amount kFee = 100;
const Height kOpenTxDh = 70;
const Height kStartBlock = 3;
const unsigned kNewBlockInterval = 1000;

struct LaserObserver : public laser::Mediator::Observer
{
    using Action = std::function<void(const laser::ChannelIDPtr& chID)>;
    Action onOpened = [] (const laser::ChannelIDPtr& chID) {};
    Action onOpenFailed = [] (const laser::ChannelIDPtr& chID) {};
    Action onClosed = [] (const laser::ChannelIDPtr& chID) {};
    Action onUpdateFinished = [] (const laser::ChannelIDPtr& chID) {};
    Action onCloseFailed = [] (const laser::ChannelIDPtr& chID) {};
    Action onTransferFailed = Action();
    void OnOpened(const laser::ChannelIDPtr& chID) override
    {
        onOpened(chID);
    }
    void OnOpenFailed(const laser::ChannelIDPtr& chID) override
    {
        onOpenFailed(chID);
    }
    void OnClosed(const laser::ChannelIDPtr& chID) override
    {
        onClosed(chID);
    }
    void OnCloseFailed(const laser::ChannelIDPtr& chID) override
    {
        onCloseFailed(chID);
    }
    void OnUpdateFinished(const laser::ChannelIDPtr& chID) override
    {
        onUpdateFinished(chID);
    }
    void OnTransferFailed(const laser::ChannelIDPtr& chID) override
    {
        onTransferFailed(chID);
    }
};

proto::FlyClient::NetworkStd::Ptr CreateNetwork(proto::FlyClient& fc, uint16_t port = kDefaultTestNodePort)
{
    io::Address nodeAddress = io::Address::localhost().port(port);
    auto nnet = std::make_shared<proto::FlyClient::NetworkStd>(fc);
    nnet->m_Cfg.m_PollPeriod_ms = 0;
    nnet->m_Cfg.m_vNodes.push_back(nodeAddress);
    nnet->Connect();

    return nnet;
};

void ConfigureNetwork(laser::Mediator& laserFirst, laser::Mediator& laserSecond)
{
    laserFirst.SetNetwork(CreateNetwork(laserFirst), false);
    laserSecond.SetNetwork(CreateNetwork(laserSecond), false);
}
