#include "server.h"
#include "adapter.h"
#include "wallet/secstring.h"
#include "core/ecc_native.h"
#include "core/block_rw.h"

#include "node/node.h"
#include "utility/cli/options.h"
#include "utility/logger.h"
#include "utility/helpers.h"
#include "utility/string_helpers.h"
#include "utility/log_rotation.h"

#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>

#include "version.h"

using namespace beam;
using namespace std;

#define LOG_FILES_DIR "logs"
#define FILES_PREFIX "explorer-node"
#define API_PORT_PARAMETER "api_port"

struct Options {
    std::string nodeDbFilename;
    std::string accessControlFile;
    std::string nodeConnectTo;
    io::Address nodeListenTo;
    io::Address explorerListenTo;
    int logLevel;
    Key::IPKdf::Ptr ownerKey;
    static const unsigned logRotationPeriod = 3*60*60*1000; // 3 hours
    std::vector<uint32_t> whitelist;
    uint32_t logCleanupPeriod;
};

static bool parse_cmdline(int argc, char* argv[], Options& o);
static void setup_node(Node& node, const Options& o);

int main(int argc, char* argv[]) {
    Options options;
    if (!parse_cmdline(argc, argv, options)) {
        return 1;
    }

    const auto path = boost::filesystem::system_complete(LOG_FILES_DIR);
    auto logger = Logger::create(LOG_LEVEL_INFO, options.logLevel, options.logLevel, FILES_PREFIX, path.string());

    clean_old_logfiles(LOG_FILES_DIR, FILES_PREFIX, options.logCleanupPeriod);

    int retCode = 0;
    try {
        io::Reactor::Ptr reactor = io::Reactor::create();
        io::Reactor::Scope scope(*reactor);
        io::Reactor::GracefulIntHandler gih(*reactor);
        io::Timer::Ptr logRotateTimer = io::Timer::create(*reactor);

        LogRotation logRotation(*reactor, options.logRotationPeriod, options.logCleanupPeriod);

        Node node;
        setup_node(node, options);
        explorer::IAdapter::Ptr adapter = explorer::create_adapter(node);
        node.Initialize();
        explorer::Server server(*adapter, *reactor, options.explorerListenTo, options.accessControlFile, options.whitelist);
        LOG_INFO() << "Node listens to " << options.nodeListenTo << ", explorer listens to " << options.explorerListenTo;
        reactor->run();
        LOG_INFO() << "Done";
    } catch (const std::exception& e) {
        LOG_ERROR() << "EXCEPTION: " << e.what();
        retCode = 255;
	}
	catch (const beam::CorruptionException& e) {
		LOG_ERROR() << "Corruption: " << e.m_sErr;
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
        (cli::HELP_FULL, "list of all options")
        (cli::NODE_PEER, po::value<string>()->default_value("eu-node03.masternet.beam.mw:8100"), "peer address")
        (cli::PORT_FULL, po::value<uint16_t>()->default_value(10000), "port to start the local node on")
        (API_PORT_PARAMETER, po::value<uint16_t>()->default_value(8888), "port to start the local api server on")
        (cli::KEY_OWNER, po::value<string>()->default_value(""), "owner viewer key")
        (cli::PASS, po::value<string>()->default_value(""), "password for owner key")
        (cli::IP_WHITELIST, po::value<std::string>()->default_value(""), "IP whitelist")
        (cli::LOG_CLEANUP_DAYS, po::value<uint32_t>()->default_value(5), "old logfiles cleanup period(days)")
    ;

    cliOptions.add(createRulesOptionsDescription());
        
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
            .style(po::command_line_style::default_style ^ po::command_line_style::allow_guessing)
            .run(), vm);

        if (vm.count(cli::HELP))
        {
            cout << cliOptions << std::endl;
            return false;
        }

        {
            std::ifstream cfg("explorer-node.cfg");

            if (cfg)
            {
                po::store(po::parse_config_file(cfg, cliOptions), vm);
            }
        }

        vm.notify();

        o.logCleanupPeriod = vm[cli::LOG_CLEANUP_DAYS].as<uint32_t>() * 24 * 3600;
        o.nodeDbFilename = FILES_PREFIX ".db";
        //o.accessControlFile = "api.keys";

        o.nodeConnectTo = vm[cli::NODE_PEER].as<string>();
        o.nodeListenTo.port(vm[cli::PORT].as<uint16_t>());
        o.explorerListenTo.port(vm[API_PORT_PARAMETER].as<uint16_t>());

        std::string keyOwner = vm[cli::KEY_OWNER].as<string>();
        if (!keyOwner.empty())
        {
            SecString pass;
            if (!beam::read_wallet_pass(pass, vm))
                throw std::runtime_error("Please, provide password for the keys.");

            KeyString ks;
            ks.SetPassword(Blob(pass.data(), static_cast<uint32_t>(pass.size())));

            {
                ks.m_sRes = keyOwner;

                std::shared_ptr<ECC::HKdfPub> kdf = std::make_shared<ECC::HKdfPub>();
                if (!ks.Import(*kdf))
                    throw std::runtime_error("view key import failed");

                o.ownerKey = kdf;
            }
        }

#ifdef WIN32
        WSADATA wsaData = { };
        WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

        std::string whitelist = vm[cli::IP_WHITELIST].as<string>();

        if (!whitelist.empty())
        {
            const auto& items = string_helpers::split(whitelist, ',');

            for (const auto& item : items)
            {
                io::Address addr;

                if (addr.resolve(item.c_str()))
                {
                    o.whitelist.push_back(addr.ip());
                }
                else throw std::runtime_error("IP address not added to whitelist: " + item);
            }
        }

        getRulesOptions(vm);

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
    LOG_INFO() << "Beam Node Explorer " << PROJECT_VERSION << " (" << BRANCH_NAME << ")";
    LOG_INFO() << "Rules signature: " << Rules::get().get_SignatureStr();

    node.m_Cfg.m_sPathLocal = o.nodeDbFilename;
    node.m_Cfg.m_Listen.port(o.nodeListenTo.port());
    node.m_Cfg.m_Listen.ip(o.nodeListenTo.ip());
    node.m_Cfg.m_MiningThreads = 0;
    node.m_Cfg.m_VerificationThreads = 1;

    node.m_Keys.m_pOwner = o.ownerKey;

    auto& address = node.m_Cfg.m_Connect.emplace_back();
    address.resolve(o.nodeConnectTo.c_str());
}