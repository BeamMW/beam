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

const Height kMaxTestHeight = 254;
const Height kChannelLockTime = 42;
const Amount kTransferFirst = 10000;
const Amount kTransferSecond = 2000;

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
    observer_1.onOpened = observer_2.onOpened = [] (const laser::ChannelIDPtr& chID) {};
    observer_1.onOpenFailed = observer_2.onOpenFailed = [] (const laser::ChannelIDPtr& chID) {};
    observer_1.onClosed = observer_2.onClosed = [] (const laser::ChannelIDPtr& chID) {};
    observer_1.onCloseFailed = observer_2.onCloseFailed = [] (const laser::ChannelIDPtr& chID) {};
    observer_1.onUpdateFinished = observer_2.onUpdateFinished = [] (const laser::ChannelIDPtr& chID) {};
    observer_1.onTransferFailed = observer_2.onTransferFailed = [] (const laser::ChannelIDPtr& chID) {};
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
    Rules::get().MaxRollback = 5;
	Rules::get().UpdateChecksum();

    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    auto wdbFirst = createSqliteWalletDB("first.db", false, false);
    auto wdbSecond = createSqliteWalletDB("second.db", false, false);

    const AmountList amounts = {100000000, 100000000, 100000000, 100000000};
    for (auto amount : amounts)
    {
        Coin coin = CreateAvailCoin(amount, 3);
        wdbFirst->storeCoin(coin);
    }
    for (auto amount : amounts)
    {
        Coin coin = CreateAvailCoin(amount, 7);
        wdbSecond->storeCoin(coin);
    }

    auto laserFirst = std::make_unique<laser::Mediator>(wdbFirst);
    auto laserSecond = std::make_unique<laser::Mediator>(wdbSecond);

    laserFirst->AddObserver(&observer_1);
    laserSecond->AddObserver(&observer_2);

    struct CheckResults {
        storage::Totals::AssetTotals totals_1, totals_1_a, totals_2, totals_2_a;
        laser::ChannelIDPtr channel_1 = nullptr, channel_2 = nullptr;
        struct Test1 {
            Height height = kMaxTestHeight;
            bool firstFailed = false;
            bool secondFailed = false;
        } test1;
        struct Test2 {
            Height height = kMaxTestHeight;
            bool secondFailed = false;
        } test2;
        struct Test3 {
            Height height = kMaxTestHeight;
            Height unlockHeight = kMaxTestHeight;
        } test3;
        struct Test4 {
            Height height = kMaxTestHeight;
        } test4;
        struct Test5 {
            Height height = kMaxTestHeight;
            bool firstCompleted = false;
            bool secondCompleted = false;
        } test5;
        struct Test6 {
            Height height = kMaxTestHeight;
            bool firstCompleted = false;
            bool secondCompleted = false;
            bool testInProgress = false;
            int count = 0;
        } test6;
        struct Test7 {
            Height height = kMaxTestHeight;
            bool transferStarted = false;
            bool transferFinished = false;
        } test7;
        struct Test8 {
            Height height = kMaxTestHeight;
            bool firstClosed = false;
            bool secondClosed = false;
        } test8;
        struct Test9 {
            Height height = kMaxTestHeight;
        } test9;
        struct Test10 {
            Height height = kMaxTestHeight;
        } test10;
        struct Test11 {
            Height height = kMaxTestHeight;
        } test11;
        struct Test12 {
            Height height = kMaxTestHeight;
            bool firstClosed = false;
            bool secondClosed = false;
        } test12;
        struct Test13 {
            Height height = kMaxTestHeight;
        } test13;
        struct Test14 {
            Height height = kMaxTestHeight;
            bool firstClosed = false;
            bool secondClosed = false;
        } test14;
    } resultsForCheck;

    auto newBlockFunc = [
        &laserFirst,
        &laserSecond,
        &resultsForCheck,
        wdbFirst,
        wdbSecond
    ] (Height height)
    {
        if (height == kMaxTestHeight)
        {
            ResetObservers();
            io::Reactor::get_Current().stop();
            return;
        }

        if (height == 2)
        {
            LOG_INFO() << "Test 1: both sides open without coins";

            observer_1.onOpened = observer_2.onOpened =
                [] (const laser::ChannelIDPtr& chID)
                {
                    WALLET_CHECK(false);
                };
            observer_1.onOpenFailed =
                [&resultsForCheck] (const laser::ChannelIDPtr& chID)
                {
                    resultsForCheck.test1.firstFailed = true;
                    WALLET_CHECK(true);
                };
           observer_2.onOpenFailed =
                [&resultsForCheck] (const laser::ChannelIDPtr& chID)
                {
                    resultsForCheck.test1.secondFailed = true;
                    WALLET_CHECK(true);
                };

            laserFirst->WaitIncoming(100000000, 100000000, 100, 10);
            auto firstWalletID = laserFirst->getWaitingWalletID();
            laserSecond->OpenChannel(100000000, 100000000, 100, firstWalletID, 10);
        }
        if (height >= 3 &&
            resultsForCheck.test1.firstFailed &&
            resultsForCheck.test1.secondFailed &&
            resultsForCheck.test1.height == kMaxTestHeight)
        {
            resultsForCheck.test1.height = height;
            laserFirst->StopWaiting();
            ResetObservers();  
        }
        if (height == resultsForCheck.test1.height + 1)
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
                [&resultsForCheck] (const laser::ChannelIDPtr& chID)
                {
                    LOG_INFO() << "Test 2: second side open failed";
                    resultsForCheck.test2.secondFailed = true;
                    WALLET_CHECK(true);
                };

            laserFirst->WaitIncoming(100000000, 100000000, 100, 10);
            auto firstWalletID = laserFirst->getWaitingWalletID();
            laserSecond->OpenChannel(100000000, 100000000, 100, firstWalletID, 10);
        }
        if (height > resultsForCheck.test1.height + 1 &&
            height > 7 &&
            resultsForCheck.test2.secondFailed &&
            resultsForCheck.test2.height == kMaxTestHeight)
        {
            resultsForCheck.test2.height = height;
            laserFirst->StopWaiting();
            ResetObservers();
        }
        if (height == resultsForCheck.test2.height + 1)
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

            laserFirst->WaitIncoming(100000000, 100000000, 101, kChannelLockTime);
            auto firstWalletID = laserFirst->getWaitingWalletID();
            laserSecond->OpenChannel(100000000, 100000000, 101, firstWalletID, kChannelLockTime);
        }
        if (height > resultsForCheck.test2.height + 1 &&
            resultsForCheck.channel_1 &&
            resultsForCheck.channel_2 &&
            resultsForCheck.test3.height == kMaxTestHeight)
        {
            resultsForCheck.test3.height = height;

            const auto& channelFirst = laserFirst->getChannel(resultsForCheck.channel_1);
            resultsForCheck.test3.unlockHeight = channelFirst->get_LockHeight();
            ResetObservers();
        }
        if (height == resultsForCheck.test3.height + 1)
        {
            LOG_INFO() << "Test 4: check balance";

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
            
            resultsForCheck.test4.height = height;
        }
        if (height == resultsForCheck.test4.height + 1)
        {
            if (height >= resultsForCheck.test3.unlockHeight)
            {
                LOG_INFO() << "Test 5: skip";
                resultsForCheck.test5.firstCompleted = true;
                resultsForCheck.test5.secondCompleted = true;
                WALLET_CHECK(false);
                return;
            }

            ResetObservers();

            auto channel2Str = to_hex(resultsForCheck.channel_2->m_pData,
                                      resultsForCheck.channel_2->nBytes);

            LOG_INFO() << "Test 5: first send to second, amount more then locked in channel";
            WALLET_CHECK(!laserFirst->Transfer(1000000000, channel2Str));

            LOG_INFO() << "Test 5: first send to second";
            observer_1.onUpdateFinished =
                [&resultsForCheck, &laserFirst] (const laser::ChannelIDPtr& chID)
                {
                    LOG_INFO() << "Test 5: first updated";
                    if (resultsForCheck.test5.firstCompleted) return;
                    resultsForCheck.test5.firstCompleted = true;
                    const auto& channelFirst = laserFirst->getChannel(resultsForCheck.channel_1);
                    WALLET_CHECK(channelFirst->get_amountCurrentMy() == channelFirst->get_amountMy() - kTransferFirst);
                };
            observer_1.onTransferFailed =
                [&resultsForCheck] (const laser::ChannelIDPtr& chID)
                {
                    LOG_INFO() << "Test 5: first transfer failed";
                    resultsForCheck.test5.firstCompleted = true;
                };
            observer_2.onUpdateFinished =
                [&resultsForCheck, &laserSecond] (const laser::ChannelIDPtr& chID)
                {
                    LOG_INFO() << "Test 5: second updated";
                    if (resultsForCheck.test5.secondCompleted) return;
                    resultsForCheck.test5.secondCompleted = true;
                    const auto& channelSecond = laserSecond->getChannel(resultsForCheck.channel_2);
                    WALLET_CHECK(channelSecond->get_amountCurrentMy() == channelSecond->get_amountMy() + kTransferFirst);
                };
            observer_2.onTransferFailed =
                [&resultsForCheck] (const laser::ChannelIDPtr& chID)
                {
                LOG_INFO() << "Test 5: second transfer failed";
                    resultsForCheck.test5.secondCompleted = true;
                };
            WALLET_CHECK(laserFirst->Transfer(kTransferFirst, channel2Str));
        }
        if (height > resultsForCheck.test4.height + 1 &&
            resultsForCheck.test5.firstCompleted &&
            resultsForCheck.test5.secondCompleted &&
            resultsForCheck.test5.height == kMaxTestHeight)
        {
            resultsForCheck.test5.height = height;
            ResetObservers();
        }
        if (height > resultsForCheck.test5.height &&
            height < resultsForCheck.test3.unlockHeight &&
            !resultsForCheck.test6.testInProgress)
        {
            LOG_INFO() << "Test 6: second send to first";

            resultsForCheck.test6.testInProgress = true;
            auto channel2Str = to_hex(resultsForCheck.channel_2->m_pData,
                                      resultsForCheck.channel_2->nBytes);
            ++resultsForCheck.test6.count;

            observer_1.onUpdateFinished =
                [&resultsForCheck, &laserFirst] (const laser::ChannelIDPtr& chID)
                {
                    LOG_INFO() << "Test 6: first updated";
                    if (resultsForCheck.test6.firstCompleted) return;
                    resultsForCheck.test6.firstCompleted = true;
                    const auto& channelFirst = laserFirst->getChannel(resultsForCheck.channel_1);
                    WALLET_CHECK(channelFirst->get_amountCurrentMy() == 
                                 channelFirst->get_amountMy() - kTransferFirst + resultsForCheck.test6.count * kTransferSecond);
                };
            observer_1.onTransferFailed =
                [&resultsForCheck] (const laser::ChannelIDPtr& chID)
                {
                    LOG_INFO() << "Test 6: first transfer failed";
                    resultsForCheck.test6.firstCompleted = true;
                };
            observer_2.onUpdateFinished =
                [&resultsForCheck, &laserSecond] (const laser::ChannelIDPtr& chID)
                {
                    LOG_INFO() << "Test 6: second updated";
                    if (resultsForCheck.test6.secondCompleted) return;
                    resultsForCheck.test6.secondCompleted = true;
                    const auto& channelSecond = laserSecond->getChannel(resultsForCheck.channel_2);
                    WALLET_CHECK(channelSecond->get_amountCurrentMy() ==
                                 channelSecond->get_amountMy() + kTransferFirst - resultsForCheck.test6.count * kTransferSecond);
                };
            observer_2.onTransferFailed =
                [&resultsForCheck] (const laser::ChannelIDPtr& chID)
                {
                LOG_INFO() << "Test 6: second transfer failed";
                    resultsForCheck.test6.secondCompleted = true;
                };
            WALLET_CHECK(laserSecond->Transfer(kTransferSecond, channel2Str));
        }
        if (height > resultsForCheck.test5.height + 1 &&
            resultsForCheck.test6.firstCompleted &&
            resultsForCheck.test6.secondCompleted)
        {
            resultsForCheck.test6.firstCompleted = false;
            resultsForCheck.test6.secondCompleted = false;
            resultsForCheck.test6.testInProgress = false;
            resultsForCheck.test6.height = height;
            ResetObservers();

            LOG_INFO() << "Test 6: finished " << resultsForCheck.test6.count << " times";
        }
        if (height >= resultsForCheck.test3.unlockHeight &&
            !resultsForCheck.test6.testInProgress &&
            !resultsForCheck.test7.transferStarted)  
        {
            LOG_INFO() << "Test 7: first send to second, after lock height reached";

            resultsForCheck.test7.transferStarted = true;

            auto channel2Str = to_hex(resultsForCheck.channel_2->m_pData,
                                      resultsForCheck.channel_2->nBytes);
            observer_1.onTransferFailed =
                [&resultsForCheck] (const laser::ChannelIDPtr& chID)
                {
                    LOG_INFO() << "Test 7: transfer failed";
                    resultsForCheck.test7.transferFinished = true;
                    WALLET_CHECK(resultsForCheck.test7.transferFinished);
                };
            observer_1.onUpdateFinished =
                [&resultsForCheck] (const laser::ChannelIDPtr& chID)
                {
                    LOG_INFO() << "Test 7: first updated";
                    if (!resultsForCheck.test7.transferFinished)
                    {
                        resultsForCheck.test7.transferFinished = true;
                        WALLET_CHECK(false);
                    }
                };

            WALLET_CHECK(laserFirst->Transfer(10000, channel2Str));
        }
        if (height > resultsForCheck.test3.unlockHeight &&
            height > resultsForCheck.test6.height + 1 &&
            resultsForCheck.test7.transferFinished &&
            resultsForCheck.test7.height == kMaxTestHeight)
        {
            resultsForCheck.test7.height = height;
            ResetObservers();
        }
        if (height == resultsForCheck.test7.height + 1)
        {
            LOG_INFO() << "Test 8: close by first, after lock height reached";

            auto channel2Str = to_hex(resultsForCheck.channel_2->m_pData,
                                      resultsForCheck.channel_2->nBytes);

            observer_1.onClosed =
                [&resultsForCheck] (const laser::ChannelIDPtr& chID)
                {
                    LOG_INFO() << "Test 8: first closed";
                    resultsForCheck.test8.firstClosed = true;
                    WALLET_CHECK(resultsForCheck.test8.firstClosed);
                };
            observer_1.onCloseFailed =
                [] (const laser::ChannelIDPtr& chID)
                {
                    LOG_INFO() << "Test 8: first -> close failed";
                    WALLET_CHECK(false);
                };
            observer_2.onClosed =
                [&resultsForCheck] (const laser::ChannelIDPtr& chID)
                {
                    LOG_INFO() << "Test 8: second closed";
                    resultsForCheck.test8.secondClosed = true;
                    WALLET_CHECK(resultsForCheck.test8.secondClosed);
                };
            observer_2.onCloseFailed =
                [] (const laser::ChannelIDPtr& chID)
                {
                    LOG_INFO() << "Test 8: second -> close failed";
                    WALLET_CHECK(false);
                };
            WALLET_CHECK(laserFirst->Close(channel2Str));
        }
        if (height > resultsForCheck.test7.height + 1 &&
            resultsForCheck.test8.firstClosed &&
            resultsForCheck.test8.secondClosed &&
            resultsForCheck.test8.height == kMaxTestHeight)
        {
            resultsForCheck.test8.height = height;
            ResetObservers();
        }
        if (height == resultsForCheck.test8.height + 1)
        {
            LOG_INFO() << "Test 9: open with coins";

            resultsForCheck.channel_1 = nullptr;
            resultsForCheck.channel_2 = nullptr;

            observer_1.onOpened =
                [&resultsForCheck] (const laser::ChannelIDPtr& chID)
                {
                    LOG_INFO() << "Test 9: first opened";
                    resultsForCheck.channel_1 = chID;
                    WALLET_CHECK(true);
                };
            observer_2.onOpened =
                [&resultsForCheck] (const laser::ChannelIDPtr& chID)
                {
                    LOG_INFO() << "Test 9: second opened";
                    resultsForCheck.channel_2 = chID;
                    WALLET_CHECK(true);
                };
            observer_1.onOpenFailed = observer_2.onOpenFailed =
                [] (const laser::ChannelIDPtr& chID)
                {
                    LOG_INFO() << "Test 9: open failed";
                    WALLET_CHECK(false);
                };

            laserFirst->WaitIncoming(100000000, 100000000, 101, 10);
            auto firstWalletID = laserFirst->getWaitingWalletID();
            laserSecond->OpenChannel(100000000, 100000000, 101, firstWalletID, 10);
        }
        if (height > resultsForCheck.test8.height + 1 &&
            resultsForCheck.channel_1 &&
            resultsForCheck.channel_2 &&
            resultsForCheck.test9.height == kMaxTestHeight)
        {
            resultsForCheck.test9.height = height;
            ResetObservers();
        }
        if (height == resultsForCheck.test9.height + 1)
        {
            LOG_INFO() << "Test 10: recreate first";

            laserFirst.reset(new laser::Mediator(wdbFirst));
            laserFirst->AddObserver(&observer_1);
            laserFirst->SetNetwork(CreateNetwork(*laserFirst));

            auto channel1Str = to_hex(resultsForCheck.channel_1->m_pData,
                                      resultsForCheck.channel_1->nBytes);
            WALLET_CHECK(laserFirst->Serve(channel1Str));

            resultsForCheck.test10.height = height;
        }
        if (height == resultsForCheck.test10.height + 1)
        {
            LOG_INFO() << "Test 11: recreate second";

            laserSecond.reset(new laser::Mediator(wdbSecond));
            laserSecond->AddObserver(&observer_2);
            laserSecond->SetNetwork(CreateNetwork(*laserSecond));

            auto channel2Str = to_hex(resultsForCheck.channel_2->m_pData,
                                      resultsForCheck.channel_2->nBytes);
            WALLET_CHECK(laserSecond->Serve(channel2Str));

            resultsForCheck.test11.height = height;
        }
        if (height == resultsForCheck.test11.height + 1)
        {
            LOG_INFO() << "Test 12: close by first, before lock height reached";

            auto channel2Str = to_hex(resultsForCheck.channel_2->m_pData,
                               resultsForCheck.channel_2->nBytes);

            observer_1.onClosed =
                [&resultsForCheck] (const laser::ChannelIDPtr& chID)
                {
                    LOG_INFO() << "Test 12: first closed";
                    resultsForCheck.test12.firstClosed = true;
                    WALLET_CHECK(resultsForCheck.test12.firstClosed);
                };
            observer_1.onCloseFailed =
                [] (const laser::ChannelIDPtr& chID)
                {
                    LOG_INFO() << "Test 12: first -> close failed";
                    WALLET_CHECK(false);
                };
            observer_2.onClosed =
                [&resultsForCheck] (const laser::ChannelIDPtr& chID)
                {
                    LOG_INFO() << "Test 12: second closed";
                    resultsForCheck.test12.secondClosed = true;
                    WALLET_CHECK(resultsForCheck.test12.secondClosed);
                };
            observer_2.onCloseFailed =
                [] (const laser::ChannelIDPtr& chID)
                {
                    LOG_INFO() << "Test 12: second -> close failed";
                    WALLET_CHECK(false);
                };
            WALLET_CHECK(laserSecond->Close(channel2Str));
        }
        if (height > resultsForCheck.test11.height + 1 &&
            resultsForCheck.test12.firstClosed &&
            resultsForCheck.test12.secondClosed &&
            resultsForCheck.test12.height == kMaxTestHeight)
        {
            resultsForCheck.test12.height = height;
            ResetObservers();
        }
        if (height == resultsForCheck.test12.height + 1)
        {
            LOG_INFO() << "Test 13: open with coins";

            resultsForCheck.channel_1 = nullptr;
            resultsForCheck.channel_2 = nullptr;

            observer_1.onOpened =
                [&resultsForCheck] (const laser::ChannelIDPtr& chID)
                {
                    LOG_INFO() << "Test 13: first opened";
                    resultsForCheck.channel_1 = chID;
                    WALLET_CHECK(true);
                };
            observer_2.onOpened =
                [&resultsForCheck] (const laser::ChannelIDPtr& chID)
                {
                    LOG_INFO() << "Test 13: second opened";
                    resultsForCheck.channel_2 = chID;
                    WALLET_CHECK(true);
                };
            observer_1.onOpenFailed = observer_2.onOpenFailed =
                [] (const laser::ChannelIDPtr& chID)
                {
                    LOG_INFO() << "Test 13: open failed";
                    WALLET_CHECK(false);
                };

            laserFirst->WaitIncoming(100000000, 100000000, 101, 10);
            auto firstWalletID = laserFirst->getWaitingWalletID();
            laserSecond->OpenChannel(100000000, 100000000, 101, firstWalletID, 10);
        }
        if (height > resultsForCheck.test12.height + 1 &&
            resultsForCheck.channel_1 &&
            resultsForCheck.channel_2 &&
            resultsForCheck.test13.height == kMaxTestHeight)
        {
            resultsForCheck.test13.height = height;
            ResetObservers();
        }
        if (height == resultsForCheck.test13.height + 1)
        {
            LOG_INFO() << "Test 14: close graceful by first, before lock height reached";

            auto channel2Str = to_hex(resultsForCheck.channel_2->m_pData,
                                      resultsForCheck.channel_2->nBytes);

            observer_1.onClosed =
                [&resultsForCheck] (const laser::ChannelIDPtr& chID)
                {
                    LOG_INFO() << "Test 14: first closed";
                    resultsForCheck.test14.firstClosed = true;
                    WALLET_CHECK(resultsForCheck.test14.firstClosed);
                };
            observer_1.onCloseFailed =
                [] (const laser::ChannelIDPtr& chID)
                {
                    LOG_INFO() << "Test 14: first -> close failed";
                    WALLET_CHECK(false);
                };
            observer_2.onClosed =
                [&resultsForCheck] (const laser::ChannelIDPtr& chID)
                {
                    LOG_INFO() << "Test 14: second closed";
                    resultsForCheck.test14.secondClosed = true;
                    WALLET_CHECK(resultsForCheck.test14.secondClosed);
                };
            observer_2.onCloseFailed =
                [] (const laser::ChannelIDPtr& chID)
                {
                    LOG_INFO() << "Test 14: second -> close failed";
                    WALLET_CHECK(false);
                };
            WALLET_CHECK(laserFirst->GracefulClose(channel2Str));
        }
        if (height > resultsForCheck.test13.height + 1 &&
            resultsForCheck.test14.firstClosed &&
            resultsForCheck.test14.secondClosed &&
            resultsForCheck.test14.height == kMaxTestHeight)
        {
            resultsForCheck.test14.height = height;
            ResetObservers();

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
