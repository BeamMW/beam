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
#include "wallet_test_node.h"

WALLET_TEST_INIT
#include "wallet_test_environment.cpp"

using namespace beam;
using namespace beam::wallet;
using namespace std;

int main()
{
    int logLevel = BEAM_LOG_LEVEL_DEBUG;
#if LOG_VERBOSE_ENABLED
    logLevel = BEAM_LOG_LEVEL_VERBOSE;
#endif
    const auto path = boost::filesystem::system_complete("logs");
    auto logger = Logger::create(logLevel, logLevel, logLevel, "laser_test", path.string());

    InitTestRules();

    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    auto testAll = [&](Amount fee) {
        auto wdbFirst = createSqliteWalletDB(std::string(test_name(__FILE__)) + "-w1.db", false, true);
        wdbFirst->AllocateKidRange(100500);
        auto wdbSecond = createSqliteWalletDB(std::string(test_name(__FILE__)) + "-w2.db", false, true);

        // m_hRevisionMaxLifeTime, m_hLockTime, m_hPostLockReserve, m_Fee
        Lightning::Channel::Params params = { kRevisionMaxLifeTime, kLockTime, kPostLockReserve, fee };
        auto laserFirst = std::make_unique<laser::Mediator>(wdbFirst, params);
        auto laserSecond = std::make_unique<laser::Mediator>(wdbSecond, params);

        LaserObserver observer_1, observer_2;
        laser::ChannelIDPtr channel_1, channel_2;
        bool laser1Closed = false, laser2Closed = false;
        bool closingStarted = false;
        Height closedAt = 0;

        observer_1.onOpened = [&channel_1](const laser::ChannelIDPtr& chID)
        {
            BEAM_LOG_INFO() << "Test laser DELETE: first opened";
            channel_1 = chID;
        };
        observer_2.onOpened = [&channel_2](const laser::ChannelIDPtr& chID)
        {
            BEAM_LOG_INFO() << "Test laser DELETE: second opened";
            channel_2 = chID;
        };
        observer_1.onOpenFailed = observer_2.onOpenFailed = [](const laser::ChannelIDPtr& chID)
        {
            BEAM_LOG_INFO() << "Test laser DELETE: open failed";
            WALLET_CHECK(false);
        };
        observer_1.onClosed = [&laser1Closed](const laser::ChannelIDPtr& chID)
        {
            BEAM_LOG_INFO() << "Test laser DELETE: first closed";
            laser1Closed = true;
        };
        observer_2.onClosed = [&laser2Closed](const laser::ChannelIDPtr& chID)
        {
            BEAM_LOG_INFO() << "Test laser DELETE: second closed";
            laser2Closed = true;
        };
        observer_1.onCloseFailed = observer_2.onCloseFailed = [](const laser::ChannelIDPtr& chID)
        {
            BEAM_LOG_INFO() << "Test laser DELETE: close failed";
            WALLET_CHECK(false);
        };
        laserFirst->AddObserver(&observer_1);
        laserSecond->AddObserver(&observer_2);

        auto newBlockFunc = [&](Height height)
        {
            if (height > kMaxTestHeight)
            {
                BEAM_LOG_ERROR() << "Test laser DELETE: time expired";
                WALLET_CHECK(false);
                io::Reactor::get_Current().stop();
            }

            if (height == kTestStartBlock)
            {
                laserFirst->WaitIncoming(100000000, 100000000, fee);
                auto firstWalletID = laserFirst->getWaitingWalletID();
                laserSecond->OpenChannel(100000000, 100000000, fee, firstWalletID, kOpenTxDh);
            }

            if (channel_1 && channel_2 && !closingStarted)
            {
                closingStarted = true;
                BEAM_LOG_INFO() << "Test laser DELETE: closing started";
                auto channel1Str = to_hex(channel_1->m_pData, channel_1->nBytes);
                WALLET_CHECK(laserFirst->GracefulClose(channel1Str));
            }

            if (laser1Closed && laser2Closed && !closedAt)
            {
                closedAt = height;
                BEAM_LOG_INFO() << "Test laser DELETE: before post lock reserve";
                auto channel1Str = to_hex(channel_1->m_pData, channel_1->nBytes);
                WALLET_CHECK(!laserFirst->Delete(channel1Str));
                auto channel2Str = to_hex(channel_2->m_pData, channel_2->nBytes);
                WALLET_CHECK(!laserSecond->Delete(channel2Str));
            }

            if (closedAt && height == closedAt + kPostLockReserve + 1)
            {
                BEAM_LOG_INFO() << "Test laser DELETE: after post lock reserve";
                auto channel1Str = to_hex(channel_1->m_pData, channel_1->nBytes);
                WALLET_CHECK(laserFirst->Delete(channel1Str));
                auto channel2Str = to_hex(channel_2->m_pData, channel_2->nBytes);
                WALLET_CHECK(laserSecond->Delete(channel2Str));
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
            node, binaryTreasury, &observer, kDefaultTestNodePort,
            kNewBlockInterval, std::string(test_name(__FILE__)) + "-n.db");
        ConfigureNetwork(*laserFirst, *laserSecond);

        mainReactor->run();
    };

    testAll(kFee);
    Rules::get().pForks[3].m_Height = 2;
    Rules::get().UpdateChecksum();
    testAll(kFeeAfter3dFork);

    return WALLET_CHECK_RESULT;
}
