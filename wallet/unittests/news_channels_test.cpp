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

#include <boost/filesystem.hpp>

// test helpers and mocks
#include "test_helpers.h"
WALLET_TEST_INIT
#include "mock_bbs_network.cpp"

// tested module
#include "wallet/client/extensions/news_channels/interface.h"
#include "wallet/client/extensions/news_channels/updates_provider.h"
#include "wallet/client/extensions/news_channels/wallet_updates_provider.h"
#include "wallet/client/extensions/news_channels/exchange_rate_provider.h"
#include "wallet/client/extensions/notifications/notification_center.h"

// dependencies
#include "wallet/client/extensions/broadcast_gateway/broadcast_router.h"
#include "wallet/client/extensions/broadcast_gateway/broadcast_msg_validator.h"
#include "wallet/client/extensions/broadcast_gateway/broadcast_msg_creator.h"

#include <tuple>

using namespace std;
using namespace beam;
using namespace beam::wallet;

namespace
{
    using PrivateKey = ECC::Scalar::Native;
    using PublicKey = PeerID;

    const string dbFileName = "wallet.db";

    /**
     *  Class to test correct notification of news channels observers
     */
    struct MockNewsObserver : public INewsObserver, public IExchangeRatesObserver
    {
        using OnVersion = function<void(const VersionInfo&, const ECC::uintBig&)>;
        using OnWalletVersion = function<void(const WalletImplVerInfo&, const ECC::uintBig&)>;
        using OnRate = function<void(const std::vector<ExchangeRate>&)>;

        MockNewsObserver(OnVersion onVers, OnWalletVersion onWalletVers, OnRate onRate)
            : m_onVers(onVers)
            , m_onWalletVers(onWalletVers)
            , m_onRate(onRate)
        {};

        virtual void onNewWalletVersion(const VersionInfo& v, const ECC::uintBig& s) override
        {
            m_onVers(v, s);
        }
        virtual void onNewWalletVersion(const WalletImplVerInfo& v, const ECC::uintBig& s) override
        {
            m_onWalletVers(v, s);
        }
        virtual void onExchangeRates(const std::vector<ExchangeRate>& r) override
        {
            m_onRate(r);
        }

        OnVersion m_onVers;
        OnWalletVersion m_onWalletVers;
        OnRate m_onRate;
    };

    /**
     *  Class to test notifications observers interface
     */
    struct MockNotificationsObserver : public INotificationsObserver
    {
        using OnNotification = function<void(ChangeAction action, const std::vector<Notification>&)>;

        MockNotificationsObserver(OnNotification callback)
            : m_onNotification(callback) {};
        
        virtual void onNotificationsChanged(ChangeAction action, const std::vector<Notification>& list) override
        {
            m_onNotification(action, list);
        }

        OnNotification m_onNotification;
    };

    void setBeforeFork3(IWalletDB::Ptr walletDB)
    {
        beam::Block::SystemState::ID id = { };
        id.m_Height = Rules::get().pForks[3].m_Height - 1;
        walletDB->setSystemStateID(id);
    }

    void setAfterFork3(IWalletDB::Ptr walletDB)
    {
        beam::Block::SystemState::ID id = { };
        id.m_Height = Rules::get().pForks[3].m_Height + 1;
        walletDB->setSystemStateID(id);
    }

    IWalletDB::Ptr createSqliteWalletDB()
    {
        if (boost::filesystem::exists(dbFileName))
        {
            boost::filesystem::remove(dbFileName);
        }

        ECC::NoLeak<ECC::uintBig> seed;
        seed.V = 10283UL;
        auto walletDB = WalletDB::init(dbFileName, string("pass123"), seed);
        setBeforeFork3(walletDB);
        return walletDB;
    }


    /**
     *  Derive key pair with specified @keyIndex
     */
    std::tuple<PublicKey, PrivateKey> deriveKeypair(IWalletDB::Ptr storage, uint64_t keyIndex)
    {
        PrivateKey sk;
        PublicKey pk;
        storage->get_MasterKdf()->DeriveKey(sk, ECC::Key::ID(keyIndex, Key::Type::Bbs));
        pk.FromSk(sk);
        return std::make_tuple(pk, sk);
    }

