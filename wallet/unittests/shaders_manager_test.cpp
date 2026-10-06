// Copyright 2018 The Beam Team
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

#include "wallet/core/wallet.h"
#include "wallet/core/wallet_db.h"
#include "wallet/core/contracts/i_shaders_manager.h"
#include "core/fly_client.h"
#include "utility/fsutils.h"
#include "utility/logger.h"
#include "test_helpers.h"
#include <boost/filesystem.hpp>

WALLET_TEST_INIT

using namespace beam;
using namespace beam::wallet;

namespace
{
    const char* kWalletDB = "shaders_manager_test.db";

    struct Result
    {
        bool m_Called = false;
        boost::optional<std::string> m_Output;
        boost::optional<std::string> m_Error;
    };

    // #1695: with no connection to a node a shader call waited for the wallet sync forever
    void TestAbortCallsWaitingForNode()
    {
        boost::filesystem::remove(kWalletDB);
        ECC::NoLeak<ECC::uintBig> seed;
        seed.V = 7U;
        auto walletDB = WalletDB::init(kWalletDB, SecString("pass"), seed);

        auto wallet = std::make_shared<Wallet>(walletDB);

        // a node endpoint that is never connected: the wallet won't get in sync
        auto network = std::make_shared<proto::FlyClient::NetworkStd>(*wallet);
        wallet->SetNodeEndpoint(network);

        auto shaders = IShadersManager::CreateInstance(*wallet, "", "", 0);
        const auto app = fsutils::fread(std::string(PROJECT_SOURCE_DIR "/bvm/Shaders/vault/app.wasm"));

        // nothing in progress, nothing to abort
        shaders->AbortCallsWaitingForNode("no node");
        WALLET_CHECK(shaders->IsDone());

        Result r1, r2;
        auto call = [&](Result& r) {
            shaders->CallShader(std::vector<uint8_t>(app), "role=manager,action=view", 1, 0, 0,
                [&r](boost::optional<ByteBuffer>&&, boost::optional<std::string>&& output, boost::optional<std::string>&& error) {
                    r.m_Called = true;
                    r.m_Output = std::move(output);
                    r.m_Error = std::move(error);
                });
        };

        call(r1); // waits for the sync
        call(r2); // queued behind it

        WALLET_CHECK(!r1.m_Called && !r2.m_Called);
        WALLET_CHECK(!shaders->IsDone());

        shaders->AbortCallsWaitingForNode("no node");

        for (const auto* r : { &r1, &r2 })
        {
            WALLET_CHECK(r->m_Called);
            WALLET_CHECK(r->m_Error && (*r->m_Error == "no node"));
            WALLET_CHECK(!r->m_Output);
        }
        WALLET_CHECK(shaders->IsDone());

        // the manager is usable afterwards, the next call waits for the sync again
        Result r3;
        call(r3);
        WALLET_CHECK(!r3.m_Called);

        shaders->AbortCallsWaitingForNode("no node");
        WALLET_CHECK(r3.m_Called && r3.m_Error);

        shaders.reset();
        wallet.reset();
        walletDB.reset();
        boost::filesystem::remove(kWalletDB);
    }
}

thread_local const beam::Rules* beam::Rules::s_pInstance = nullptr;

int main()
{
    const auto logLevel = BEAM_LOG_LEVEL_DEBUG;
    const auto logger = beam::Logger::create(logLevel, logLevel);
    ECC::InitializeContext();

    beam::Rules r;
    beam::Rules::Scope scopeRules(r);

    io::Reactor::Ptr reactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*reactor);

    TestAbortCallsWaitingForNode();

    return WALLET_CHECK_RESULT;
}
