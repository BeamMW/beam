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

#include "test_helpers.h"
#include "wallet/core/common.h"
#include "wallet/core/simple_transaction.h"
#include "wallet/core/wallet_network.h"
#include "keykeeper/local_private_key_keeper.h"
// for wallet_test_environment.cpp
#include "core/unittest/mini_blockchain.h"
#include "core/radixtree.h"
#include "utility/test_helpers.h"
#include "node/node.h"

WALLET_TEST_INIT
#include "wallet_test_environment.cpp"
#include "mock_bbs_network.cpp"

#include "wallet/client/extensions/broadcast_gateway/broadcast_router.h"
#include "wallet/client/extensions/news_channels/interface.h"
#include "wallet/client/extensions/news_channels/broadcast_msg_validator.h"
#include "wallet/client/extensions/news_channels/broadcast_msg_creator.h"
#include "wallet/client/extensions/news_channels/updates_provider.h"

#include <tuple>

using namespace beam;
using namespace beam::wallet;

namespace
{
    using PrivateKey = ECC::Scalar::Native;
    using PublicKey = PeerID;

    /**
     *  Class to test correct notification of news channels observers
     */
    struct MockNewsObserver : public INewsObserver, public IExchangeRateObserver
    {
        using OnVersion = function<void(const VersionInfo&, const ECC::uintBig&)>;
        using OnRate = function<void(const ExchangeRates&)>;

        MockNewsObserver(OnVersion onVers, OnRate onRate)
            : m_onVers(onVers)
            , m_onRate(onRate) {};

        virtual void onNewWalletVersion(const VersionInfo& v, const ECC::uintBig& s) override
        {
            m_onVers(v, s);
        }
        virtual void onExchangeRates(const ExchangeRates& r) override
        {
            m_onRate(r);
        }

        OnVersion m_onVers;
        OnRate m_onRate;
    };

    /**
     *  Derive key pair with specified @keyIndex
     */
    std::tuple<PublicKey, PrivateKey> deriveKeypair(IWalletDB::Ptr walletDB, uint64_t keyIndex)
    {
        PrivateKey sk;
        PublicKey pk;
        walletDB->get_MasterKdf()->DeriveKey(sk, ECC::Key::ID(keyIndex, Key::Type::Bbs));
        pk.FromSk(sk);
        return std::make_tuple(pk, sk);
    }

    /**
     *  Create signature for @data using key derived with specified @keyIndex
     *  return derived public key and signature
     */
    std::tuple<PublicKey, ByteBuffer> signData(const ByteBuffer& data, uint64_t keyIndex, IWalletDB::Ptr walletDB)
    {
        const auto& [pk, sk] = deriveKeypair(walletDB, keyIndex);
        SignatureHandler signHandler;
        signHandler.m_data = data;
        signHandler.Sign(sk);
        ByteBuffer rawSignature = toByteBuffer(signHandler.m_Signature);
        return std::make_tuple(pk, rawSignature);
    }
    
    /**
     *  Create message with Protocol header according to broadcast protocol
     */
    ByteBuffer addProtocolHeader(const BroadcastMsg& body, BroadcastContentType type)
    {
        ByteBuffer content = toByteBuffer(body);
        ByteBuffer msg(MsgHeader::SIZE + content.size());
        MsgHeader header(0, // V0
                         0, // V1
                         1, // V2
                         static_cast<uint8_t>(type),
                         static_cast<uint8_t>(content.size()));
        header.write(msg.data());
        std::copy(std::cbegin(content),
                  std::cend(content),
                  std::begin(msg) + MsgHeader::SIZE);
        return msg;
    };

    /**
     * Create BroadcastMsg with specified string
     */
    // BroadcastMsg createBroadcastMsg(std::string testString)
    // {
    //     BroadcastMsg res = { toByteBuffer(testString), };
    //     return res;
    // }