    /**
     *  Send broadcast message using protocol version 0.0.1
     *  Used to test compatibility between versions.
     */
    ByteBuffer createMessage(BroadcastContentType type, const BroadcastMsg& msg)
    {
        ByteBuffer content = wallet::toByteBuffer(msg);
        size_t packSize = MsgHeader::SIZE + content.size();
        assert(packSize <= proto::Bbs::s_MaxMsgSize);

        // Prepare Protocol header
        ByteBuffer packet(packSize);
        MsgHeader header(BroadcastRouter::m_ver_1[0],
                         BroadcastRouter::m_ver_1[1],
                         BroadcastRouter::m_ver_1[2]);
        
        switch (type)
        {
        case BroadcastContentType::SwapOffers:
            header.type = 0;
            break;
        case BroadcastContentType::SoftwareUpdates:
            header.type = 1;
            break;
        case BroadcastContentType::ExchangeRates:
            header.type = 2;
            break;
        case BroadcastContentType::WalletUpdates:
            header.type = 3;
            break;
        case BroadcastContentType::DexOffers:
            header.type = 4;
            break;
        }
        header.size = static_cast<uint32_t>(content.size());
        header.write(packet.data());

        std::copy(std::begin(content),
                std::end(content),
                std::begin(packet) + MsgHeader::SIZE);

        return packet;
    }

    void TestSoftwareVersion()
    {
        cout << endl << "Test Version operations" << endl;

        {
            Version v {123, 456, 789};
            WALLET_CHECK(to_string(v) == "123.456.789");
        }

        {
            WALLET_CHECK(Version(12,12,12) == Version(12,12,12));
            WALLET_CHECK(!(Version(12,12,12) != Version(12,12,12)));
            WALLET_CHECK(Version(12,13,12) != Version(12,12,12));
            WALLET_CHECK(!(Version(12,13,12) == Version(12,12,12)));

            WALLET_CHECK(Version(12,12,12) < Version(13,12,12));
            WALLET_CHECK(Version(12,12,12) < Version(12,13,12));
            WALLET_CHECK(Version(12,12,12) < Version(12,12,13));
            WALLET_CHECK(Version(12,12,12) < Version(13,13,13));
            WALLET_CHECK(!(Version(12,12,12) < Version(12,12,12)));
            
            WALLET_CHECK(Version(12,12,12) <= Version(12,12,12));
            WALLET_CHECK(Version(12,12,12) <= Version(13,13,13));
            WALLET_CHECK(!(Version(12,12,13) <= Version(12,12,12)));
        }

        {
            Version v;
            bool res = false;

            WALLET_CHECK_NO_THROW(res = v.from_string("12.345.6789"));
            WALLET_CHECK(res == true);
            WALLET_CHECK(v == Version(12,345,6789));

            WALLET_CHECK_NO_THROW(res = v.from_string("0.0.0"));
            WALLET_CHECK(res == true);
            WALLET_CHECK(v == Version());

            WALLET_CHECK_NO_THROW(res = v.from_string("12345.6789"));
            WALLET_CHECK(res == true);
            WALLET_CHECK(v == Version(12345,6789,0));

            WALLET_CHECK_NO_THROW(res = v.from_string("12,345.6789"));
            WALLET_CHECK(res == false);

            WALLET_CHECK_NO_THROW(res = v.from_string("12.345.6e89"));
            WALLET_CHECK(res == false);

            WALLET_CHECK_NO_THROW(res = v.from_string("12345.6789.12.52"));
            WALLET_CHECK(res == false);

            WALLET_CHECK_NO_THROW(res = v.from_string("f12345.6789.52"));
            WALLET_CHECK(res == false);
        }

        {
            WALLET_CHECK("desktop" == VersionInfo::to_string(VersionInfo::Application::DesktopWallet));
            WALLET_CHECK(VersionInfo::Application::DesktopWallet == VersionInfo::from_string("desktop"));
        }
    }

