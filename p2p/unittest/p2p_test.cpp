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

#include "p2p/p2p.h"
#include "utility/helpers.h"

#define LOG_VERBOSE_ENABLED 0
#include "utility/logger.h"

#pragma warning (disable: 4702) // unreachable code

namespace beam {

struct TestP2PNotifications : P2PNotifications {
    TestP2PNotifications(io::Address p=io::Address()) :  peer(p) {}

    virtual void on_p2p_started(P2P* _p2p) {
        p2p = _p2p;
        threadId = get_thread_id();
        LOG_INFO() << "p2p started, thread=" << threadId;
        assert(async::ctx());
        timer = async::ctx()->set_timer(1500 + rand() % 1500, BIND_THIS_MEMFN(on_timer));
    }

    virtual void on_peer_connected(StreamId id) {
        LOG_INFO() << "new peer handshaked, peer=" << id.address();
        wasConnectedTo.insert(id.address().ip());
    }

    virtual void on_peer_state_updated(StreamId id, const PeerState& newState) {
        LOG_INFO() << "peer state upated, peer=" << id.address()
            << TRACE(newState.tip)
            << TRACE(newState.connectedPeersCount)
            << TRACE(newState.knownServersCount);
    }

    virtual void on_peer_disconnected(StreamId id) {
        LOG_INFO() << "peer disconnected, peer=" << id.address();
    }

    virtual void on_p2p_stopped() {
        LOG_INFO() << "p2p stopped, thread=" << threadId;
        timer.reset();
        p2p=0;
    }

    void on_timer() {
        if (p2p) p2p->update_tip(++tip);
    }

    uint32_t tip = 0;
    P2P* p2p = 0;
    io::Address peer;
    io::Timer::Ptr timer;
    uint64_t threadId=0;
    std::unordered_set<uint32_t> wasConnectedTo;
};

int p2ptest(int numNodes, int runTime) {
#ifndef __linux__
    LOG_WARNING() << "This test runs on linux only";
    return 0;
#endif
    srand((unsigned int) time(0));

    static const uint32_t LOCALHOST_BASE = 0x7F000001;
    static const uint16_t PORT_BASE = 20000;

    std::vector<TestP2PNotifications> callbacks;
    std::vector<std::unique_ptr<P2P>> nodes;
    nodes.reserve(numNodes);
    callbacks.reserve(numNodes);

    P2PSettings settings;

    // seed initial server address
    settings.priorityPeers.emplace_back(LOCALHOST_BASE, PORT_BASE);

    LOG_INFO() << "Creating " << numNodes << " nodes";
    for (int i=0; i<numNodes; ++i) {
        //settings.peerId = i+1;
        settings.bindToIp = LOCALHOST_BASE + i;
        // not all nodes listen
        settings.listenToPort = ( (i+1)%3 == 0 ) ? 0 : uint16_t(PORT_BASE + i);
        // ~ etc

        callbacks.emplace_back(io::Address(settings.bindToIp, settings.listenToPort));
        nodes.push_back(std::make_unique<P2P>(callbacks.back(), settings));
    }

    LOG_INFO() << "Starting nodes";
    for (auto& n : nodes) {
        n->start();
    }

    LOG_INFO() << "Waiting for signal or " << runTime << " seconds";
    wait_for_termination(runTime);

    LOG_INFO() << "Waiting for nodes to quit";
    nodes.clear();

    LOG_INFO() << "Done\n====================================================";
    for (const auto& c : callbacks) {
        LOG_INFO() << c.peer << " was connected to " << c.wasConnectedTo.size() << " peers";
    }
    return 0;
}

int p2ptest_1(io::Address seedAddr, uint16_t port) {
    std::unique_ptr<P2P> node;

    LOG_INFO() << "Creating node";

    TestP2PNotifications callbacks;
    P2PSettings settings;

    // seed initial server address
    settings.priorityPeers.emplace_back(seedAddr.port((uint16_t) port));

    node = std::make_unique<P2P>(callbacks, settings);

    LOG_INFO() << "Starting node";
    node->start();

    LOG_INFO() << "Waiting for signal";
    wait_for_termination(0);

    LOG_INFO() << "Waiting for nodes to quit";
    node.reset();

    LOG_INFO() << "Done";
    return 0;
}

} //namespace

static const int DEF_NUM_NODES = 3;
static const int DEF_RUN_TIME = 5;

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
    logger->set_time_format("%T", true);

    bool isUnittest = true;
    int numNodes = DEF_NUM_NODES;
    int runTimeOrPort = DEF_RUN_TIME;
    io::Address seedAddr;

    if (argc > 1) {
        if (strchr(argv[1], ':') && seedAddr.resolve(argv[1]) && seedAddr.ip())
            isUnittest = false;
        else {
            numNodes = atoi(argv[1]);
            if (numNodes <= 0) numNodes = DEF_NUM_NODES;
        }
    }

    if (argc > 2) {
        runTimeOrPort = atoi(argv[2]);
        if (runTimeOrPort <= 0) {
            if (isUnittest) runTimeOrPort = DEF_RUN_TIME;
            else runTimeOrPort = 0;
        }
    }

    try {
        if (isUnittest)
            return p2ptest(numNodes, runTimeOrPort);
        else
            return p2ptest_1(seedAddr, static_cast<uint16_t>(runTimeOrPort));
    } catch (const std::exception& e) {
        LOG_ERROR() << "Exception: " << e.what();
    } catch (...) {
        LOG_ERROR() << "Non-std exception";
    }
    return 255;
}
