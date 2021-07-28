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

// test helpers and mocks
#include "test_helpers.h"
WALLET_TEST_INIT
#include "mock_bbs_network.cpp"

// tested module
#include "wallet/client/extensions/broadcast_gateway/broadcast_router.h"
#include "wallet/client/extensions/broadcast_gateway/broadcast_msg_validator.h"
#include "wallet/client/extensions/broadcast_gateway/broadcast_msg_creator.h"

// dependencies
#include "boost/optional.hpp"
#include <boost/filesystem.hpp>

using namespace std;
using namespace beam;

namespace
{
    using PrivateKey = ECC::Scalar::Native;
    using PublicKey = PeerID;

    const string dbFileName = "wallet.db";

    struct MockBroadcastListener : public IBroadcastListener
    {
        using OnMessage = function<void(BroadcastMsg&)>;

        MockBroadcastListener(OnMessage func) : m_callback(func) {};

        virtual bool onMessage(uint64_t unused, ByteBuffer&& msg) override
        {
            BroadcastMsg bMsg;
            if (fromByteBuffer(msg, bMsg))
            {
                m_callback(bMsg);
                return true;
            }
            else
            {
                cout << "MockBroadcastListener message deserialization error" << endl;
            }
            return false;
        };

        virtual bool onMessage(uint64_t unused, BroadcastMsg&& bMsg) override
        {
            m_callback(bMsg);
            return true;
        };

        OnMessage m_callback;
    };

    IWalletDB::Ptr createSqliteWalletDB()
    {
        if (boost::filesystem::exists(dbFileName))
        {
            boost::filesystem::remove(dbFileName);
        }
        ECC::NoLeak<ECC::uintBig> seed;
        seed.V = 10283UL;
        auto walletDB = WalletDB::init(dbFileName, string("pass123"), seed);
        beam::Block::SystemState::ID id = { };
        id.m_Height = 134;
        walletDB->setSystemStateID(id);
        return walletDB;
    }

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

    ByteBuffer testMsgCreate(const ByteBuffer& content, BroadcastContentType type)
    {
        ByteBuffer msg(MsgHeader::SIZE);
        MsgHeader header(0, // V0
                         0, // V1
                         1, // V2
                         static_cast<uint8_t>(type),
                         static_cast<uint8_t>(content.size()));
        header.write(msg.data());
        std::copy(std::cbegin(content),
                  std::cend(content),
                  std::back_inserter(msg));
        return msg;
    };

    void TestProtocolParsing()
    {
        cout << endl << "Test protocol parser stress" << endl;

        auto mockNetwork = MockBbsNetwork::CreateInstance();
        BroadcastRouter broadcastRouter(mockNetwork, *mockNetwork, MockTimestampHolder::CreateInstance());
        
        BroadcastMsg testContent = { {'t','e','s','t'}, {'t','e','s','t'} };

        uint32_t correctMessagesCount = 0;
        MockBroadcastListener testListener(
            [&correctMessagesCount, &testContent]
            (BroadcastMsg& msg)
            {
                ++correctMessagesCount;
                WALLET_CHECK(msg == testContent);
            });

        auto testContentType = BroadcastContentType::SwapOffers;
        broadcastRouter.registerListener(testContentType, &testListener);

        WalletID dummyWid;
        dummyWid.m_Channel = proto::Bbs::s_BtcSwapOffersChannel;
        {
            cout << "Case: empty message" << endl;
            ByteBuffer emptyBuf;

            WALLET_CHECK_NO_THROW(
                mockNetwork->SendRawMessage(dummyWid, emptyBuf)
            );
            WALLET_CHECK(correctMessagesCount == 0);
        }
        {
            cout << "Case: message header too short" << endl;
            ByteBuffer data(beam::MsgHeader::SIZE - 2, 't');

            WALLET_CHECK_NO_THROW(
                mockNetwork->SendRawMessage(dummyWid, data)
            );
            WALLET_CHECK(correctMessagesCount == 0);
        }
        {
            cout << "Case: message contain only header" << endl;
            ByteBuffer data(MsgHeader::SIZE, 0);
            MsgHeader header(0,0,1,0,0);
            header.write(data.data());
                        
            WALLET_CHECK_NO_THROW(
                mockNetwork->SendRawMessage(dummyWid, data)
            );
            WALLET_CHECK(correctMessagesCount == 0);
        }
        {
            ByteBuffer data(MsgHeader::SIZE, 0);
            MsgHeader header(0,0,2,0,0);
            header.write(data.data());
                        
            WALLET_CHECK_NO_THROW(
                mockNetwork->SendRawMessage(dummyWid, data)
            );
            WALLET_CHECK(correctMessagesCount == 0);
        }
        {
            cout << "Case: unsupported version" << endl;
            ByteBuffer data(MsgHeader::SIZE, 0);
            MsgHeader header(1,2,3,0,0);
            header.write(data.data());
            
            WALLET_CHECK_NO_THROW(
                mockNetwork->SendRawMessage(dummyWid, data)
            );
            WALLET_CHECK(correctMessagesCount == 0);
        }
        {
            cout << "Case: wrong message type" << endl;
            ByteBuffer data(MsgHeader::SIZE, 0);
            MsgHeader header(0,0,1,123,0);
            header.write(data.data());
            
            WALLET_CHECK_NO_THROW(
                mockNetwork->SendRawMessage(dummyWid, data)
            );
            WALLET_CHECK(correctMessagesCount == 0);
        }
        {
            cout << "Case: length more than real" << endl;
            uint32_t bodyLength = 6;
            ByteBuffer data(MsgHeader::SIZE + bodyLength - 1, 0);
            MsgHeader header(0,0,1,0,bodyLength);
            header.write(data.data());
            
            WALLET_CHECK_NO_THROW(
                mockNetwork->SendRawMessage(dummyWid, data)
            );
            WALLET_CHECK(correctMessagesCount == 0);
        }
        {
            cout << "Case: correct message" << endl;

            auto msg = testMsgCreate(toByteBuffer(testContent), testContentType);
            WALLET_CHECK_NO_THROW(
                mockNetwork->SendRawMessage(dummyWid, msg)
            );
            WALLET_CHECK(correctMessagesCount == 1);
        }
        broadcastRouter.unregisterListener(testContentType);

        cout << "Test end" << endl;
    }