    void TestNewsChannelsObservers()
    {
        cout << endl << "Test news channels observers" << endl;

        auto storage = createSqliteWalletDB();
        MockBbsNetwork network;
        BroadcastRouter broadcastRouter(network, network);
        BroadcastMsgValidator validator;
        AppUpdateInfoProvider updatesProvider(broadcastRouter, validator);
        WalletUpdatesProvider walletUpdatesProvider(broadcastRouter, validator);
        ExchangeRateProvider rateProvider(broadcastRouter, validator, *storage);
        
        int execCountVers = 0;
        int execCountWalletVers = 0;
        int execCountRate = 0;

        const VersionInfo verInfo {
            VersionInfo::Application::DesktopWallet,
            Version {123,456,789}
            };
        const WalletImplVerInfo walletVerInfo {
            VersionInfo::Application::DesktopWallet,
            Version {123,456,789},
            "Test title",
            "bla-bla message",
            1234
            };

        auto timestamp = getTimestamp();
        std::vector<ExchangeRateF2> ratesF2 = {{ ExchangeRateF2::CurrencyF2::Beam, ExchangeRateF2::CurrencyF2::Usd, 147852369, timestamp}};

        timestamp++;
        std::vector<ExchangeRate> ratesF3 = {{ Currency::BEAM(), Currency::USD(), 147852369, timestamp}};

        const auto& [pk, sk] = deriveKeypair(storage, 321);
        BroadcastMsg msgV    = BroadcastMsgCreator::createSignedMessage(toByteBuffer(verInfo), sk);
        BroadcastMsg msgWV   = BroadcastMsgCreator::createSignedMessage(toByteBuffer(walletVerInfo), sk);
        BroadcastMsg msgRF2  = BroadcastMsgCreator::createSignedMessage(toByteBuffer(ratesF2), sk);
        BroadcastMsg msgRF3  = BroadcastMsgCreator::createSignedMessage(toByteBuffer(ratesF3), sk);

        MockNewsObserver testObserver(
            [&execCountVers, &verInfo] (const VersionInfo& v, const ECC::uintBig& id)
            {
                WALLET_CHECK(verInfo == v);
                ++execCountVers;
            },
            [&execCountWalletVers, &walletVerInfo] (const WalletImplVerInfo& v, const ECC::uintBig& id)
            {
                WALLET_CHECK(walletVerInfo == v);
                ++execCountWalletVers;
            },
            [&execCountRate, &ratesF3] (const std::vector<ExchangeRate>& r)
            {
                WALLET_CHECK(r.size() == 1 && ratesF3.size() == 1);
                WALLET_CHECK(r[0].m_from == ratesF3[0].m_from && r[0].m_to == ratesF3[0].m_to && r[0].m_rate == ratesF3[0].m_rate);
                ++execCountRate;
            });

        {
            // loading correct key with 2 additional just for filling
            PublicKey pk2, pk3;
            std::tie(pk2, std::ignore) = deriveKeypair(storage, 789);
            std::tie(pk3, std::ignore) = deriveKeypair(storage, 456);
            validator.setPublisherKeys({pk, pk2, pk3});
        }

        {
            cout << "Case: subscribed on valid message" << endl;
            updatesProvider.Subscribe(&testObserver);
            walletUpdatesProvider.Subscribe(&testObserver);
            rateProvider.Subscribe(&testObserver);

            broadcastRouter.sendMessage(BroadcastContentType::SoftwareUpdates, msgV);
            WALLET_CHECK(execCountVers == 1);

            broadcastRouter.sendMessage(BroadcastContentType::WalletUpdates, msgWV);
            WALLET_CHECK(execCountWalletVers == 1);

            // only one below should succeed
            broadcastRouter.sendMessage(BroadcastContentType::ExchangeRates, msgRF2);
            broadcastRouter.sendMessage(BroadcastContentType::ExchangeRates, msgRF3);
            WALLET_CHECK(execCountRate == 1);

            setAfterFork3(storage);
            // only one below should succeed
            broadcastRouter.sendMessage(BroadcastContentType::ExchangeRates, msgRF2);
            broadcastRouter.sendMessage(BroadcastContentType::ExchangeRates, msgRF3);
            setBeforeFork3(storage);
            WALLET_CHECK(execCountRate == 2);
        }

        {
            cout << "Case: unsubscribed on valid message" << endl;
            updatesProvider.Unsubscribe(&testObserver);
            walletUpdatesProvider.Unsubscribe(&testObserver);
            rateProvider.Unsubscribe(&testObserver);
            broadcastRouter.sendMessage(BroadcastContentType::SoftwareUpdates, msgV);
            broadcastRouter.sendMessage(BroadcastContentType::WalletUpdates, msgWV);
            broadcastRouter.sendMessage(BroadcastContentType::ExchangeRates, msgRF2);
            WALLET_CHECK(execCountVers == 1);
            WALLET_CHECK(execCountWalletVers == 1);
            WALLET_CHECK(execCountRate == 2);
        }
        {
            cout << "Case: subscribed back" << endl;
            updatesProvider.Subscribe(&testObserver);
            walletUpdatesProvider.Subscribe(&testObserver);
            rateProvider.Subscribe(&testObserver);

            broadcastRouter.sendMessage(BroadcastContentType::SoftwareUpdates, msgV);
            WALLET_CHECK(execCountVers == 2);

            broadcastRouter.sendMessage(BroadcastContentType::WalletUpdates, msgWV);
            WALLET_CHECK(execCountWalletVers == 2);

            broadcastRouter.sendMessage(BroadcastContentType::ExchangeRates, msgRF2);
            setAfterFork3(storage);
            broadcastRouter.sendMessage(BroadcastContentType::ExchangeRates, msgRF3);
            setBeforeFork3(storage);
            WALLET_CHECK(execCountRate == 2);   // the rate was the same so no need in the notification
        }

        {
            cout << "Case: subscribed on invalid message" << endl;
            // sign the same message with other key
            PrivateKey newSk;
            std::tie(std::ignore, newSk) = deriveKeypair(storage, 322);
            msgV = BroadcastMsgCreator::createSignedMessage(toByteBuffer(verInfo), newSk);
            broadcastRouter.sendMessage(BroadcastContentType::SoftwareUpdates, msgV);
            WALLET_CHECK(execCountVers == 2);
        }

        {
            cout << "Case: compatibility with the previous ver:0.0.1" << endl;
            timestamp++;
            ratesF2.front().m_updateTime = timestamp;

            timestamp++;
            ratesF3.front().m_updateTime = timestamp;

            msgV   = BroadcastMsgCreator::createSignedMessage(toByteBuffer(verInfo), sk);
            msgWV  = BroadcastMsgCreator::createSignedMessage(toByteBuffer(walletVerInfo), sk);
            msgRF2 = BroadcastMsgCreator::createSignedMessage(toByteBuffer(ratesF2), sk);
            msgRF3 = BroadcastMsgCreator::createSignedMessage(toByteBuffer(ratesF3), sk);

            broadcastRouter.sendRawMessage(BroadcastContentType::SoftwareUpdates, createMessage(BroadcastContentType::SoftwareUpdates, msgV));
            WALLET_CHECK(execCountVers == 3);

            broadcastRouter.sendRawMessage(BroadcastContentType::WalletUpdates, createMessage(BroadcastContentType::WalletUpdates, msgWV));
            WALLET_CHECK(execCountWalletVers == 2);     // BroadcastContentType::WalletUpdates are not implemented for protocol 0.0.1

            broadcastRouter.sendRawMessage(BroadcastContentType::ExchangeRates, createMessage(BroadcastContentType::ExchangeRates, msgRF2));
            WALLET_CHECK(execCountRate == 3);

            setAfterFork3(storage);
            broadcastRouter.sendRawMessage(BroadcastContentType::ExchangeRates, createMessage(BroadcastContentType::ExchangeRates, msgRF3));
            setBeforeFork3(storage);
            WALLET_CHECK(execCountRate == 4);
        }

        cout << "Test end" << endl;
    }

