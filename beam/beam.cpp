#include "core/common.h"

#include <iostream>
#include <boost/program_options.hpp>

namespace po = boost::program_options;

int main(int argc, char* argv[])
{
    po::options_description desc("allowed options");

    desc.add_options()
        ("help", "list of all options")
        ("node", "start Beam node")
        ("wallet", "start Beam wallet")
    ;

    try
    {
        po::variables_map vm;
        po::store(po::parse_command_line(argc, argv, desc), vm);
        po::notify(vm);

        if (vm.count("node")) 
        {
            std::cout << "starting as node..." << std::endl;
        } 
        else if (vm.count("wallet")) 
        {
            std::cout << "starting as wallet..." << std::endl;
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
