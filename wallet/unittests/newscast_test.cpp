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

using namespace beam;
using namespace beam::wallet;
using namespace std;

namespace
{
    /**
     *  Class to test correct notification of news channels observers
     */
    struct MockNewsObserver : public INewsObserver
    {
        using CheckerFunction = function<void(NewsMessage msg)>;
        MockNewsObserver(CheckerFunction checker) :
            m_testChecker(checker) {};

        virtual void onNewsUpdate(NewsMessage msg) override
        {
            m_testChecker(msg);
        }

        CheckerFunction m_testChecker;
    };

    /**
     *  Pack data to structure w/o changes.
     */
    BbsMsg makeBbsMsg(ByteBuffer fullMsgRaw)
    {
        BbsMsg m;
        m.m_Channel = BbsChannel(proto::Bbs::s_MaxChannels + Newscast::BbsChannelsOffset);
        m.m_TimePosted = getTimestamp();
        m.m_Message = fullMsgRaw;
        return m;
    }
    
    /**
     *  Add protocol defined header to data and pack to structure.
     */
    BbsMsg makeBbsMsg(ByteBuffer msgRaw, ByteBuffer signatureRaw)
    {
        BbsMsg m;
        m.m_Channel = BbsChannel(proto::Bbs::s_MaxChannels + Newscast::BbsChannelsOffset);
        m.m_TimePosted = getTimestamp();

        ByteBuffer rawFullMsg(MsgHeader::SIZE);
        size_t rawBodySize = msgRaw.size() + signatureRaw.size();
        assert(rawBodySize <= UINT32_MAX);
        MsgHeader header(0, 0, 1, 1, static_cast<uint32_t>(rawBodySize));
        header.write(rawFullMsg.data());
        std::copy(std::begin(msgRaw), std::end(msgRaw), std::back_inserter(rawFullMsg));
        std::copy(std::begin(signatureRaw), std::end(signatureRaw), std::back_inserter(rawFullMsg));

        m.m_Message = rawFullMsg;
        return m;
    }

    /**
     *  Tests NewsEndpoint according to BBS news and updates broadcasting protocol for stress conditions.
     */
    void TestProtocolStress()
    {
        cout << endl << "Test protocol corruption" << endl;
        
        MockBbsNetwork mockNetwork;
        Newscast newsEndpoint(mockNetwork);
        
        {
            cout << "Case: empty message" << endl;
            WALLET_CHECK_NO_THROW(newsEndpoint.OnMsg(makeBbsMsg(ByteBuffer())));
        }        
        {
            cout << "Case: message header too short" << endl;
            WALLET_CHECK_NO_THROW(newsEndpoint.OnMsg(makeBbsMsg(ByteBuffer(beam::MsgHeader::SIZE - 2, 't'))));
        }
        {
            cout << "Case: message contain only header" << endl;
            ByteBuffer data;
            data.reserve(MsgHeader::SIZE);
            MsgHeader header(0,0,1,1,0);
            header.write(data.data());
            WALLET_CHECK_NO_THROW(newsEndpoint.OnMsg(makeBbsMsg(data)));
        }
        {
            cout << "Case: unsupported version" << endl;
            ByteBuffer data;
            data.reserve(MsgHeader::SIZE);
            MsgHeader header(0,0,2,1,0);
            header.write(data.data());
            WALLET_CHECK_NO_THROW(newsEndpoint.OnMsg(makeBbsMsg(data)));
        }
        {
            cout << "Case: wrong length" << endl;
            ByteBuffer data;
            data.reserve(MsgHeader::SIZE);
            MsgHeader header(0,0,1,1,5);
            header.write(data.data());
            WALLET_CHECK_NO_THROW(newsEndpoint.OnMsg(makeBbsMsg(data)));
        }
        {
            cout << "Case: wrong message type" << endl;
            ByteBuffer data;
            data.reserve(MsgHeader::SIZE);
            MsgHeader header(0,0,1,123,0);
            header.write(data.data());
            WALLET_CHECK_NO_THROW(newsEndpoint.OnMsg(makeBbsMsg(data)));
        }
        {
            cout << "Case: wrong body length" << endl;
            ByteBuffer data;
            uint32_t bodyLength = 6;
            data.reserve(MsgHeader::SIZE + bodyLength);
            MsgHeader header(0,0,1,1,bodyLength);
            header.write(data.data());
            WALLET_CHECK_NO_THROW(newsEndpoint.OnMsg(makeBbsMsg(data)));
        }
        cout << "Test end" << endl;
    }