    void TestExchangeRateProvider()
    {
        cout << endl << "Test ExchangeRateProvider" << endl;

        MockBbsNetwork network;
        BroadcastRouter broadcastRouter(network, network);
        BroadcastMsgValidator validator;

        auto storage = createSqliteWalletDB();
        setAfterFork3(storage);

        ExchangeRateProvider rateProvider(broadcastRouter, validator, *storage);

        const auto& [pk, sk] = deriveKeypair(storage, 321);
        validator.setPublisherKeys({pk});

        // empty provider
        {
            cout << "Case: empty rates" << endl;
            WALLET_CHECK(rateProvider.getRates().empty());
        }

        const std::vector<ExchangeRate> rate = {{Currency::BEAM(), Currency::USD(), 147852369, getTimestamp()}};
        // add rate
        {
            cout << "Case: add rates" << endl;
            BroadcastMsg msg = BroadcastMsgCreator::createSignedMessage(toByteBuffer(rate), sk);
            broadcastRouter.sendMessage(BroadcastContentType::ExchangeRates, msg);

            auto testRates = rateProvider.getRates();
            WALLET_CHECK(testRates.size() == 1);
            WALLET_CHECK(testRates[0] == rate[0]);
        }
        // update rate
        {
            cout << "Case: not update if rates older" << endl;
            const std::vector<ExchangeRate> rateOlder = {{ Currency::BEAM(), Currency::USD(), 14785238554, getTimestamp()-100 }};
            BroadcastMsg msg = BroadcastMsgCreator::createSignedMessage(toByteBuffer(rateOlder), sk);
            broadcastRouter.sendMessage(BroadcastContentType::ExchangeRates, msg);

            auto testRates = rateProvider.getRates();
            WALLET_CHECK(testRates.size() == 1);
            WALLET_CHECK(testRates[0] == rate[0]);
        }
        const std::vector<ExchangeRate> rateNewer = {{ Currency::BEAM(), Currency::USD(), 14785238554, getTimestamp()+100 }};
        {
            cout << "Case: update rates" << endl;
            BroadcastMsg msg = BroadcastMsgCreator::createSignedMessage(toByteBuffer(rateNewer), sk);
            broadcastRouter.sendMessage(BroadcastContentType::ExchangeRates, msg);

            auto testRates = rateProvider.getRates();
            WALLET_CHECK(testRates.size() == 1);
            WALLET_CHECK(testRates[0] == rateNewer[0]);
        }
        // add more rate
        {
            cout << "Case: add more rates" << endl;
            const std::vector<ExchangeRate> rateAdded = {{ Currency::BEAM(), Currency::BTC(), 987, getTimestamp()+100 }};
            BroadcastMsg msg = BroadcastMsgCreator::createSignedMessage(toByteBuffer(rateAdded), sk);
            broadcastRouter.sendMessage(BroadcastContentType::ExchangeRates, msg);

            auto testRates = rateProvider.getRates();
            WALLET_CHECK(testRates.size() == 2);
            WALLET_CHECK(testRates[0] == rateNewer[0] || testRates[1] == rateNewer[0]);
        }
    }

