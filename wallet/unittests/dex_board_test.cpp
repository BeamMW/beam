// Copyright 2026 The Beam Team
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

#include "test_helpers.h"
WALLET_TEST_INIT

#include "wallet/client/extensions/dex_board/dex_board.h"

#include <boost/filesystem.hpp>

using namespace beam;
using namespace beam::wallet;
using namespace std;

namespace
{
    const string dbFileName = "dex_board_test.db";

    constexpr Asset::ID kSendAsset = Asset::s_BeamID;
    constexpr Amount kSendAmount = 10'000'000;
    constexpr Asset::ID kReceiveAsset = 3;
    constexpr Amount kReceiveAmount = 5'000;

    // Delivers published orders straight back to the board, as the broadcast network would
    struct LoopbackGateway : public IBroadcastMsgGateway
    {
        IBroadcastListener* m_listener = nullptr;

        void registerListener(BroadcastContentType, IBroadcastListener* listener) override { m_listener = listener; }
        void unregisterListener(BroadcastContentType) override { m_listener = nullptr; }
        void sendRawMessage(BroadcastContentType, ByteBuffer&&) override {}
        void sendMessage(BroadcastContentType, const BroadcastMsg& msg) override
        {
            BroadcastMsg copy = msg;
            m_listener->onMessage(std::move(copy));
        }
    };

    IWalletDB::Ptr createWalletDB()
    {
        boost::filesystem::remove(dbFileName);
        ECC::NoLeak<ECC::uintBig> seed;
        seed.V = 10283UL;
        return WalletDB::init(dbFileName, string("pass123"), seed);
    }

    DexOrderID publishMyOrder(DexBoard& board, IWalletDB& db)
    {
        const uint64_t ownID = 1;
        WalletID wid;
        db.get_SbbsWalletID(wid, ownID);

        DexOrder order(DexOrderID::generate(), wid, ownID,
                       kSendAsset, kSendAmount, "BEAM",
                       kReceiveAsset, kReceiveAmount, "X",
                       3600);
        board.publishOrder(order);
        WALLET_CHECK(board.getDexOrder(order.getID()).is_initialized());
        return order.getID();
    }

    SetTxParameter makeAccept(const DexOrderID& id)
    {
        SetTxParameter msg;
        msg.m_Type = TxType::DexSimpleSwap;
        msg.AddParameter(TxParameterID::ExternalDexOrderID, id);
        return msg;
    }

    // What an honest taker sends: the order mirrored (DexSimpleSwapBuilder::SendToPeer)
    SetTxParameter& addTerms(SetTxParameter& msg, Amount send, Amount receive)
    {
        return msg
            .AddParameter(TxParameterID::Amount, send)
            .AddParameter(TxParameterID::AssetID, kSendAsset)
            .AddParameter(TxParameterID::DexReceiveAmount, receive)
            .AddParameter(TxParameterID::DexReceiveAsset, kReceiveAsset);
    }

    void TestAcceptChecksOrderTerms()
    {
        cout << "\nTest incoming DEX swap must match the order terms" << endl;

        auto db = createWalletDB();
        LoopbackGateway broadcast;
        IRawCommGateway raw;
        DexBoard board(broadcast, raw, *db);
        ISimpleSwapHandler& handler = board;

        {
            auto msg = makeAccept(publishMyOrder(board, *db));
            addTerms(msg, 150'000'000, 1);
            WALLET_CHECK(!handler.acceptIncomingDexSS(msg));
        }
        {
            auto msg = makeAccept(publishMyOrder(board, *db));
            WALLET_CHECK(!handler.acceptIncomingDexSS(msg));
        }
        {
            auto id = publishMyOrder(board, *db);
            auto msg = makeAccept(id);
            addTerms(msg, kSendAmount, kReceiveAmount);
            WALLET_CHECK(handler.acceptIncomingDexSS(msg));
            WALLET_CHECK(!board.getDexOrder(id).is_initialized());
        }
    }
}

thread_local const beam::Rules* beam::Rules::s_pInstance = nullptr;

int main()
{
    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    Rules r;
    Rules::Scope scopeRules(r);
    r.m_Consensus = Rules::Consensus::FakePoW;
    r.UpdateChecksum();

    TestAcceptChecksOrderTerms();

    boost::filesystem::remove(dbFileName);
    return WALLET_CHECK_RESULT;
}