    /**
     *  Tests:
     *  - only correctly signed with PrivateKey messageas are accepted
     *  - publishers PublicKeys are correcly accepted
     *  - observers are notified about news correctly
     */
    void TestSignature()
    {
        cout << endl << "Test signature verification" << endl;

        auto senderWalletDB = createSenderWalletDB();
        MockBbsNetwork mockNetwork;
        Newscast newsEndpoint(mockNetwork);

        using PrivateKey = ECC::Scalar::Native;
        using PublicKey = PeerID;

        std::array<std::pair<PublicKey,PrivateKey>,10> keyPairsArray;
        for (int i = 0; i < keyPairsArray.size(); ++i)
        {
            PrivateKey sk;
            PublicKey pk;
            senderWalletDB->get_MasterKdf()->DeriveKey(sk, ECC::Key::ID(i, Key::Type::Bbs));
            proto::Sk2Pk(pk, sk);
            keyPairsArray[i] = std::make_pair(pk, sk);
        }

        std::array<std::pair<bool, bool>, keyPairsArray.size()> keyDistributionTable = 
        {
            // different combinations with keys:
            // (loadKeyToEndpoint, signMessageWithKey)
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

        std::array<BbsMsg, keyPairsArray.size()> bbsMessages;   // messages to broadcast
        std::vector<PublicKey> pubKeys;     // keys to load in NewsEndpoint
        for (int i = 0; i < keyPairsArray.size(); ++i)
        {
            const auto [to_load, to_sign] = keyDistributionTable[i];
            const auto& [pubKey, privKey] = keyPairsArray[i];

            if (to_load) pubKeys.push_back(pubKey);

            NewsMessage msg = { std::to_string(i) };

            SignatureConfirmation signature;
            const auto msgRaw = toByteBuffer(msg);
            signature.m_data = msgRaw;
            if (to_sign) signature.Sign(privKey);

            bbsMessages[i] = makeBbsMsg(msgRaw, toByteBuffer(signature.m_Signature));
        }

        newsEndpoint.setPublicKeys(pubKeys);

        // Subscribe test checker
        int countReceived = 0;
        MockNewsObserver testObserver(
            [&keyDistributionTable, &countReceived](NewsMessage msg)
            {
                cout << "Case: " << msg.m_content << endl;
                int keyIdx = std::stoi(msg.m_content);
                assert(keyIdx >= 0 && keyIdx < keyDistributionTable.size());
                auto [sharedKey, msgSigned] = keyDistributionTable[keyIdx];
                WALLET_CHECK(sharedKey && msgSigned);
                // Only messages with signed with keys set to NewsEndpoint
                // have to appear here.
                ++countReceived;
            });
        newsEndpoint.Subscribe(&testObserver);

        // Push messages and check
        std::for_each(  std::cbegin(bbsMessages),
                        std::cend(bbsMessages),
                        [&newsEndpoint](BbsMsg m)
                        {
                            newsEndpoint.OnMsg(std::move(m));
                        });

        // Count amount of valid messages and compare to amount successfully received
        int countValid = 0;
        std::for_each(  std::cbegin(keyDistributionTable),
                        std::cend(keyDistributionTable),
                        [&countValid](const auto& pair)
                        {
                            if (pair.first && pair.second)
                                ++countValid;
                        });
        WALLET_CHECK(countReceived == countValid);
        
        cout << "Test end" << endl;
    }

} // namespace

int main()
{
    cout << "NewsEndpoint tests:" << endl;

    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    TestProtocolStress();
    TestSignature();

    assert(g_failureCount == 0);
    return WALLET_CHECK_RESULT;
}