    void TestNotificationCenter()
    {
        cout << endl << "Test NotificationCenter" << endl;

        auto storage = createSqliteWalletDB();
        std::map<Notification::Type,bool> activeTypes {
            { Notification::Type::SoftwareUpdateAvailable, true },
            { Notification::Type::AddressStatusChanged, true },
            { Notification::Type::WalletImplUpdateAvailable, true },
            { Notification::Type::BeamNews, true },
            { Notification::Type::TransactionFailed, true },
            { Notification::Type::TransactionCompleted, true }

        };
        NotificationCenter center(*storage, activeTypes, io::Reactor::get_Current().shared_from_this());

        {
            {
                cout << "Case: empty list" << endl;
                WALLET_CHECK(center.getNotifications().size() == 0);
            }

            const ECC::uintBig id({ 0,1,2,3,4,5,6,7,8,9,
                                    0,1,2,3,4,5,6,7,8,9,
                                    0,1,2,3,4,5,6,7,8,9,
                                    0,1});
            const ECC::uintBig id2({0,1,2,3,4,5,6,7,8,9,
                                    0,1,2,3,4,5,6,7,8,9,
                                    0,1,2,3,4,5,6,7,8,9,
                                    0,2});
            const VersionInfo info {
                VersionInfo::Application::DesktopWallet,
                Version(1,2,3)
                };
            
            const WalletImplVerInfo walletVerInfo {
                VersionInfo::Application::DesktopWallet,
                Version {123,456,789},
                "Test title",
                "bla-bla message",
                1234
                };

            size_t notificationsCounter = 0;
            
            {
                cout << "Case: create notification" << endl;
                size_t execCount = 0;
                MockNotificationsObserver observer(
                    [&execCount, &id]
                    (ChangeAction action, const std::vector<Notification>& list)
                    {
                        WALLET_CHECK(action == ChangeAction::Added);
                        WALLET_CHECK(list.size() == 1);
                        WALLET_CHECK(list[0].m_ID == id);
                        WALLET_CHECK(list[0].m_state == Notification::State::Unread);
                        ++execCount;
                    }
                );
                center.Subscribe(&observer);
                center.onNewWalletVersion(info, id);
                auto list = center.getNotifications();
                WALLET_CHECK(list.size() == ++notificationsCounter);
                WALLET_CHECK(list[0].m_ID == id);
                WALLET_CHECK(list[0].m_type == Notification::Type::SoftwareUpdateAvailable);
                WALLET_CHECK(list[0].m_state == Notification::State::Unread);
                WALLET_CHECK(list[0].m_createTime != 0);
                WALLET_CHECK(list[0].m_content == toByteBuffer(info));
                WALLET_CHECK(execCount == 1);
                center.Unsubscribe(&observer);
            }

            {
                cout << "Case: create wallet version notification" << endl;
                size_t execCount = 0;
                MockNotificationsObserver observer(
                    [&execCount, &id2, &walletVerInfo]
                    (ChangeAction action, const std::vector<Notification>& list)
                    {
                        WALLET_CHECK(action == ChangeAction::Added);
                        WALLET_CHECK(list.size() == 1);
                        WALLET_CHECK(list[0].m_ID == id2);
                        WALLET_CHECK(list[0].m_type == Notification::Type::WalletImplUpdateAvailable);
                        WALLET_CHECK(list[0].m_state == Notification::State::Unread);
                        WALLET_CHECK(list[0].m_createTime != 0);
                        WALLET_CHECK(list[0].m_content == toByteBuffer(walletVerInfo));
                        ++execCount;
                    }
                );
                center.Subscribe(&observer);
                center.onNewWalletVersion(walletVerInfo, id2);
                auto list = center.getNotifications();
                WALLET_CHECK(list.size() == ++notificationsCounter);
                WALLET_CHECK(execCount == 1);
                center.Unsubscribe(&observer);
            }

            // update: mark as read
            {
                cout << "Case: mark as read" << endl;
                size_t execCount = 0;
                MockNotificationsObserver observer(
                    [&execCount, &id, &info]
                    (ChangeAction action, const std::vector<Notification>& list)
                    {
                        WALLET_CHECK(action == ChangeAction::Updated);
                        WALLET_CHECK(list.size() == 1);
                        WALLET_CHECK(list[0].m_ID == id);
                        WALLET_CHECK(list[0].m_type == Notification::Type::SoftwareUpdateAvailable);
                        WALLET_CHECK(list[0].m_state == Notification::State::Read);
                        WALLET_CHECK(list[0].m_content == toByteBuffer(info));
                        ++execCount;
                    }
                );
                center.Subscribe(&observer);
                center.markNotificationAsRead(id);
                auto list = center.getNotifications();
                WALLET_CHECK(list.size() == notificationsCounter);
                WALLET_CHECK(execCount == 1);
                center.Unsubscribe(&observer);
            }

            // delete
            {
                cout << "Case: delete notification" << endl;
                size_t execCount = 0;
                MockNotificationsObserver observer(
                    [&execCount, &id]
                    (ChangeAction action, const std::vector<Notification>& list)
                    {
                        WALLET_CHECK(action == ChangeAction::Removed);
                        WALLET_CHECK(list.size() == 1);
                        WALLET_CHECK(list[0].m_ID == id);
                        ++execCount;
                    }
                );
                center.Subscribe(&observer);
                center.deleteNotification(id);
                // all notifications returned! even deleted.
                WALLET_CHECK(center.getNotifications().size() == notificationsCounter);
                WALLET_CHECK(execCount == 1);
                center.Unsubscribe(&observer);
            }

            // check on duplicate
            {
                cout << "Case: duplicate notification" << endl;
                MockNotificationsObserver observer(
                    []
                    (ChangeAction action, const std::vector<Notification>& list)
                    {
                        WALLET_CHECK(false);
                    }
                );
                center.Subscribe(&observer);
                center.onNewWalletVersion(info, id);
                auto list = center.getNotifications();
                WALLET_CHECK(list.size() == notificationsCounter);
                center.Unsubscribe(&observer);
            }
        }
    }

