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

// #include <assert.h>
#include <iostream>
// #include <functional>
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

#include "wallet/core/news_channels.h"

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

    BbsMsg makeBbsMsg(ByteBuffer data)
    {
        BbsMsg m;
        m.m_Channel = BbsChannel(proto::Bbs::s_MaxChannels + NewsEndpoint::BbsChannelsOffset);
        m.m_TimePosted = getTimestamp();
        m.m_Message = data;
        return m;
    }

    void TestProtocolStress()
    {
        cout << endl << "Test protocol corruption" << endl;

        MockWallet mockWalletWallet;
        auto senderWalletDB = createSenderWalletDB();
        auto keyKeeper = make_shared<LocalPrivateKeyKeeper>(senderWalletDB, senderWalletDB->get_MasterKdf());
        MockNetwork mockNetwork(mockWalletWallet, senderWalletDB, keyKeeper);
        NewsEndpoint newsEndpoint(mockNetwork);
        
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

} // namespace

int main()
{
    cout << "NewsEndpoint tests:" << endl;

    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    TestProtocolStress();

    assert(g_failureCount == 0);
    return WALLET_CHECK_RESULT;
}