    void TestRouter()
    {
        cout << endl << "Test router" << endl;

        {
            std::cout << "Case: listening to network messages" << endl;

            auto mockNetwork = MockBbsNetwork::CreateInstance();
            BroadcastRouter broadcastRouter(mockNetwork, *mockNetwork, MockTimestampHolder::CreateInstance());
            uint32_t executed = 0;

            const BroadcastMsg testSample = { {'s','w','a','p'}, {'s','w','a','p'} };
            MockBroadcastListener testListener(
                [&executed, &testSample]
                (BroadcastMsg& msg)
                {
                    ++executed;                    
                    WALLET_CHECK(msg == testSample);
                });

            broadcastRouter.registerListener(BroadcastContentType::SwapOffers, &testListener);

            WalletID dummyWid;
            dummyWid.m_Channel = proto::Bbs::s_BtcSwapOffersChannel;
            auto msgA = testMsgCreate(toByteBuffer(testSample), BroadcastContentType::SwapOffers);
            mockNetwork->SendRawMessage(dummyWid, msgA);

            WALLET_CHECK(executed == 1);
        }
        {
            std::cout << "Case: broadcasting messages to network" << endl;

            auto mockNetwork = MockBbsNetwork::CreateInstance();
            BroadcastRouter broadcastRouterA(mockNetwork, *mockNetwork, MockTimestampHolder::CreateInstance());
            BroadcastRouter broadcastRouterB(mockNetwork, *mockNetwork, MockTimestampHolder::CreateInstance());
            uint32_t executed = 0;
            BroadcastMsg msgA = { {'m','s','g','A'}, {'s','i','g','n','A'} };
            BroadcastMsg msgB = { {'m','s','g','B'}, {'s','i','g','n','B'} };

            MockBroadcastListener testListener(
                [&executed, &msgA, &msgB]
                (BroadcastMsg& msg)
                {
                    switch (++executed)
                    {
                    case 1:
                        WALLET_CHECK(msg == msgA);
                        break;
                    case 2:
                        WALLET_CHECK(msg == msgB);
                        break;
                    default:
                        WALLET_CHECK(false);
                        break;
                    }
                });

            broadcastRouterA.registerListener(BroadcastContentType::ExchangeRates, &testListener);
            broadcastRouterB.registerListener(BroadcastContentType::SoftwareUpdates, &testListener);

            broadcastRouterB.sendMessage(BroadcastContentType::ExchangeRates, msgA);
            broadcastRouterA.sendMessage(BroadcastContentType::SoftwareUpdates, msgB);
            WALLET_CHECK(executed == 2);
        }

        cout << "Test end" << endl;
    }

    /**
     *  Tests:
     *  - only correctly signed with PrivateKey messageas are accepted
     *  - publishers PublicKeys are correcly accepted
     */
    void TestBroadcastMsgValidator()
    {
        cout << endl << "Test BroadcastMsgValidator signature verification" << endl;

        auto walletDB = createSqliteWalletDB();
        BroadcastMsgValidator validator;

        // generate key pairs for test
        std::array<std::pair<PublicKey,PrivateKey>,4> keyPairsArray;
        for (size_t i = 0; i < keyPairsArray.size(); ++i)
        {
            const auto [pk, sk] = deriveKeypair(walletDB, i);
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
            std::make_pair(false, false)
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

        auto walletDB = createSqliteWalletDB();
        BroadcastMsgValidator validator;

        {
            cout << "Case: string to public key convertation" << endl;
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

            const auto& [pk, signature] = signData(msg.m_content, 123, walletDB);
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
            const auto& [pk, signature] = signData(msg.m_content, 159, walletDB);
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

    void TestBroadcastMsgCreator()
    {
        cout << endl << "Test BroadcastMsgCreator" << endl;
        
        {
            cout << "Case: string to private key convertation" << endl;
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

        BroadcastMsgValidator validator;
        auto walletDB = createSqliteWalletDB();

        const auto& [pk, sk] = deriveKeypair(walletDB, 1);
        validator.setPublisherKeys({ pk });

        BroadcastMsg msg;
        BroadcastMsg outMsg;
        WALLET_CHECK_NO_THROW(msg = BroadcastMsgCreator::createSignedMessage(toByteBuffer(std::string("hello")), sk));
        bool res = validator.processMessage(toByteBuffer(msg), outMsg);
        WALLET_CHECK(res);
        WALLET_CHECK(msg == outMsg);
    }

} // namespace


int main()
{
    cout << "Broadcasting tests:" << endl;

    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    TestProtocolParsing();
    TestRouter();
    TestBroadcastMsgValidator();
    TestPublisherKeysLoading();
    TestBroadcastMsgCreator();

    boost::filesystem::remove(dbFileName);
    
    assert(g_failureCount == 0);
    return WALLET_CHECK_RESULT;
}
