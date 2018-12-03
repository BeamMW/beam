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

#include "explorer/adapter.h"
#include "node/node.h"
#include "utility/logger.h"
#include <future>
#include <boost/filesystem.hpp>
#include <wallet/unittests/util.h>

namespace beam {

struct WaitHandle {
    io::Reactor::Ptr reactor;
    std::future<void> future;
};

struct NodeParams {
    io::Address nodeAddress;
    io::Address connectTo;
    std::string treasuryPath;
    ECC::uintBig walletSeed;
};

static const uint16_t NODE_PORT=20000;

WaitHandle run_node(const NodeParams& params) {
    WaitHandle ret;
    io::Reactor::Ptr reactor = io::Reactor::create();

    ret.future = std::async(
        std::launch::async,
        [&params, reactor]() {
            io::Reactor::Scope scope(*reactor);
            beam::Node node;

            node.m_Cfg.m_Listen.port(params.nodeAddress.port());
            node.m_Cfg.m_Listen.ip(params.nodeAddress.ip());
            node.m_Cfg.m_MiningThreads = 1;
            node.m_Cfg.m_VerificationThreads = 1;
            node.m_Cfg.m_TestMode.m_FakePowSolveTime_ms = 500;

			node.m_Keys.InitSingleKey(params.walletSeed);

            if (!params.connectTo.empty()) {
                node.m_Cfg.m_Connect.push_back(params.connectTo);
            }
            if (!params.treasuryPath.empty()) {
                ReadTreasury(node.m_Cfg.m_Treasury, params.treasuryPath);
                LOG_INFO() << "Treasury blocks read: " << node.m_Cfg.m_Treasury.size();
            }

            explorer::IAdapter::Ptr adapter = explorer::create_adapter(node);

            LOG_INFO() << "starting a node on " << node.m_Cfg.m_Listen.port() << " port...";
            node.Initialize();
            reactor->run();
        }
    );

    ret.reactor = reactor;
    return ret;
}

#define FILENAME "_xx"

void cleanup_files() {
    boost::filesystem::remove_all(FILENAME);
    boost::filesystem::remove_all(FILENAME "_");
}

int test_adapter(int seconds) {
    cleanup_files();
    using namespace beam;

    NodeParams nodeParams;
    nodeParams.nodeAddress = io::Address::localhost().port(NODE_PORT);
    nodeParams.treasuryPath = FILENAME "_";

    ECC::Hash::Processor()
		<< Blob("xxx", 3)
		>> nodeParams.walletSeed;

    IWalletDB::Ptr kc = init_wallet_db(FILENAME, &nodeParams.walletSeed);

    WaitHandle nodeWH = run_node(nodeParams);

    wait_for_termination(seconds);

    nodeWH.reactor->stop();
    nodeWH.future.get();

    return 0;
}

} //namespace

int main(int argc, char* argv[]) {
    using namespace beam;

    int logLevel = LOG_LEVEL_DEBUG;
#if LOG_VERBOSE_ENABLED
    logLevel = LOG_LEVEL_VERBOSE;
#endif
    auto logger = Logger::create(logLevel, logLevel);
    logger->set_header_formatter(
        [](char* buf, size_t maxSize, const char* timestampFormatted, const LogMessageHeader& header) -> size_t {
            if (header.line)
                return snprintf(buf, maxSize, "%c %s (%s, %d) ", loglevel_tag(header.level), timestampFormatted, header.func, (int)get_thread_id());
            return snprintf(buf, maxSize, "%c %s (%d) ", loglevel_tag(header.level), timestampFormatted, (int)get_thread_id());
        }
    );
    ECC::InitializeContext();
    Rules::get().DesiredRate_s = 1; // 1 minute
    Rules::get().StartDifficulty = 1;

    int seconds = 0;
    if (argc > 1) {
        seconds = atoi(argv[1]);
    }
    if (seconds == 0) {
        seconds = 4;
        Rules::get().FakePoW = true;
    }

    int ret = test_adapter(seconds);
    return ret;
}

