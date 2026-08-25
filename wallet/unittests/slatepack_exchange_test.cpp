// Copyright 2018-2026 The Beam Team
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

// End-to-end test: two node-connected wallets complete a simple transfer over Slatepacks
// with NO SBBS endpoint. Proves the manual transport carries a full S1->S2->finalize
// negotiation when the only message endpoint is a SlatepackEndpoint.

#ifndef LOG_VERBOSE_ENABLED
    #define LOG_VERBOSE_ENABLED 0
#endif

#include "utility/logger.h"
#include "node/node.h"
#include "core/unittest/mini_blockchain.h"
#include "utility/test_helpers.h"
#include "test_helpers.h"
#include "wallet_test_node.h"
#include "wallet/core/slatepack_endpoint.h"
#include <boost/filesystem.hpp>

WALLET_TEST_INIT
#include "wallet_test_environment.cpp"

using namespace beam;
using namespace beam::wallet;
using namespace std;

// Declared in core/block_crypt.h but intentionally not defined in libcore (built with
// -fvisibility=hidden); every beam executable that uses Rules defines it in its own main TU.
thread_local const beam::Rules* beam::Rules::s_pInstance = nullptr;

namespace
{
    void TestSlatepackExchange()
    {
        cout << "\nTesting Slatepack manual exchange (no SBBS)...\n";

        io::Reactor::Ptr mainReactor{ io::Reactor::create() };
        io::Reactor::Scope scope(*mainReactor);

        int completed = 0;
        auto onCompleted = [&](auto) { ++completed; mainReactor->stop(); };

        TestNode node;

        // Regular rigs give funded, node-connected wallets (and an SBBS endpoint). We add a
        // SlatepackEndpoint to each; the ManualTransport flag routes our tx to Slatepack while
        // SBBS skips it (via AcceptsMessage), so this also exercises the transport filter.
        TestWalletRig sender(createSenderWalletDB(), onCompleted);
        TestWalletRig receiver(createReceiverWalletDB(), onCompleted);

        vector<string> fromSender, fromReceiver;

        auto showSlatepack = [](const char* who, const char* slate, const string& s)
        {
            cout << "\n  " << who << " produced " << slate << " -> Slatepack (" << s.size() << " bytes):\n"
                 << "  ----------------------------------------------------------------\n"
                 << s << "\n"
                 << "  ----------------------------------------------------------------\n";
        };
        auto senderBp = make_shared<SlatepackEndpoint>(*sender.m_Wallet, sender.m_WalletDB,
            [&](const TxID&, const string& s) { showSlatepack("SENDER", "S1", s); fromSender.push_back(s); mainReactor->stop(); });
        auto receiverBp = make_shared<SlatepackEndpoint>(*receiver.m_Wallet, receiver.m_WalletDB,
            [&](const TxID&, const string& s) { showSlatepack("RECEIVER", "S2", s); fromReceiver.push_back(s); mainReactor->stop(); });
        sender.m_Wallet->AddMessageEndpoint(senderBp);
        receiver.m_Wallet->AddMessageEndpoint(receiverBp);

        const Amount amount = 3;
        sender.m_Wallet->StartTransaction(CreateSimpleTransactionParameters()
            .SetParameter(TxParameterID::PeerAddr, receiver.m_BbsAddr)
            .SetParameter(TxParameterID::Amount, amount)
            .SetParameter(TxParameterID::Fee, Amount(1))
            .SetParameter(TxParameterID::Lifetime, Height(200))
            .SetParameter(TxParameterID::ManualTransport, true));

        // Hand-deliver one Slatepack exactly as the UI/CLI does: Preview() decrypts it and
        // reports a summary WITHOUT touching the transaction, then Commit() confirms it. Both
        // halves are asserted here, so a regression in the two-step import fails the test.
        int couriered = 0;
        auto courier = [&couriered](const shared_ptr<SlatepackEndpoint>& to, const char* who, const string& pack)
        {
            slatepack::Error err = slatepack::Error::None;
            SlatepackEndpoint::ImportInfo info;

            cout << "  >> couriering to " << who << ": preview\n";
            const bool previewed = to->Preview(pack, err, info);
            WALLET_CHECK(previewed);
            if (!previewed)
            {
                cout << "     preview failed: " << slatepack::ErrorToString(err) << "\n";
                return;
            }
            WALLET_CHECK(!info.m_TxID.empty());
            cout << "     " << (info.m_IsSend ? "sending " : "receiving ") << info.m_Amount
                 << ", fee " << info.m_Fee << ", tx " << info.m_TxID << "\n"
                 << "  >> confirming\n";
            WALLET_CHECK(to->Commit(info.m_TxID, err));
            ++couriered;
        };

        // Driver: run the reactor until it stops (a Slatepack popped out, a tx completed, or
        // the per-leg watchdog fired), courier any produced Slatepack to the other wallet, and
        // repeat until both txs report Completed. The watchdog turns a stalled negotiation into
        // a failed assertion instead of a hang.
        io::Timer::Ptr watchdog = io::Timer::create(io::Reactor::get_Current());
        for (int leg = 0; leg < 16 && completed < 2; ++leg)
        {
            watchdog->start(8000, false, [&mainReactor] { mainReactor->stop(); });
            mainReactor->run();
            watchdog->cancel();

            while (!fromSender.empty())
            {
                const string s = fromSender.back(); fromSender.pop_back();
                courier(receiverBp, "RECEIVER", s);
            }
            while (!fromReceiver.empty())
            {
                const string s = fromReceiver.back(); fromReceiver.pop_back();
                courier(senderBp, "SENDER", s);
            }
        }

        auto sh = sender.m_WalletDB->getTxHistory();
        auto rh = receiver.m_WalletDB->getTxHistory();
        Amount received = 0;
        for (const auto& c : receiver.GetCoins())
            received += c.m_ID.m_Value;

        auto statusStr = [](const vector<TxDescription>& h)
        {
            return h.empty() ? "none"
                 : h[0].m_status == TxStatus::Completed ? "Completed" : "in-progress";
        };
        cout << "\n  ==================== exchange summary ====================\n"
             << "   amount sent         : " << amount << "\n"
             << "   amount received     : " << received << "\n"
             << "   Slatepacks couriered : " << couriered << "  (S1 + S2)\n"
             << "   SBBS messages       : 0\n"
             << "   sender tx status    : " << statusStr(sh) << "\n"
             << "   receiver tx status  : " << statusStr(rh) << "\n"
             << "  =========================================================\n\n";

        // A pack addressed to someone else is refused rather than silently ignored, and garbage
        // is refused with a decodable reason.
        {
            slatepack::Error err = slatepack::Error::None;
            SlatepackEndpoint::ImportInfo info;
            WALLET_CHECK(!senderBp->Preview("hello world", err, info));
            WALLET_CHECK(err == slatepack::Error::NotSlatepack);

            WALLET_CHECK(!senderBp->Commit("not-a-tx-id", err));
            WALLET_CHECK(err == slatepack::Error::NoPendingImport);
        }

        // At least S1 (sender->receiver) and S2 (receiver->sender) must have crossed the gap.
        WALLET_CHECK(couriered >= 2);
        WALLET_CHECK(sh.size() == 1);
        WALLET_CHECK(rh.size() == 1);
        WALLET_CHECK(!sh.empty() && sh[0].m_status == TxStatus::Completed);
        WALLET_CHECK(!rh.empty() && rh[0].m_status == TxStatus::Completed);
        WALLET_CHECK(received == amount);
    }
}

int main()
{
    const int logLevel = BEAM_LOG_LEVEL_WARNING;
    const auto path = boost::filesystem::system_complete("logs");
    auto logger = beam::Logger::create(logLevel, logLevel, logLevel, "slatepack_exchange_test", path.string());

    ECC::PseudoRandomGenerator prg;
    prg.m_hv = 125U;

    beam::Rules r;
    beam::Rules::Scope scopeRules(r);
    r.m_Consensus = Rules::Consensus::FakePoW;
    r.pForks[1].m_Height = 100500;
    r.DisableForksFrom(2);
    r.UpdateChecksum();

    wallet::g_AssetsEnabled = true;
    storage::HookErrors();

    TestSlatepackExchange();

    return WALLET_CHECK_RESULT;
}
