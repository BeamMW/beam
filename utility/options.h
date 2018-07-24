#pragma once

#include <boost/program_options.hpp>

namespace beam
{
    namespace po = boost::program_options;
    namespace cli
    {
        extern const char* HELP;
        extern const char* HELP_FULL;
        extern const char* MODE;
        extern const char* PORT;
        extern const char* PORT_FULL;
        extern const char* STORAGE;
        extern const char* WALLET_STORAGE;
        extern const char* HISTORY;
        extern const char* TEMP;
        extern const char* IMPORT;
        extern const char* MINING_THREADS;
        extern const char* VERIFICATION_THREADS;
        extern const char* MINER_ID;
        extern const char* NODE_PEER;
        extern const char* PASS;
        extern const char* AMOUNT;
        extern const char* AMOUNT_FULL;
        extern const char* RECEIVER_ADDR;
        extern const char* RECEIVER_ADDR_FULL;
        extern const char* NODE_ADDR;
        extern const char* NODE_ADDR_FULL;
        extern const char* COMMAND;
        extern const char* NODE;
        extern const char* WALLET;
        extern const char* LISTEN;
        extern const char* TREASURY;
        extern const char* TREASURY_BLOCK;
        extern const char* INIT;
        extern const char* SEND;
        extern const char* INFO;
        extern const char* TX_HISTORY;
        extern const char* WALLET_SEED;
        extern const char* FEE;
        extern const char* FEE_FULL;
        extern const char* RECEIVE;
 // ui
        extern const char* WALLET_ADDR;
    }

    po::options_description createOptionsDescription();

    po::variables_map getOptions(int argc, char* argv[], const char* configFile, const po::options_description& options);
}