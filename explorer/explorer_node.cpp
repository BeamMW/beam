#include "server.h"
#include "adapter.h"
#include "node/node.h"
#include "utility/logger.h"
#include "utility/helpers.h"
#include <boost/program_options.hpp>

using namespace beam;
using namespace std;
namespace po = boost::program_options;

#define FILES_PREFIX "explorer-node_"
#define PEER_PARAMETER "peer"
#define PORT_PARAMETER "port"
#define API_PORT_PARAMETER "api_port"
#define HELP_FULL_PARAMETER "help,h"
#define HELP_PARAMETER "help"

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
    
    po::options_description cliOptions("Node explorer options");
    cliOptions.add_options()
        (HELP_FULL_PARAMETER, "list of all options")
        (PEER_PARAMETER, po::value<string>()->default_value("172.104.249.212:8101"), "peer address")
        (PORT_PARAMETER, po::value<uint16_t>()->default_value(10000), "port to start the local node on")
        (API_PORT_PARAMETER, po::value<uint16_t>()->default_value(8888), "port to start the local api server on");
        
#ifdef NDEBUG
    o.logLevel = LOG_LEVEL_INFO;
#else
    o.logLevel = LOG_LEVEL_DEBUG;
#endif


    po::variables_map vm;
    try
    {
        po::store(po::command_line_parser(argc, argv) // value stored first is preferred
            .options(cliOptions)
            .run(), vm);

        if (vm.count(HELP_PARAMETER))
        {
            cout << cliOptions << std::endl;
            return false;
        }

        o.nodeDbFilename = FILES_PREFIX "db";
        //o.accessControlFile = "api.keys";

        o.nodeConnectTo = vm[PEER_PARAMETER].as<string>();
        o.nodeListenTo.port(vm[PORT_PARAMETER].as<uint16_t>());
        o.explorerListenTo.port(vm[API_PORT_PARAMETER].as<uint16_t>());

        return true;
    }
    catch (const po::error& ex)
    {
        cout << ex.what();
        cout << cliOptions << std::endl;
    }
    catch (const exception& ex)
    {
        cout << ex.what();
    }

    return false;
}

void setup_node(Node& node, const Options& o) {
    Rules::get().UpdateChecksum();
    LOG_INFO() << "Rules signature: " << Rules::get().Checksum;

    node.m_Cfg.m_sPathLocal = o.nodeDbFilename;
    node.m_Cfg.m_Listen.port(o.nodeListenTo.port());
    node.m_Cfg.m_Listen.ip(o.nodeListenTo.ip());
    node.m_Cfg.m_MiningThreads = 0;
    node.m_Cfg.m_VerificationThreads = 1;
    node.m_Cfg.m_Sync.m_NoFastSync = true;

    auto& address = node.m_Cfg.m_Connect.emplace_back();
    address.resolve(o.nodeConnectTo.c_str());
}