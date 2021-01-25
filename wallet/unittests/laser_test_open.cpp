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

#include "utility/logger.h"
#include "wallet/laser/mediator.h"
#include "node/node.h"
#include "core/unittest/mini_blockchain.h"
#include "utility/test_helpers.h"
#include "laser_test_utils.h"
#include "test_helpers.h"

WALLET_TEST_INIT
#include "wallet_test_environment.cpp"

using namespace beam;
using namespace beam::wallet;
using namespace std;

int main()
{
    int logLevel = LOG_LEVEL_DEBUG;
#if LOG_VERBOSE_ENABLED
    logLevel = LOG_LEVEL_VERBOSE;
#endif
    const auto path = boost::filesystem::system_complete("logs");
    auto logger = Logger::create(logLevel, logLevel, logLevel, "laser_test", path.string());

    Rules r;
    Rules::Scope scopeRules(r);
    InitTestRules(r);

    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    auto wdbFirst = createSqliteWalletDB(std::string(test_name(__FILE__)) + "-w1.db", false, true);
    wdbFirst->AllocateKidRange(100500);
    auto wdbSecond = createSqliteWalletDB(std::string(test_name(__FILE__)) + "-w2.db", false, true);

    // m_hRevisionMaxLifeTime, m_hLockTime, m_hPostLockReserve, m_Fee
    Lightning::Channel::Params params = {kRevisionMaxLifeTime, kLockTime, kPostLockReserve, kFee};
    auto laserFirst = std::make_unique<laser::Mediator>(wdbFirst, params);
    auto laserSecond = std::make_unique<laser::Mediator>(wdbSecond, params);

    LaserObserver observer_1, observer_2;
    laser::ChannelIDPtr channel_1, channel_2;

    observer_1.onOpened =
        [&channel_1] (const laser::ChannelIDPtr& chID)
        {
            LOG_INFO() << "Test laser OPEN: first opened";
            channel_1 = chID;
        };
    observer_2.onOpened =
        [&channel_2] (const laser::ChannelIDPtr& chID)
        {
            LOG_INFO() << "Test laser OPEN: second opened";
            channel_2 = chID;
        };
    observer_1.onOpenFailed = observer_2.onOpenFailed =
        [] (const laser::ChannelIDPtr& chID)
        {
            LOG_INFO() << "Test laser OPEN: open failed";
            WALLET_CHECK(false);
        };
    laserFirst->AddObserver(&observer_1);
    laserSecond->AddObserver(&observer_2);

    storage::Totals::AssetTotals totals_1, totals_1_a, totals_2, totals_2_a;

    auto newBlockFunc = [&] (Height height)
    {
        if (height > kMaxTestHeight)
        {
            LOG_ERROR() << "Test laser OPEN: time expired";
            WALLET_CHECK(false);
            io::Reactor::get_Current().stop();
        }

        if (height == kTestStartBlock)
        {
            storage::Totals totalsCalc_1(*(laserFirst->getWalletDB()));
            totals_1= totalsCalc_1.GetBeamTotals();

            storage::Totals totalsCalc_2(*(laserSecond->getWalletDB()));
            totals_2= totalsCalc_2.GetBeamTotals();

            laserFirst->WaitIncoming(100000000, 100000000, kFee);
            auto firstWalletID = laserFirst->getWaitingWalletID();
            laserSecond->OpenChannel(100000000, 100000000, kFee, firstWalletID, kOpenTxDh);
        }

        if (channel_1 && channel_2)
        {
            storage::Totals totalsCalc_1(*(laserFirst->getWalletDB()));
            totals_1_a = totalsCalc_1.GetBeamTotals();
            const auto& channelFirst = laserFirst->getChannel(channel_1);

            auto feeFirst = channelFirst->get_fee();

            storage::Totals totalsCalc_2(*(laserSecond->getWalletDB()));
            totals_2_a = totalsCalc_2.GetBeamTotals();
            const auto& channelSecond = laserSecond->getChannel(channel_2);

            auto feeSecond = channelSecond->get_fee();

            Amount nFeeSecond = feeSecond / 2;
            Amount nFeeFirst = feeFirst - nFeeSecond;

            AmountBig::Type val1 {totals_1_a.Unspent};
            val1 += AmountBig::Type(channelFirst->get_amountMy()+ nFeeFirst * 3);
            WALLET_CHECK(totals_1.Unspent == val1);

            AmountBig::Type val2 {totals_2_a.Unspent};
            val2 += AmountBig::Type(channelSecond->get_amountMy() + nFeeSecond * 3);
            WALLET_CHECK(totals_2.Unspent == val2);

            LOG_INFO() << "Test laser OPEN: finished";
            io::Reactor::get_Current().stop();
        }
    };

    Node node;
    NodeObserver observer([&]()
    {
        auto cursor = node.get_Processor().m_Cursor;
        newBlockFunc(cursor.m_Sid.m_Height);
    });
    auto binaryTreasury = MakeTreasury(wdbFirst, wdbSecond);
    InitNodeToTest(
        node, r, binaryTreasury, &observer, kDefaultTestNodePort,
        kNewBlockInterval, std::string(test_name(__FILE__)) + "-n.db");
    ConfigureNetwork(*laserFirst, *laserSecond);

    mainReactor->run();

    return WALLET_CHECK_RESULT;
}
