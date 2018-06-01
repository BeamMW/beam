#include "p2p/p2p.h"
#include "utility/helpers.h"

#define LOG_VERBOSE_ENABLED 0
#define LOG_DEBUG_ENABLED 1
#include "utility/logger.h"

namespace beam {

int p2ptest(int numNodes, int runTime) {
#ifndef __linux__
    LOG_WARNING() << "This test runs on linux only";
    return 0;
#endif
    static const uint32_t LOCALHOST_BASE = 0x7F000001;
    static const uint16_t PORT_BASE = 20000;

    std::vector<std::unique_ptr<P2P>> nodes;
    nodes.reserve(numNodes);

    LOG_DEBUG() << "Creating " << numNodes << " nodes";
    for (int i=0; i<numNodes; ++i) {
        // odd node numbers are not servers
        uint16_t listenTo = (i & 1) ? 0 : PORT_BASE + i;
        nodes.push_back(std::make_unique<P2P>(io::Address(LOCALHOST_BASE + i, 0), listenTo));
    }

    LOG_DEBUG() << "Seeding all of them initial server address";
    KnownServers seed { {io::Address(LOCALHOST_BASE, PORT_BASE), 1} };
    for (auto& n : nodes) {
        n->add_known_servers(seed);
    }

    LOG_DEBUG() << "Starting nodes";
    for (auto& n : nodes) {
        n->start();
    }

    LOG_DEBUG() << "Waiting for signal or " << runTime << " seconds";
    wait_for_termination(runTime);

    LOG_DEBUG() << "Waiting for nodes to quit";
    nodes.clear();

    LOG_DEBUG() << "Done";
    return 0;
}

} //namespace

static const int DEF_NUM_NODES = 47;

int main() {
    using namespace beam;

    int logLevel = LOG_LEVEL_DEBUG;
#if LOG_VERBOSE_ENABLED
    logLevel = LOG_LEVEL_VERBOSE;
#endif
    auto logger = Logger::create(logLevel, logLevel);

    try {
        return p2ptest(DEF_NUM_NODES, 6);
    } catch (const std::exception& e) {
        LOG_ERROR() << "Exception: " << e.what();
    } catch (...) {
        LOG_ERROR() << "Non-std exception";
    }
    return 255;
}
