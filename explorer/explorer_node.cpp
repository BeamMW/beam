#include "server.h"
#include "adapter.h"
#include "node/node.h"
#include "utility/logger.h"
#include "utility/helpers.h"

using namespace beam;

#define FILES_PREFIX "explorer-node_"

struct Options {
    std::string nodeDbFilename;
    std::string accessControlFile;
    std::string nodeConnectTo;
    io::Address nodeListenTo;
    io::Address explorerListenTo;
    int logLevel;
    static const unsigned logRotationPeriod = 3*60*60*1000; // 3 hours
};

static bool parse_cmdline(int argc, char* argv[], Options& o);
static void setup_node(Node& node, const Options& o);

int main(int argc, char* argv[]) {
    Options options;
    if (!parse_cmdline(argc, argv, options)) {
        return 1;
    }
    auto logger = Logger::create(LOG_LEVEL_INFO, options.logLevel, options.logLevel, FILES_PREFIX);
    int retCode = 0;
    try {
        io::Reactor::Ptr reactor = io::Reactor::create();
        io::Reactor::Scope scope(*reactor);
        io::Reactor::GracefulIntHandler gih(*reactor);
        io::Timer::Ptr logRotateTimer = io::Timer::create(*reactor);
        logRotateTimer->start(
            options.logRotationPeriod, true, []() { Logger::get()->rotate(); }
        );
        Node node;
        setup_node(node, options);
        explorer::IAdapter::Ptr adapter = explorer::create_adapter(node);
        node.Initialize();
        explorer::Server server(*adapter, *reactor, options.explorerListenTo, options.accessControlFile);
        LOG_INFO() << "Node listens to " << options.nodeListenTo << ", explorer listens to " << options.explorerListenTo;
        reactor->run();
        LOG_INFO() << "Done";
    } catch (const std::exception& e) {
        LOG_ERROR() << "EXCEPTION: " << e.what();
        retCode = 255;
    } catch (...) {
        LOG_ERROR() << "NON_STD EXCEPTION";
        retCode = 255;
    }
    return retCode;
}

bool parse_cmdline(int argc, char* argv[], Options& o) {
    // TODO cmdline

#ifdef NDEBUG
    o.logLevel = LOG_LEVEL_INFO;
#else
    o.logLevel = LOG_LEVEL_DEBUG;
#endif

    o.nodeDbFilename = FILES_PREFIX "db";
    //o.accessControlFile = "api.keys";

    o.nodeConnectTo = "172.104.249.212:8101";
    o.nodeListenTo.port(10000);
    o.explorerListenTo.port(8888);

    return true;
}

void setup_node(Node& node, const Options& o) {
    Rules::get().UpdateChecksum();
    LOG_INFO() << "Rules signature: " << Rules::get().Checksum;

    node.m_Cfg.m_sPathLocal = o.nodeDbFilename;
    node.m_Cfg.m_Listen.port(o.nodeListenTo.port());
    node.m_Cfg.m_Listen.ip(o.nodeListenTo.ip());
    node.m_Cfg.m_MiningThreads = 0;
    node.m_Cfg.m_VerificationThreads = 1;

    auto& address = node.m_Cfg.m_Connect.emplace_back();
    address.resolve(o.nodeConnectTo.c_str());
}