    /**
     *  Tests:
     *  - only correctly signed with PrivateKey messageas are accepted
     *  - publishers PublicKeys are correcly accepted
     */
    void TestSignatureValidation()
    {
        cout << endl << "Test BroadcastMsgValidator signature verification" << endl;

        auto senderWalletDB = createSenderWalletDB();
        BroadcastMsgValidator validator;

        // generate key pairs for test
        std::array<std::pair<PublicKey,PrivateKey>,10> keyPairsArray;
        for (size_t i = 0; i < keyPairsArray.size(); ++i)
        {
            const auto [pk, sk] = deriveKeypair(senderWalletDB, i);
            keyPairsArray[i] = std::make_pair(pk, sk);
        }

        // generate cases for test
        const std::array<std::pair<bool, bool>, keyPairsArray.size()> keyDistributionTable = 
        {
            // different combinations with keys:
            // (loadKeyToEndpoint, signDataWithKey)
            std::make_pair(true, true),
            std::make_pair(true, false),
            std::make_pair(false, true),
            std::make_pair(false, false),
            std::make_pair(true, true),
            std::make_pair(true, false),
            std::make_pair(false, true),
            std::make_pair(false, false),
            std::make_pair(true, true),
            std::make_pair(true, true)
        };

        // prepare messages and publisher keys
        std::array<ByteBuffer, keyPairsArray.size()> messages;  // messages for test
        std::vector<PublicKey> publisherKeys;   // keys to load in NewsEndpoint
        for (size_t i = 0; i < keyPairsArray.size(); ++i)
        {
            const auto [to_load, to_sign] = keyDistributionTable[i];
            const auto& [pubKey, privKey] = keyPairsArray[i];

            if (to_load) publisherKeys.push_back(pubKey);

            BroadcastMsg msg;
            msg.m_content = toByteBuffer(std::to_string(i));
            SignatureHandler signHandler;
            signHandler.m_data = msg.m_content;
            if (to_sign) signHandler.Sign(privKey);
            msg.m_signature = toByteBuffer(signHandler.m_Signature);
            messages[i] = toByteBuffer(msg);
        }

        WALLET_CHECK_NO_THROW(validator.setPublisherKeys(publisherKeys));

        // Push messages and check validation result
        for (size_t i = 0; i < messages.size(); ++i)
        {
            const ByteBuffer& data = messages[i];
            BroadcastMsg msg;
            bool res = false;
            WALLET_CHECK_NO_THROW(res = validator.processMessage(data, msg));

            auto [keyLoaded, msgSigned] = keyDistributionTable[i];

            if (res)
            {
                // Only messages with signed with correct keys have to pass validation
                WALLET_CHECK(keyLoaded && msgSigned);
                // Check content
                ByteBuffer referenceContent = toByteBuffer(std::to_string(i));
                WALLET_CHECK(referenceContent == msg.m_content);
            }
            else
            {
                WALLET_CHECK(!keyLoaded || !msgSigned);
                // key not loaded to validator or message not correctly signed
                // are the reasons to fail
            }
        }
        cout << "Test end" << endl;
    }

