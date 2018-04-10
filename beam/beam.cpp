#include "core/common.h"

#include "node.h"
#include "wallet/wallet.h"
#include "core/ecc_native.h"

#include <iostream>
#include <boost/program_options.hpp>

namespace po = boost::program_options;

// !TODO: replace cout with log

int main(int argc, char* argv[])
{
    po::options_description desc("allowed options");
    desc.add_options()
        ("help,h", "list of all options")
        ("node,n", "start Beam node on the given port")
        ("wallet,w", "start Beam wallet on the given port")
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

            std::cout << "starting a node on " << config.port << " port..." << std::endl;

            beam::Node node;
            node.listen(config);
        } 
        else if (vm.count("wallet")) 
        {
            std::cout << "starting a wallet..." << std::endl;

            beam::Wallet wallet;
            wallet.sendDummyTransaction();
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