    void TestNotificationsOnOffSwitching()
    {
        cout << endl << "Test notifications on/off switching" << endl;

        auto storage = createSqliteWalletDB();
        std::map<Notification::Type,bool> activeTypes {
            { Notification::Type::SoftwareUpdateAvailable, false },
            { Notification::Type::AddressStatusChanged, false },
            { Notification::Type::WalletImplUpdateAvailable, false },
            { Notification::Type::BeamNews, false },
            { Notification::Type::TransactionFailed, false },
            { Notification::Type::TransactionCompleted, false }
        };
        NotificationCenter center(*storage, activeTypes, io::Reactor::get_Current().shared_from_this());

        WALLET_CHECK(center.getNotifications().size() == 0);

        VersionInfo info { VersionInfo::Application::DesktopWallet, Version(1,2,3) };
        const ECC::uintBig id( {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
                                1,1,1,1,1,1,1,1,1,1,1,1});
        const ECC::uintBig id2({2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
                                2,2,2,2,2,2,2,2,2,2,2,2});
        const ECC::uintBig id3({3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
                                3,3,3,3,3,3,3,3,3,3,3,3});

        // notifications is off on start
        {
            cout << "Case: notifications is off on start" << endl;
            MockNotificationsObserver observer(
                []
                (ChangeAction action, const std::vector<Notification>& list)
                {
                    WALLET_CHECK(false);
                }
            );
            center.Subscribe(&observer);
            center.onNewWalletVersion(info, id);
            auto list = center.getNotifications();
            WALLET_CHECK(list.size() == 0);
            center.Unsubscribe(&observer);
        }
        // notifications switched on
        {
            cout << "Case: notifications switched on" << endl;
            size_t execCount = 0;
            MockNotificationsObserver observer(
                [&execCount, &id2]
                (ChangeAction action, const std::vector<Notification>& list)
                {
                    WALLET_CHECK(action == ChangeAction::Added);
                    WALLET_CHECK(list.size() == 1);
                    WALLET_CHECK(list[0].m_ID == id2);
                    ++execCount;
                }
            );
            center.switchOnOffNotifications(Notification::Type::SoftwareUpdateAvailable, true);
            center.Subscribe(&observer);
            auto list = center.getNotifications();
            WALLET_CHECK(list.size() == 1);
            center.onNewWalletVersion(info, id2);
            list = center.getNotifications();
            WALLET_CHECK(list.size() == 2);
            center.Unsubscribe(&observer);
            WALLET_CHECK(execCount == 1);
        }
        // notification switched off
        {
            cout << "Case: notifications switched on" << endl;
            MockNotificationsObserver observer(
                []
                (ChangeAction action, const std::vector<Notification>& list)
                {
                    WALLET_CHECK(false);
                }
            );
            center.switchOnOffNotifications(Notification::Type::SoftwareUpdateAvailable, false);
            center.Subscribe(&observer);
            center.onNewWalletVersion(info, id3);
            auto list = center.getNotifications();
            WALLET_CHECK(list.size() == 0);
            center.Unsubscribe(&observer);
        }
    }

