#include "wallet/wallet_network.h"
#include "core/common.h"

#include "node.h"
#include "wallet/wallet.h"
#include "wallet/keychain.h"
#include "core/ecc_native.h"
#include "utility/logger.h"

#include <iostream>
#include <boost/program_options.hpp>

namespace po = boost::program_options;
using namespace std;

int main(int argc, char* argv[])
{
    po::options_description desc("Allowed options");
    desc.add_options()
        ("help,h", "list of all options")
        ("node,n", "start Beam node on the given port")
        ("wallet,w", "start Beam wallet on the given port")
        ("init", "init wallet with password")
        ("pass", po::value<std::string>()->default_value(""), "password for the wallet")
        ("port,p", po::value<uint16_t>()->default_value(10000), "port to start the server/wallet on")
        ("mode", po::value<string>()->required(), "mode to execute[node|wallet]")
        ("command", po::value<string>(), "command to execute [send|listen]")
    ;

    po::positional_options_description pos;
    pos.add("mode", 1)
       .add("command", -1);

    try
    {
        po::variables_map vm;
        po::store(po::command_line_parser(argc, argv)
            .options(desc)
            .positional(pos)
            .allow_unregistered().run(), vm);
        po::notify(vm);

        if (vm.count("node")) 
        {
            beam::Node::Config config;
            config.port = vm["port"].as<uint16_t>();

            LOG_INFO() << "starting a node on " << config.port << " port...";

            beam::Node node;
            node.listen(config);
        } 
        else if (vm.count("mode") && vm.count("command")) {
            auto mode = vm["mode"].as<string>();
            auto command = vm["command"].as<string>();
            if (mode == "node") {

            }
            else if (mode == "wallet") {
                LOG_INFO() << "starting a wallet..."; 
                std::string pass(vm["pass"].as<std::string>());
                if (pass.size()) {
                    auto keychain = vm.count("init") 
                        ? beam::Keychain::init(pass) 
                        : beam::Keychain::open(pass);

                    if(keychain) {
                        std::cout << "wallet sucessfully created/opened..." << std::endl;
                    }
                    else {
                        std::cout << "something went wrong, wallet not opened..." << std::endl;
                    }
                }
                else {
                    std::cout << "Please, provide password for the wallet." << std::endl;
                }
                if (command == "send") {

                }
                else if (command == "listen") {
                    auto port = vm["port"].as<uint16_t>();
                    beam::WalletNetworkIO network_io { beam::io::Address::localhost().port(port), true };
                    beam::IKeyChain::Ptr kc = beam::IKeyChain::Ptr();
                    beam::Wallet wallet{ kc, network_io };
                    network_io.start();
                }
            }
        }
        else
        {
            std::cout << desc << std::endl;
        }
    }
    catch(const po::error& e)
    {
        std::cout << e.what() << std::endl;
    }

    return 0;
}
