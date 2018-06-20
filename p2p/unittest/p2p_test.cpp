#include "p2p/p2p.h"
#include "utility/helpers.h"

#define LOG_VERBOSE_ENABLED 0
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

    P2PSettings settings;

    // seed initial server address
    settings.priorityPeers.emplace_back(LOCALHOST_BASE, PORT_BASE);

    LOG_INFO() << "Creating " << numNodes << " nodes";
    for (int i=0; i<numNodes; ++i) {
        //settings.peerId = i+1;
        settings.bindToIp = LOCALHOST_BASE + i;
        // odd node numbers are not servers
        settings.listenToPort = (i & 1) ? 0 : PORT_BASE + i;
        // ~ etc

        nodes.push_back(std::make_unique<P2P>(settings));
    }

    LOG_INFO() << "Starting nodes";
    for (auto& n : nodes) {
        n->start();
    }

    LOG_INFO() << "Waiting for signal or " << runTime << " seconds";
    wait_for_termination(runTime);

    LOG_INFO() << "Waiting for nodes to quit";
    nodes.clear();

    LOG_INFO() << "Done";
    return 0;
}

int p2ptest_1(io::Address seedAddr, int port) {
    std::unique_ptr<P2P> node;

    LOG_INFO() << "Creating node";

    P2PSettings settings;

    // seed initial server address
    settings.priorityPeers.emplace_back(seedAddr.port(port));

    node = std::make_unique<P2P>(settings);

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
            return snprintf(buf, maxSize, "%c %s (%s, %d) ", loglevel_tag(header.level), timestampFormatted, header.func, (int)get_thread_id());
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
            return p2ptest_1(seedAddr, runTimeOrPort);
    } catch (const std::exception& e) {
        LOG_ERROR() << "Exception: " << e.what();
    } catch (...) {
        LOG_ERROR() << "Non-std exception";
    }
    return 255;
}
