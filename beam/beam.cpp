#include "wallet/wallet_network.h"
#include "core/common.h"

#include "node.h"
#include "wallet/wallet.h"
#include "wallet/keychain.h"
#include "wallet/wallet_network.h"
#include "core/ecc_native.h"
#include "utility/logger.h"

#include <boost/program_options.hpp>

namespace po = boost::program_options;
using namespace std;
using namespace beam;

int main(int argc, char* argv[])
{
    LoggerConfig lc;
    int logLevel = LOG_LEVEL_DEBUG;
#if LOG_VERBOSE_ENABLED
    logLevel = LOG_LEVEL_VERBOSE;
#endif
    lc.consoleLevel = logLevel;
    lc.flushLevel = logLevel;
    auto logger = Logger::create(lc);

    po::options_description general_options("General options");
    general_options.add_options()
        ("help,h", "list of all options")
        ("port,p", po::value<uint16_t>()->default_value(10000), "port to start the server/wallet on")
        ("mode", po::value<string>(), "mode to execute[node|wallet]")
        ("command", po::value<string>(), "command to execute [send|listen]")    ;

    po::options_description wallet_options("Wallet options");
    wallet_options.add_options()
        ("pass", po::value<string>()->default_value(""), "password for the wallet")
        ("amount,a", po::value<ECC::Amount>(), "amount to send")
        ("receiver_addr,r", po::value<string>(), "address of receiver")
        ("node_addr,n", po::value<string>(), "address of node") ;

    po::options_description options{ "Allowed options" };
    options.add(general_options)
           .add(wallet_options);

    po::positional_options_description pos;
    pos.add("mode", 1)
       .add("command", -1);

    try
    {
        po::variables_map vm;
        po::store(po::command_line_parser(argc, argv)
            .options(options)
            .positional(pos)
            .allow_unregistered().run(), vm);
        po::notify(vm);

        auto port = vm["port"].as<uint16_t>();

        if (vm.count("mode")) {
            auto mode = vm["mode"].as<string>();
            if (mode == "node") {
                beam::Node node;

                node.m_Cfg.m_Listen.port(vm["port"].as<int>());
                node.m_Cfg.m_Listen.ip(INADDR_ANY);
                node.m_Cfg.m_sPathLocal = vm["storage"].as<std::string>();

                LOG_INFO() << "starting a node on " << node.m_Cfg.m_Listen.port() << " port...";

                node.Initialize();
            }
            else if (mode == "wallet" && vm.count("command")) {
                LOG_INFO() << "starting a wallet..."; 
                auto command = vm["command"].as<string>();
                std::string pass(vm["pass"].as<std::string>());
                if (!pass.size()) {
                    LOG_ERROR() << "Please, provide password for the wallet.";
                    return -1;
                }
                auto keychain = command == "init"
                    ? Keychain::init(pass) 
                    : Keychain::open(pass);

                if (!keychain) {
                    LOG_ERROR() << "something went wrong, wallet not opened...";
                    return -1;
                }

                if (command != "send" && command != "listen") {
                    LOG_ERROR() << "unknown command: \'" << command << "\'";
                    return -1;
                }

                LOG_INFO() << "wallet sucessfully created/opened...";

                bool is_server = command == "listen";

                WalletNetworkIO network_io{ io::Address::localhost().port(port), is_server };
                Wallet wallet{ keychain, network_io, is_server ? Wallet::TxCompletedAction() : [&network_io] (auto a) { network_io.stop(); } };
                network_io.set_wallet_proxy(&wallet);

                // resolve address after network io
                io::Address node_addr;
                node_addr.resolve(vm["node_addr"].as<string>().c_str());

                LOG_INFO() << "connecting to node " << node_addr.str();
                // connect to node
                network_io.connect(node_addr, [&wallet](uint64_t tag)
                {
                    LOG_INFO() << "connected to node";
                    wallet.set_node_id(tag);
                });

                if (command == "send") {
                    auto amount = vm["amount"].as<ECC::Amount>();
                    io::Address receiver_addr;
                    receiver_addr.resolve(vm["receiver_addr"].as<string>().c_str());
                    
                    LOG_INFO() << "connecting to receiver " << receiver_addr.str();
                    // connect to receiver
                    network_io.connect(node_addr, [&wallet, amount](uint64_t tag) mutable
                    {
                        LOG_INFO() << "connected to receiver. Sending " << amount;
                        wallet.send_money(tag, move(amount));
                    });
                }
                network_io.start();
            }
        }
        else
        {
            cout << options << std::endl;
        }
    }
    catch(const po::error& e)
    {
        LOG_ERROR() << e.what();
    }

    return 0;
}
