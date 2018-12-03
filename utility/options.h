// Copyright 2018 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <boost/program_options.hpp>
#include "core/ecc.h"
#include "logger.h"

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
        extern const char* STRATUM_PORT;
        extern const char* STRATUM_SECRETS_PATH;
        extern const char* STORAGE;
        extern const char* WALLET_STORAGE;
        extern const char* HISTORY;
        extern const char* TEMP;
        extern const char* IMPORT;
        extern const char* MINING_THREADS;
        extern const char* VERIFICATION_THREADS;
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
		extern const char* RESYNC;
		extern const char* CRASH;
		extern const char* INIT;
        extern const char* NEW_ADDRESS;
        extern const char* CANCEL_TX;
        extern const char* SEND;
        extern const char* INFO;
        extern const char* NEW_ADDRESS_LABEL;
        extern const char* TX_HISTORY;
        extern const char* TX_ID;
        extern const char* WALLET_SEED;
        extern const char* WALLET_PHRASES;
        extern const char* FEE;
        extern const char* FEE_FULL;
        extern const char* RECEIVE;
        extern const char* LOG_LEVEL;
        extern const char* FILE_LOG_LEVEL;
        extern const char* LOG_INFO;
        extern const char* LOG_DEBUG;
        extern const char* LOG_VERBOSE;
        extern const char* VERSION;
        extern const char* VERSION_FULL;
        extern const char* GIT_COMMIT_HASH;
#if defined(BEAM_USE_GPU)
        extern const char* MINER_TYPE;
#endif
 // treasury
        extern const char* TR_OPCODE;
        extern const char* TR_WID;
        extern const char* TR_PERC;
		extern const char* TR_COMMENT;
		// ui
        extern const char* WALLET_ADDR;
		extern const char* APPDATA_PATH;
    }

    enum OptionsFlag : int
    {
        GENERAL_OPTIONS = 1 << 0,
        NODE_OPTIONS    = 1 << 1,
        WALLET_OPTIONS  = 1 << 2,
        UI_OPTIONS      = 1 << 3,

        ALL_OPTIONS     = GENERAL_OPTIONS | NODE_OPTIONS | WALLET_OPTIONS | UI_OPTIONS
    };

    po::options_description createOptionsDescription(int flags = ALL_OPTIONS);

    po::variables_map getOptions(int argc, char* argv[], const char* configFile, const po::options_description& options);

    int getLogLevel(const std::string &dstLog, const po::variables_map& vm, int defaultValue = LOG_LEVEL_DEBUG);

	std::vector<std::string> getCfgPeers(const po::variables_map& vm);

    class SecString;

    bool read_wallet_seed(ECC::NoLeak<ECC::uintBig>& walletSeed, po::variables_map& vm);

    bool read_wallet_pass(SecString& pass, po::variables_map& vm);
}
