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

#include "wallet/client/extensions/newscast/newscast.h"
#include "wallet/client/extensions/newscast/newscast_protocol_builder.h"

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
    struct MockNewsObserver : public INewsObserver
    {
        using CheckerFunction = function<void(const NewsMessage& msg)>;

        MockNewsObserver(CheckerFunction checker) :
            m_testChecker(checker) {};

        virtual void onNewsUpdate(const NewsMessage& msg) override
        {
            m_testChecker(msg);
        }

        CheckerFunction m_testChecker;
    };

    /**
     *  Derive key pair with specified @keyIndex
     */
    std::tuple<PublicKey, PrivateKey> deriveKeypair(IWalletDB::Ptr walletDB, uint64_t keyIndex)
    {
        PrivateKey sk;
        PublicKey pk;
        walletDB->get_MasterKdf()->DeriveKey(sk, ECC::Key::ID(keyIndex, Key::Type::Bbs));
        proto::Sk2Pk(pk, sk);
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
     *  Create message according to Newscast protocol
     */
    ByteBuffer makeMsg(const ByteBuffer& msgRaw, const ByteBuffer& signatureRaw)
    {
        ByteBuffer fullMsg(MsgHeader::SIZE);
        size_t rawBodySize = msgRaw.size() + signatureRaw.size();
        assert(rawBodySize <= UINT32_MAX);

        MsgHeader header(0, 0, 1, 1, static_cast<uint32_t>(rawBodySize));
        header.write(fullMsg.data());

        std::copy(  std::begin(msgRaw),
                    std::end(msgRaw),
                    std::back_inserter(fullMsg));
        std::copy(  std::begin(signatureRaw),
                    std::end(signatureRaw),
                    std::back_inserter(fullMsg));

        return fullMsg;
    }

    /**
     * Create NewsMessage with specified string
     */
    NewsMessage createNewsMessage(std::string testString)
    {
        NewsMessage res = { NewsMessage::Type::ExchangeRates, toByteBuffer(testString) };
        return res;
    }

    /**
     *  Test NewscastProtocolParser for stress conditions.
     */
    void TestProtocolParserStress()
    {
        cout << endl << "Test protocol parser stress" << endl;
        
        NewscastProtocolParser parser;
        {
            cout << "Case: empty message" << endl;
            ByteBuffer emptyBuf;
            WALLET_CHECK_NO_THROW(parser.parseMessage(emptyBuf));
        }        
        {
            cout << "Case: message header too short" << endl;
            ByteBuffer data(beam::MsgHeader::SIZE - 2, 't');
            WALLET_CHECK_NO_THROW(parser.parseMessage(data));
        }
        {
            cout << "Case: message contain only header" << endl;
            ByteBuffer data;
            data.reserve(MsgHeader::SIZE);
            MsgHeader header(0,0,1,1,0);
            header.write(data.data());
            WALLET_CHECK_NO_THROW(parser.parseMessage(data));
        }
        {
            cout << "Case: unsupported version" << endl;
            ByteBuffer data;
            data.reserve(MsgHeader::SIZE);
            MsgHeader header(0,0,2,1,0);
            header.write(data.data());
            WALLET_CHECK_NO_THROW(parser.parseMessage(data));
        }
        {
            cout << "Case: wrong length" << endl;
            ByteBuffer data;
            data.reserve(MsgHeader::SIZE);
            MsgHeader header(0,0,1,1,5);
            header.write(data.data());
            WALLET_CHECK_NO_THROW(parser.parseMessage(data));
        }
        {
            cout << "Case: wrong message type" << endl;
            ByteBuffer data;
            data.reserve(MsgHeader::SIZE);
            MsgHeader header(0,0,1,123,0);
            header.write(data.data());
            WALLET_CHECK_NO_THROW(parser.parseMessage(data));
        }
        {
            cout << "Case: wrong body length" << endl;
            ByteBuffer data;
            uint32_t bodyLength = 6;
            data.reserve(MsgHeader::SIZE + bodyLength);
            MsgHeader header(0,0,1,1,bodyLength);
            header.write(data.data());
            WALLET_CHECK_NO_THROW(parser.parseMessage(data));
        }
        cout << "Test end" << endl;
    }

    /**
     *  Tests:
     *  - only correctly signed with PrivateKey messageas are accepted
     *  - publishers PublicKeys are correcly accepted
     */
    void TestSignatureVerification()
    {
        cout << endl << "Test NewscastProtocolParser signature verification" << endl;

        auto senderWalletDB = createSenderWalletDB();
        NewscastProtocolParser parser;

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

            NewsMessage msg = createNewsMessage(std::to_string(i));

            SignatureHandler signature;
            signature.m_data = toByteBuffer(msg);
            if (to_sign) signature.Sign(privKey);

            messages[i] = makeMsg(signature.m_data, toByteBuffer(signature.m_Signature));
        }

        WALLET_CHECK_NO_THROW(parser.setPublisherKeys(publisherKeys));

        // Push messages and check parser result
        for (size_t i = 0; i < messages.size(); ++i)
        {
            const ByteBuffer& data = messages[i];
            boost::optional<NewsMessage> res;
            WALLET_CHECK_NO_THROW(res = parser.parseMessage(data));

            auto [keyLoaded, msgSigned] = keyDistributionTable[i];

            if (res)
            {
                // Only messages with signed with correct keys have to pass validation.
                WALLET_CHECK(keyLoaded && msgSigned);
                // Check content
                NewsMessage referenceMsg = createNewsMessage(std::to_string(i));
                WALLET_CHECK(referenceMsg == *res);
            }
            else
            {
                WALLET_CHECK(!keyLoaded || !msgSigned);
                // key not loaded to parser or message not correctly signed
                // are the reasons to fail parsers validation.
            }
        }
        cout << "Test end" << endl;
    }

    /**
     *  Test NewscastProtocolParser key loading
     */
    void TestPublisherKeysLoading()
    {
        cout << endl << "Test NewscastProtocolParser keys loading" << endl;

        auto senderWalletDB = createSenderWalletDB();
        NewscastProtocolParser parser;

        {
            cout << "Case: no keys loaded, not signed message" << endl;

            const NewsMessage news = createNewsMessage("not signed message");
            ByteBuffer msgRaw = toByteBuffer(news);
            SignatureHandler signatureHandler;
            signatureHandler.m_data = msgRaw;
            ByteBuffer data = makeMsg(msgRaw, toByteBuffer(signatureHandler.m_Signature));

            boost::optional<NewsMessage> res;
            WALLET_CHECK_NO_THROW(res = parser.parseMessage(data));
            WALLET_CHECK(!res);
        }
        {
            cout << "Case: no keys loaded, correct message" << endl;

            const NewsMessage news = createNewsMessage("correct message");
            ByteBuffer msgRaw = toByteBuffer(news);

            const auto& [pk, signatureRaw] = signData(msgRaw, 123, senderWalletDB);
            ByteBuffer data = makeMsg(msgRaw, signatureRaw);

            boost::optional<NewsMessage> res;
            WALLET_CHECK_NO_THROW(res = parser.parseMessage(data));
            WALLET_CHECK(!res);

            cout << "Case: key loaded, correct message" << endl;
            WALLET_CHECK_NO_THROW(parser.setPublisherKeys({pk}));
            WALLET_CHECK_NO_THROW(res = parser.parseMessage(data));
            WALLET_CHECK(res);
            WALLET_CHECK(*res == news);
            
            cout << "Case: keys cleared" << endl;

            WALLET_CHECK_NO_THROW(parser.setPublisherKeys({}));
            WALLET_CHECK(!parser.parseMessage(data));
        }
        {
            cout << "Case: new key loaded" << endl;

            const NewsMessage news = createNewsMessage("test message");
            ByteBuffer msgRaw = toByteBuffer(news);

            const auto& [pk, signatureRaw] = signData(msgRaw, 159, senderWalletDB);
            ByteBuffer data = makeMsg(msgRaw, signatureRaw);

            WALLET_CHECK_NO_THROW(parser.setPublisherKeys({pk}));
            boost::optional<NewsMessage> res;
            WALLET_CHECK_NO_THROW(res = parser.parseMessage(data));
            WALLET_CHECK(res);
            WALLET_CHECK(*res == news);
        }
        cout << "Test end" << endl;
    }

    void TestNewscastObservers()
    {
        cout << endl << "Test Newscast observers" << endl;

        MockBbsNetwork network;
        NewscastProtocolParser parser;
        Newscast newsEndpoint(network, parser);
        auto senderWalletDB = createSenderWalletDB();
        
        int notificationCount = 0;
        const NewsMessage news = { NewsMessage::Type::WalletUpdateNotification, toByteBuffer("test message") };
        MockNewsObserver testObserver(
            // void onNewsUpdate(const NewsMessage& msg)
            [&notificationCount, &news]
            (const NewsMessage& receivedNews)
            {
                WALLET_CHECK(news == receivedNews);
                ++notificationCount;
            });

        WalletID channel;   // only channel is used in WalletID structure
        channel.m_Channel = Newscast::BbsChannelsOffset;

        ByteBuffer msgRaw = toByteBuffer(news);
        const auto& [pk, signatureRaw] = signData(msgRaw, 321, senderWalletDB);
        ByteBuffer data = makeMsg(msgRaw, signatureRaw);

        {
            PublicKey pk2, pk3;
            std::tie(pk2, std::ignore) = deriveKeypair(senderWalletDB, 789);    // just for amount
            std::tie(pk3, std::ignore) = deriveKeypair(senderWalletDB, 456);
            parser.setPublisherKeys({pk, pk2, pk3});
        }

        {
            cout << "Case: subscribed on valid message" << endl;
            newsEndpoint.Subscribe(&testObserver);
            network.SendRawMessage(channel, data);
            WALLET_CHECK(notificationCount == 1);
        }
        {
            cout << "Case: unsubscribed on valid message" << endl;
            newsEndpoint.Unsubscribe(&testObserver);
            network.SendRawMessage(channel, data);
            WALLET_CHECK(notificationCount == 1);
        }
        {
            cout << "Case: subscribed back" << endl;
            newsEndpoint.Subscribe(&testObserver);
            network.SendRawMessage(channel, data);
            WALLET_CHECK(notificationCount == 2);
        }
        {
            cout << "Case: subscribed on invalid message" << endl;
            // sign the same message with other key
            ByteBuffer newSignatureRaw;
            std::tie(std::ignore, newSignatureRaw) = signData(msgRaw, 322, senderWalletDB);
            data = makeMsg(msgRaw, newSignatureRaw);

            network.SendRawMessage(channel, data);
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
            auto pubKey = NewscastProtocolParser::stringToPublicKey("db617cedb17543375b602036ab223b67b06f8648de2bb04de047f485e7a9daec");
            WALLET_CHECK(pubKey && *pubKey == referencePubKey);
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
            auto pubKey = NewscastProtocolBuilder::stringToPrivateKey("f70c36f2d8342b66e3081ea4d87543566d6ad242c6e61dbf926d57ff42de0c59");
            WALLET_CHECK(pubKey && *pubKey == nativeKey);
        }
    }

    void TestProtocolBuilder()
    {
        cout << endl << "Test NewscastProtocolBuilder" << endl;
        // TODO Newscast test
        /// Create message signed with private key
        // static ByteBuffer createMessage(const NewsMessage& content, const PrivateKey& key);
    }


} // namespace

int main()
{
    cout << "Newscast tests:" << endl;

    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    TestProtocolParserStress();
    TestSignatureVerification();
    TestPublisherKeysLoading();
    TestNewscastObservers();
    TestStringToKeyConvertation();
    TestProtocolBuilder();

    assert(g_failureCount == 0);
    return WALLET_CHECK_RESULT;
}