    /**
     *  Test BroadcastMsgValidator key loading
     */
    void TestPublisherKeysLoading()
    {
        cout << endl << "Test BroadcastMsgValidator keys loading" << endl;

        auto senderWalletDB = createSenderWalletDB();
        BroadcastMsgValidator validator;

        {
            cout << "Case: no keys loaded, not signed message" << endl;

            BroadcastMsg msg;
            msg.m_content = toByteBuffer(std::string("not signed message"));
            SignatureHandler signHandler;
            signHandler.m_data = msg.m_content;
            msg.m_signature = toByteBuffer(signHandler.m_Signature);
            ByteBuffer data = toByteBuffer(msg);

            bool res = false;
            WALLET_CHECK_NO_THROW(res = validator.processMessage(data, msg));
            WALLET_CHECK(!res);
        }
        {
            cout << "Case: no keys loaded, correct message" << endl;

            BroadcastMsg msg;
            msg.m_content = toByteBuffer(std::string("correct message"));

            const auto& [pk, signature] = signData(msg.m_content, 123, senderWalletDB);
            msg.m_signature = signature;
            ByteBuffer data = toByteBuffer(msg);

            bool res = false;
            BroadcastMsg resMsg;
            WALLET_CHECK_NO_THROW(res = validator.processMessage(data, resMsg));
            WALLET_CHECK(!res);

            cout << "Case: key loaded, correct message" << endl;
            WALLET_CHECK_NO_THROW(validator.setPublisherKeys({pk}));
            WALLET_CHECK_NO_THROW(res = validator.processMessage(data, resMsg));
            WALLET_CHECK(res);
            WALLET_CHECK(msg == resMsg);
            
            cout << "Case: keys cleared" << endl;

            WALLET_CHECK_NO_THROW(validator.setPublisherKeys({}));
            WALLET_CHECK(!validator.processMessage(data, resMsg));
        }
        {
            cout << "Case: new key loaded" << endl;

            BroadcastMsg msg;
            msg.m_content = toByteBuffer(std::string("test message"));
            const auto& [pk, signature] = signData(msg.m_content, 159, senderWalletDB);
            msg.m_signature = signature;
            ByteBuffer data = toByteBuffer(msg);

            WALLET_CHECK_NO_THROW(validator.setPublisherKeys({pk}));
            bool res = false;
            BroadcastMsg resMsg;
            WALLET_CHECK_NO_THROW(res = validator.processMessage(data, resMsg));
            WALLET_CHECK(res);
            WALLET_CHECK(msg == resMsg);
        }
        cout << "Test end" << endl;
    }

    void TestNewsChannelsObservers()
    {
        cout << endl << "Test news channels observers" << endl;

        MockBbsNetwork network;
        BroadcastRouter broadcastRouter(network, network);
        BroadcastMsgValidator validator;
        AppUpdateInfoProvider updatesProvider(broadcastRouter, validator);
        // TODO exchange rates provider test
        auto senderWalletDB = createSenderWalletDB();
        
        int notificationCount = 0;

        const VersionInfo verInfo = { VersionInfo::Application::DesktopWallet, Version {123,456,789} };
        // const ExchangeRates rates = {getTimestamp(), { ExchangeRate { ExchangeRates::Currency::Beam,
        //                                                               147852369,
        //                                                               ExchangeRates::Currency::Usd
        //                                                             }}};

        BroadcastMsg msg;
        msg.m_content = toByteBuffer(verInfo);
        const auto& [pk, signature] = signData(msg.m_content, 321, senderWalletDB);
        msg.m_signature = signature;

        MockNewsObserver testObserver(
            [&notificationCount, &verInfo/*, &signature*/]
            (const VersionInfo& v, const ECC::uintBig& s)
            {
                WALLET_CHECK(verInfo == v);
                // WALLET_CHECK(signature == toByteBuffer(s));
                ++notificationCount;
            },
            [&notificationCount/*, &rates*/]
            (const ExchangeRates& r)
            {
                // WALLET_CHECK(rates == r);
                ++notificationCount;
            });


        {
            // loading correct key with 2 additional just for filling
            PublicKey pk2, pk3;
            std::tie(pk2, std::ignore) = deriveKeypair(senderWalletDB, 789);
            std::tie(pk3, std::ignore) = deriveKeypair(senderWalletDB, 456);
            validator.setPublisherKeys({pk, pk2, pk3});
        }

        {
            cout << "Case: subscribed on valid message" << endl;
            updatesProvider.Subscribe(&testObserver);
            broadcastRouter.sendMessage(BroadcastContentType::SoftwareUpdates, msg);
            WALLET_CHECK(notificationCount == 1);
        }
        {
            cout << "Case: unsubscribed on valid message" << endl;
            updatesProvider.Unsubscribe(&testObserver);
            broadcastRouter.sendMessage(BroadcastContentType::SoftwareUpdates, msg);
            WALLET_CHECK(notificationCount == 1);
        }
        {
            cout << "Case: subscribed back" << endl;
            updatesProvider.Subscribe(&testObserver);
            broadcastRouter.sendMessage(BroadcastContentType::SoftwareUpdates, msg);
            WALLET_CHECK(notificationCount == 2);
        }
        {
            cout << "Case: subscribed on invalid message" << endl;
            // sign the same message with other key
            ByteBuffer newSignature;
            std::tie(std::ignore, newSignature) = signData(msg.m_content, 322, senderWalletDB);
            msg.m_signature = newSignature;

            broadcastRouter.sendMessage(BroadcastContentType::SoftwareUpdates, msg);
            WALLET_CHECK(notificationCount == 2);
        }
        cout << "Test end" << endl;
    }

