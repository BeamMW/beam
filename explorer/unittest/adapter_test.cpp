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
#include <wallet/core/common_utils.h>

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

#define verify_test(x) \
    do { \
        if (!(x)) { \
            BEAM_LOG_ERROR() << "Test failed: " #x " at line " << __LINE__; \
            exit(1); \
        } \
    } while (false)

// every mined block is found by its hash, and the answer is the same as by its height
void test_block_by_hash(Node& node, explorer::IAdapter& adapter) {
    auto& proc = node.get_Processor();
    const Height hTip = proc.m_Cursor.m_hh.m_Height;
    verify_test(hTip > 0);

    for (Block::Number n(1); n.v <= proc.m_Cursor.m_Full.m_Number.v; n.v++) {
        Block::SystemState::Full s;
        proc.get_DB().get_State(proc.FindActiveAtStrict(n), s);

        Merkle::Hash hv;
        s.get_Hash(hv);

        json byHash = adapter.get_block_by_hash(hv);
        verify_test(byHash["found"] == true);
        verify_test(byHash == adapter.get_block(s.get_Height(), 0));
    }

    Merkle::Hash hvUnknown = Zero;
    verify_test(adapter.get_block_by_hash(hvUnknown)["found"] == false);
    verify_test(adapter.get_block_by_hash(Blob("short", 5))["found"] == false);

    BEAM_LOG_INFO() << "block by hash: all " << hTip << " blocks found";
}

WaitHandle run_node(const NodeParams& params) {
    WaitHandle ret;
    io::Reactor::Ptr reactor = io::Reactor::create();

    const Rules& r = Rules::get();

    ret.future = std::async(
        std::launch::async,
        [&params, reactor, r]()
        {
            io::Reactor::Scope scope(*reactor);
            Rules::Scope rulesScope(r);

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
                wallet::ReadTreasury(node.m_Cfg.m_Treasury, params.treasuryPath);
                BEAM_LOG_INFO() << "Treasury blocks read: " << node.m_Cfg.m_Treasury.size();
            }

            explorer::IAdapter::Ptr adapter = explorer::create_adapter(node);

            BEAM_LOG_INFO() << "starting a node on " << node.m_Cfg.m_Listen.port() << " port...";
            node.Initialize();
            adapter->Initialize();
            reactor->run();

            test_block_by_hash(node, *adapter);
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

    WaitHandle nodeWH = run_node(nodeParams);

    wait_for_termination(seconds);

    nodeWH.reactor->stop();
    nodeWH.future.get();

    return 0;
}

} //namespace

thread_local const beam::Rules* beam::Rules::s_pInstance = nullptr;

int main(int argc, char* argv[]) {
    using namespace beam;

    beam::Rules r;
    beam::Rules::Scope scopeRules(r);

    const int logLevel = BEAM_LOG_LEVEL_VERBOSE;
    auto logger = Logger::create(logLevel, logLevel);

    logger->set_header_formatter(
        [](char* buf, size_t maxSize, const char* timestampFormatted, const LogMessageHeader& header) -> size_t {
            if (header.line)
                return snprintf(buf, maxSize, "%c %s (%s, %d) ", loglevel_tag(header.level), timestampFormatted, header.func, (int)get_thread_id());
            return snprintf(buf, maxSize, "%c %s (%d) ", loglevel_tag(header.level), timestampFormatted, (int)get_thread_id());
        }
    );
    ECC::InitializeContext();
    r.DA.Target_ms = 1000; // 1 second
    r.DA.Difficulty0 = 1;

    int seconds = 0;
    if (argc > 1) {
        seconds = atoi(argv[1]);
    }
    if (seconds == 0) {
        seconds = 4;
        r.m_Consensus = Rules::Consensus::FakePoW;
        r.TreasuryChecksum = Zero; // no treasury, otherwise the node waits for it and mines nothing
        r.UpdateChecksum();
    }

    int ret = test_adapter(seconds);
    return ret;
}