    void TestNotificationsOnExpiredAddress()
    {
        cout << endl << "Test notifications on expired address" << endl;

        // notification on appearing expired address in storage
        {
            auto storage = createSqliteWalletDB();
            std::map<Notification::Type,bool> activeTypes {
                { Notification::Type::SoftwareUpdateAvailable, false },
                { Notification::Type::AddressStatusChanged, true },
                { Notification::Type::WalletImplUpdateAvailable, false },
                { Notification::Type::BeamNews, false },
                { Notification::Type::TransactionFailed, false },
                { Notification::Type::TransactionCompleted, false }
            };
            WalletAddress addr;
            storage->createAddress(addr);
            addr.m_createTime = 123;
            addr.m_duration = 456;
            storage->saveAddress(addr);
            NotificationCenter center(*storage, activeTypes, io::Reactor::get_Current().shared_from_this());

            size_t exeCount = 0;
            MockNotificationsObserver observer(
                [&exeCount, &addr]
                (ChangeAction action, const std::vector<Notification>& list)
                {
                    WALLET_CHECK(action == ChangeAction::Added);
                    WALLET_CHECK(list.size() == 1);
                    WALLET_CHECK(list[0].m_ID == addr.m_walletID.m_Pk);
                    WALLET_CHECK(list[0].m_type == Notification::Type::AddressStatusChanged);
                    WALLET_CHECK(list[0].m_state == Notification::State::Unread);
                    ++exeCount;
                }
            );

            center.Subscribe(&observer);
            auto list = center.getNotifications();
            WALLET_CHECK(list.size() == 1);
            WALLET_CHECK(list[0].m_ID == addr.m_walletID.m_Pk);
            WALLET_CHECK(list[0].m_type == Notification::Type::AddressStatusChanged);
            WALLET_CHECK(list[0].m_state == Notification::State::Unread);
            center.Unsubscribe(&observer);
            WALLET_CHECK(exeCount == 1);
        }

        // notification on address update if expired
        {
            auto storage = createSqliteWalletDB();
            std::map<Notification::Type,bool> activeTypes {
                { Notification::Type::SoftwareUpdateAvailable, false },
                { Notification::Type::AddressStatusChanged, true },
                { Notification::Type::WalletImplUpdateAvailable, false },
                { Notification::Type::BeamNews, false },
                { Notification::Type::TransactionFailed, false },
                { Notification::Type::TransactionCompleted, false }
            };
            const ECC::uintBig id2({
                0,1,2,3,4,5,6,7,8,9,
                0,1,2,3,4,5,6,7,8,9,
                0,1,2,3,4,5,6,7,8,9,
                0,2});
            WalletID wid2;
            wid2.m_Channel = 123u;
            wid2.m_Pk = id2;
            WalletAddress addr2;
            addr2.m_walletID = wid2;
            addr2.m_label = "expiredAddress";
            addr2.m_category = "abc";
            addr2.m_createTime = 123;
            addr2.m_duration = 456;
            addr2.m_OwnID = 2;
            addr2.m_Identity = id2;
            NotificationCenter center(*storage, activeTypes, io::Reactor::get_Current().shared_from_this());

            size_t exeCount = 0;
            MockNotificationsObserver observer(
                [&exeCount, &id2]
                (ChangeAction action, const std::vector<Notification>& list)
                {
                    WALLET_CHECK(action == ChangeAction::Added);
                    WALLET_CHECK(list.size() == 1);
                    WALLET_CHECK(list[0].m_ID == id2);
                    WALLET_CHECK(list[0].m_type == Notification::Type::AddressStatusChanged);
                    WALLET_CHECK(list[0].m_state == Notification::State::Unread);
                    ++exeCount;
                }
            );
            center.Subscribe(&observer);
            center.onAddressChanged(ChangeAction::Updated, { addr2 });
            auto list = center.getNotifications();
            WALLET_CHECK(list.size() == 1);
            center.Unsubscribe(&observer);
            WALLET_CHECK(exeCount == 1);
        }
    }

} // namespace

int main()
{
    cout << "News channels tests:" << endl;

    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    TestSoftwareVersion();
    TestNewsChannelsObservers();
    TestExchangeRateProvider();

    TestNotificationCenter();
    TestNotificationsOnOffSwitching();
    TestNotificationsOnExpiredAddress();

    boost::filesystem::remove(dbFileName);

    assert(g_failureCount == 0);
    return WALLET_CHECK_RESULT;
}

