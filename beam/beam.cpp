#include "core/common.h"

#include "node.h"
#include "wallet/wallet.h"
#include "wallet/keychain.h"
#include "core/ecc_native.h"
#include "utility/logger.h"

#include <iostream>
#include <boost/program_options.hpp>

namespace po = boost::program_options;

int main(int argc, char* argv[])
{
    po::options_description desc("allowed options");
    desc.add_options()
        ("help,h", "list of all options")
        ("node,n", "start Beam node on the given port")
        ("wallet,w", "start Beam wallet on the given port")
        ("init", "init wallet with password")
        ("pass", po::value<std::string>()->default_value(""), "password for the wallet")
        ("port,p", po::value<int>()->default_value(10000), "port to start the server/wallet on")
    ;

    po::positional_options_description pos;

    try
    {
        po::variables_map vm;
        po::store(po::command_line_parser(argc, argv)
            .options(desc)
            .positional(pos).run(), vm);
        po::notify(vm);

        if (vm.count("node")) 
        {
            beam::Node::Config config;
            config.port = vm["port"].as<int>();

            LOG_INFO() << "starting a node on " << config.port << " port...";

            beam::Node node;
            node.listen(config);
        } 
        else if (vm.count("wallet")) 
        {
            LOG_INFO() << "starting a wallet...";

            std::string pass(vm["pass"].as<std::string>());

            if (pass.size())
            {
                auto keychain = vm.count("init") 
                    ? beam::Keychain::init(pass) 
                    : beam::Keychain::open(pass);

                if(keychain)
                {
                    std::cout << "wallet sucessfully created/opened..." << std::endl;
                }
                else
                {
                    std::cout << "something went wrong, wallet not opened..." << std::endl;
                }
            }
            else
            {
                std::cout << "Please, provide password for the wallet." << std::endl;
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