    void TestStringToKeyConvertation()
    {
        cout << endl << "Test string to key convertation" << endl;

        {
            ECC::uintBig referencePubKey = {
                0xdb, 0x61, 0x7c, 0xed, 0xb1, 0x75, 0x43, 0x37,
                0x5b, 0x60, 0x20, 0x36, 0xab, 0x22, 0x3b, 0x67,
                0xb0, 0x6f, 0x86, 0x48, 0xde, 0x2b, 0xb0, 0x4d,
                0xe0, 0x47, 0xf4, 0x85, 0xe7, 0xa9, 0xda, 0xec
            };

            bool res = false;
            PeerID key;
            WALLET_CHECK_NO_THROW(res = BroadcastMsgValidator::stringToPublicKey("db617cedb17543375b602036ab223b67b06f8648de2bb04de047f485e7a9daec", key));
            WALLET_CHECK(res && key == referencePubKey);
        }
        {
            ECC::uintBig referencePrivateKey = {
                0xf7, 0x0c, 0x36, 0xf2, 0xd8, 0x34, 0x2b, 0x66,
                0xe3, 0x08, 0x1e, 0xa4, 0xd8, 0x75, 0x43, 0x56,
                0x6d, 0x6a, 0xd2, 0x42, 0xc6, 0xe6, 0x1d, 0xbf,
                0x92, 0x6d, 0x57, 0xff, 0x42, 0xde, 0x0c, 0x59
            };
            ECC::Scalar refKey;
            refKey.m_Value = referencePrivateKey;
            ECC::Scalar::Native nativeKey;
            nativeKey.Import(refKey);

            bool res = false;
            ECC::Scalar::Native key;
            WALLET_CHECK_NO_THROW(res = BroadcastMsgCreator::stringToPrivateKey("f70c36f2d8342b66e3081ea4d87543566d6ad242c6e61dbf926d57ff42de0c59", key));
            WALLET_CHECK(res && key == nativeKey);
        }
    }

    void TestProtocolBuilder()
    {
        cout << endl << "Test BroadcastMsgCreator" << endl;
        // TODO Newscast test
        /// Create message signed with private key
        // static ByteBuffer createMessage(const NewsMessage& content, const PrivateKey& key);
    }

    void TestSoftwareVersion()
    {
        cout << endl << "Test Version" << endl;

        {
            Version v {123, 456, 789};
            WALLET_CHECK(to_string(v) == "123.456.789");
            // WALLET_CHECK(to_string(Version::getCurrent()) == PROJECT_VERSION);
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
            WALLET_CHECK(res == false);

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

} // namespace

int main()
{
    cout << "News channels tests:" << endl;

    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    TestSignatureValidation();
    TestPublisherKeysLoading();
    TestNewsChannelsObservers();
    TestStringToKeyConvertation();
    // TestProtocolBuilder();
    TestSoftwareVersion();

    assert(g_failureCount == 0);
    return WALLET_CHECK_RESULT;
}

