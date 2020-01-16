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

#ifndef LOG_VERBOSE_ENABLED
    #define LOG_VERBOSE_ENABLED 0
#endif

#include <boost/filesystem.hpp>
#include "utility/logger.h"
#include "keykeeper/local_private_key_keeper.h"
#include "wallet/core/wallet_network.h"
#include "wallet/core/simple_transaction.h"
#include "wallet/laser/mediator.h"
#include "node/node.h"
#include "core/unittest/mini_blockchain.h"
#include "utility/test_helpers.h"
#include "test_helpers.h"

WALLET_TEST_INIT
#include "wallet_test_environment.cpp"

using namespace beam;
using namespace beam::wallet;
using namespace std;

namespace {

class LaserObserver : public laser::Mediator::Observer
{
public:
    void OnOpened(const laser::ChannelIDPtr& chID) override
    {
    }
    void OnOpenFailed(const laser::ChannelIDPtr& chID) override
    {
    }
    void OnClosed(const laser::ChannelIDPtr& chID) override
    {
    }
    void OnUpdateStarted(const laser::ChannelIDPtr& chID) override
    {
    } 
    void OnUpdateFinished(const laser::ChannelIDPtr& chID) override
    {
    } 
};

proto::FlyClient::NetworkStd::Ptr CreateNetwork(proto::FlyClient& fc)
{
    io::Address nodeAddress = io::Address::localhost().port(32125);
    auto nnet = make_shared<proto::FlyClient::NetworkStd>(fc);
    nnet->m_Cfg.m_PollPeriod_ms = 0;
    nnet->m_Cfg.m_vNodes.push_back(nodeAddress);
    nnet->Connect();

    return nnet;
}

}  // namespace

int main()
{
    int logLevel = LOG_LEVEL_DEBUG;
#if LOG_VERBOSE_ENABLED
    logLevel = LOG_LEVEL_VERBOSE;
#endif
    const auto path = boost::filesystem::system_complete("logs");
    auto logger = Logger::create(logLevel, logLevel, logLevel, "laser_test", path.string());

    Rules::get().pForks[1].m_Height = 1;
	Rules::get().FakePoW = true;
	Rules::get().UpdateChecksum();

    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    auto wdbFirst = createSqliteWalletDB("first.db", false, false);
    auto wdbSecond = createSqliteWalletDB("second.db", false, false);

    const AmountList amounts = {100000000, 100000000, 100000000, 100000000};
    for (auto amount : amounts)
    {
        Coin coin = CreateAvailCoin(amount, 7);
        wdbFirst->storeCoin(coin);
        wdbSecond->storeCoin(coin);
    }

    auto keyKeeperFirst = make_shared<LocalPrivateKeyKeeper>(wdbFirst, wdbFirst->get_MasterKdf());
    auto keyKeeperSecond = make_shared<LocalPrivateKeyKeeper>(wdbSecond, wdbSecond->get_MasterKdf());

    auto laserFirst = std::make_unique<laser::Mediator>(wdbFirst, keyKeeperFirst);
    auto laserSecond = std::make_unique<laser::Mediator>(wdbSecond, keyKeeperSecond);

    LaserObserver firstObserver, secondObserver;
    laserFirst->AddObserver(&firstObserver);
    laserSecond->AddObserver(&secondObserver);

    auto newBlockFunc = [&laserFirst, &laserSecond] (Height height)
    {
        LOG_INFO() << "Client 1 laser channels count: " << laserFirst->getChannelsCount();
        LOG_INFO() << "Client 2 laser channels count: " << laserSecond->getChannelsCount();
        if (height == 3)
        {
            auto firstWalletID = laserFirst->getWaitingWalletID();

            LOG_INFO() << "Try open channel with ID: " << to_string(firstWalletID);
            laserSecond->OpenChannel(20000, 10000, 100, firstWalletID, 42);
        }
        if (height == 8)
        {
            auto firstWalletID = laserFirst->getWaitingWalletID();

            LOG_INFO() << "Try open channel with ID: " << to_string(firstWalletID);
            laserSecond->OpenChannel(20000, 10000, 100, firstWalletID, 42);
        }
        if (height == 17)
        {
            LOG_INFO() << "Client 1 channels count: " << laserFirst->getChannelsCount();
            LOG_INFO() << "Client 2 channels count: " << laserSecond->getChannelsCount();
        }
    };

    TestNode node(newBlockFunc, 1);
    io::Timer::Ptr timer = io::Timer::create(*mainReactor);
    timer->start(1000, true, [&node]() {node.AddBlock(); });

    laserFirst->SetNetwork(CreateNetwork(*laserFirst));
    laserSecond->SetNetwork(CreateNetwork(*laserSecond));

    laserFirst->WaitIncoming(10000, 100, 42);

    mainReactor->run();

    return WALLET_CHECK_RESULT;
}
