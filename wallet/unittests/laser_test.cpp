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

struct LaserObserver : public laser::Mediator::Observer
{
    using Action = std::function<void(const laser::ChannelIDPtr& chID)>;
    Action onOpened = Action();
    Action onOpenFailed = Action();
    Action onClosed = Action();
    Action onUpdateStarted = Action();
    Action onUpdateFinished = Action();
    Action onCloseFailed = Action();
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
    void OnUpdateStarted(const laser::ChannelIDPtr& chID) override
    {
        onUpdateStarted(chID);
    } 
    void OnUpdateFinished(const laser::ChannelIDPtr& chID) override
    {
        onUpdateFinished(chID);
    }
} observer_1, observer_2;

proto::FlyClient::NetworkStd::Ptr CreateNetwork(proto::FlyClient& fc)
{
    io::Address nodeAddress = io::Address::localhost().port(32125);
    auto nnet = make_shared<proto::FlyClient::NetworkStd>(fc);
    nnet->m_Cfg.m_PollPeriod_ms = 0;
    nnet->m_Cfg.m_vNodes.push_back(nodeAddress);
    nnet->Connect();

    return nnet;
}

void ResetObservers()
{
    observer_1.onOpened = observer_2.onOpened = LaserObserver::Action();
    observer_1.onOpenFailed = observer_2.onOpenFailed = LaserObserver::Action();
    observer_1.onClosed = observer_2.onClosed = LaserObserver::Action();
    observer_1.onCloseFailed = observer_2.onCloseFailed = LaserObserver::Action();
    observer_1.onUpdateStarted = observer_2.onUpdateStarted = LaserObserver::Action();
    observer_1.onUpdateFinished = observer_2.onUpdateFinished = LaserObserver::Action();
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
    }
    for (auto amount : amounts)
    {
        Coin coin = CreateAvailCoin(amount, 10);
        wdbSecond->storeCoin(coin);
    }

    auto keyKeeperFirst = make_shared<LocalPrivateKeyKeeper>(wdbFirst, wdbFirst->get_MasterKdf());
    auto keyKeeperSecond = make_shared<LocalPrivateKeyKeeper>(wdbSecond, wdbSecond->get_MasterKdf());

    auto laserFirst = std::make_unique<laser::Mediator>(wdbFirst, keyKeeperFirst);
    auto laserSecond = std::make_unique<laser::Mediator>(wdbSecond, keyKeeperSecond);

    laserFirst->AddObserver(&observer_1);
    laserSecond->AddObserver(&observer_2);

    struct CheckResults {
        storage::Totals::AssetTotals totals_1, totals_1_a, totals_2, totals_2_a;
        laser::ChannelIDPtr channel_1 = nullptr, channel_2 = nullptr;
        bool test3Success = false;
    } resultsForCheck;

    auto newBlockFunc = [&laserFirst, &laserSecond, &resultsForCheck] (Height height)
    {
        if (height == 2)
        {
            LOG_INFO() << "Test 1: both sides open without coins";

            observer_1.onOpened = observer_2.onOpened =
                [] (const laser::ChannelIDPtr& chID)
                {
                    WALLET_CHECK(false);
                };
            observer_1.onOpenFailed = observer_2.onOpenFailed =
                [] (const laser::ChannelIDPtr& chID)
                {
                    WALLET_CHECK(true);
                };

            laserFirst->WaitIncoming(100000000, 100000000, 100, 10);
            auto firstWalletID = laserFirst->getWaitingWalletID();
            laserSecond->OpenChannel(100000000, 100000000, 100, firstWalletID, 10);
        }
        if (height == 4)
        {
            laserFirst->StopWaiting();
            ResetObservers();
        }
        if (height == 8)
        {
            LOG_INFO() << "Test 2: second side open without coins";
            observer_1.onOpened =
                [] (const laser::ChannelIDPtr& chID)
                {
                    LOG_INFO() << "Test 2: first side opened";
                    WALLET_CHECK(true);
                };
            observer_1.onOpenFailed =
                [] (const laser::ChannelIDPtr& chID)
                {
                    LOG_INFO() << "Test 2: first side open failed";
                    WALLET_CHECK(false);
                };

            observer_2.onOpened =
                [] (const laser::ChannelIDPtr& chID)
                {
                    LOG_INFO() << "Test 2: second side opened";
                    WALLET_CHECK(false);
                };
            observer_2.onOpenFailed =
                [] (const laser::ChannelIDPtr& chID)
                {
                    LOG_INFO() << "Test 2: second side open failed";
                    WALLET_CHECK(true);
                };

            laserFirst->WaitIncoming(100000000, 100000000, 100, 10);
            auto firstWalletID = laserFirst->getWaitingWalletID();
            laserSecond->OpenChannel(100000000, 100000000, 100, firstWalletID, 10);
        }
        if (height == 10)
        {
            laserFirst->StopWaiting();
            ResetObservers();
        }
        if (height == 11)
        {
            storage::Totals totalsCalc_1(*(laserFirst->getWalletDB()));
            resultsForCheck.totals_1= totalsCalc_1.GetTotals(Zero);

            storage::Totals totalsCalc_2(*(laserSecond->getWalletDB()));
            resultsForCheck.totals_2= totalsCalc_2.GetTotals(Zero);

            LOG_INFO() << "Test 3: open with coins";

            observer_1.onOpened =
                [&resultsForCheck] (const laser::ChannelIDPtr& chID)
                {
                    LOG_INFO() << "Test 3: first opened";
                    resultsForCheck.channel_1 = chID;
                    WALLET_CHECK(true);
                };
            observer_2.onOpened =
                [&resultsForCheck] (const laser::ChannelIDPtr& chID)
                {
                    LOG_INFO() << "Test 3: second opened";
                    resultsForCheck.channel_2 = chID;
                    WALLET_CHECK(true);
                };
            observer_1.onOpenFailed = observer_2.onOpenFailed =
                [] (const laser::ChannelIDPtr& chID)
                {
                    LOG_INFO() << "Test 3: open failed";
                    WALLET_CHECK(false);
                };

            laserFirst->WaitIncoming(100000000, 100000000, 101, 10);
            auto firstWalletID = laserFirst->getWaitingWalletID();
            laserSecond->OpenChannel(100000000, 100000000, 101, firstWalletID, 10);
        }
        if (height >= 13 && height <= 25 &&
            resultsForCheck.channel_1 &&
            resultsForCheck.channel_2 &&
            !resultsForCheck.test3Success)
        {
            resultsForCheck.test3Success = true;
            LOG_INFO() << "Test 3: check balance";

            storage::Totals totalsCalc_1(*(laserFirst->getWalletDB()));
            resultsForCheck.totals_1_a = totalsCalc_1.GetTotals(Zero);
            const auto& channelFirst = laserFirst->getChannel(resultsForCheck.channel_1);

            auto feeFirst = channelFirst->get_fee();

            storage::Totals totalsCalc_2(*(laserSecond->getWalletDB()));
            resultsForCheck.totals_2_a = totalsCalc_2.GetTotals(Zero);
            const auto& channelSecond = laserSecond->getChannel(resultsForCheck.channel_2);

            auto feeSecond = channelSecond->get_fee();

            Amount nFeeSecond = feeSecond / 2;
            Amount nFeeFirst = feeFirst - nFeeSecond;

            WALLET_CHECK(
                    resultsForCheck.totals_1.Unspent ==
                    resultsForCheck.totals_1_a.Unspent + channelFirst->get_amountMy() + nFeeFirst * 3);

            WALLET_CHECK(
                    resultsForCheck.totals_2.Unspent ==
                    resultsForCheck.totals_2_a.Unspent + channelSecond->get_amountMy() + nFeeSecond * 3);
        }
        if (height == 26)
        {
            ResetObservers();
            io::Reactor::get_Current().stop();
        }
    };

    TestNode node(newBlockFunc, 1);
    io::Timer::Ptr timer = io::Timer::create(*mainReactor);
    timer->start(1000, true, [&node]() {node.AddBlock(); });

    laserFirst->SetNetwork(CreateNetwork(*laserFirst));
    laserSecond->SetNetwork(CreateNetwork(*laserSecond));

    mainReactor->run();

    return WALLET_CHECK_RESULT;
}
