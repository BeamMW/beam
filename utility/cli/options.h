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
#include "utility/logger.h"
#include "wallet/core/secstring.h"

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
        extern const char* STRATUM_USE_TLS;
        extern const char* STORAGE;
        extern const char* WALLET_STORAGE;
        extern const char* MINING_THREADS;
        extern const char* VERIFICATION_THREADS;
        extern const char* NONCEPREFIX_DIGITS;
        extern const char* NODE_PEER;
        extern const char* PASS;
        extern const char* SET_SWAP_SETTINGS;
        extern const char* ACTIVE_CONNECTION;
        extern const char* SWAP_WALLET_PASS;
        extern const char* SWAP_WALLET_USER;
        extern const char* ALTCOIN_SETTINGS_RESET;
        extern const char* SHOW_SWAP_SETTINGS;
        extern const char* ELECTRUM_SEED;
        extern const char* GENERATE_ELECTRUM_SEED;
        extern const char* SELECT_SERVER_AUTOMATICALLY;
        extern const char* ELECTRUM_ADDR;
        extern const char* AMOUNT;
        extern const char* AMOUNT_FULL;
        extern const char* RECEIVER_ADDR;
        extern const char* RECEIVER_ADDR_FULL;
        extern const char* NODE_ADDR;
        extern const char* NODE_ADDR_FULL;
        extern const char* SWAP_WALLET_ADDR;
        extern const char* COMMAND;
        extern const char* NODE;
        extern const char* WALLET;
        extern const char* LISTEN;
        extern const char* TREASURY;
        extern const char* TREASURY_BLOCK;
        extern const char* RESET_ID;
        extern const char* ERASE_ID;
        extern const char* PRINT_TXO;
        extern const char* CHECKDB;
        extern const char* VACUUM;
        extern const char* CRASH;
        extern const char* INIT;
        extern const char* RESTORE;
        extern const char* EXPORT_MINER_KEY;
        extern const char* EXPORT_OWNER_KEY;
        extern const char* KEY_SUBKEY;
        extern const char* KEY_OWNER;  // deprecated
        extern const char* OWNER_KEY;
        extern const char* KEY_MINE;  // deprecated
        extern const char* MINER_KEY;
        extern const char* BBS_ENABLE;
        extern const char* NEW_ADDRESS;
        extern const char* CANCEL_TX;
        extern const char* DELETE_TX;
        extern const char* TX_DETAILS;
        extern const char* PAYMENT_PROOF_EXPORT;
        extern const char* PAYMENT_PROOF_VERIFY;
        extern const char* PAYMENT_PROOF_DATA;
        extern const char* PAYMENT_PROOF_REQUIRED;
        extern const char* SEND;
        extern const char* INFO;
        extern const char* NEW_ADDRESS_COMMENT;
        extern const char* EXPIRATION_TIME;
        extern const char* TX_HISTORY;
        extern const char* TX_ID;
        extern const char* SEED_PHRASE;
        extern const char* IGNORE_DICTIONARY;
        extern const char* GENERATE_PHRASE;
        extern const char* FEE;
        extern const char* FEE_FULL;
        extern const char* LOG_LEVEL;
        extern const char* FILE_LOG_LEVEL;
        extern const char* LOG_INFO;
        extern const char* LOG_DEBUG;
        extern const char* LOG_VERBOSE;
        extern const char* LOG_CLEANUP_DAYS;
        extern const char* LOG_UTXOS;
        extern const char* VERSION;
        extern const char* VERSION_FULL;
        extern const char* GIT_COMMIT_HASH;
        extern const char* WALLET_ADDR;
        extern const char* CHANGE_ADDRESS_EXPIRATION;
        extern const char* WALLET_ADDRESS_LIST;
        extern const char* WALLET_RESCAN;
        extern const char* UTXO;
        extern const char* EXPORT_ADDRESSES;
        extern const char* EXPORT_DATA;
        extern const char* IMPORT_ADDRESSES;
        extern const char* IMPORT_DATA;
        extern const char* IMPORT_EXPORT_PATH;
        extern const char* IP_WHITELIST;
		extern const char* FAST_SYNC;
		extern const char* GENERATE_RECOVERY_PATH;
		extern const char* RECOVERY_AUTO_PATH;
		extern const char* RECOVERY_AUTO_PERIOD;
        extern const char* COLD_WALLET;
        extern const char* SWAP_INIT;
        extern const char* SWAP_ACCEPT;
        extern const char* SWAP_TOKEN;
        extern const char* SWAP_AMOUNT;
        extern const char* SWAP_FEERATE;
        extern const char* SWAP_COIN;
        extern const char* SWAP_BEAM_SIDE;
        extern const char* SWAP_TX_HISTORY;
        extern const char* NODE_POLL_PERIOD;
        extern const char* PROXY_USE;
        extern const char* PROXY_ADDRESS;
        // values
        extern const char* EXPIRATION_TIME_24H;
        extern const char* EXPIRATION_TIME_NEVER;
        extern const char* EXPIRATION_TIME_NOW;
        // laser
#ifdef BEAM_LASER_SUPPORT
        extern const char* LASER;
        extern const char* LASER_OPEN;
        extern const char* LASER_TRANSFER;
        extern const char* LASER_WAIT;
        extern const char* LASER_SERVE;
        extern const char* LASER_LIST;
        extern const char* LASER_CLOSE;
        extern const char* LASER_DELETE;
        extern const char* LASER_AMOUNT_MY;
        extern const char* LASER_AMOUNT_TARGET;
        extern const char* LASER_TARGET_ADDR;
        extern const char* LASER_FEE;
        extern const char* LASER_LOCK_TIME;
        extern const char* LASER_CHANNEL_ID;
        extern const char* LASER_ALL;
        extern const char* LASER_CLOSE_GRACEFUL;
#endif  // BEAM_LASER_SUPPORT

        // wallet api
        extern const char* API_USE_HTTP;
        extern const char* API_USE_TLS;
        extern const char* API_TLS_CERT;
        extern const char* API_TLS_KEY;
        extern const char* API_USE_ACL;
        extern const char* API_ACL_PATH;

        // treasury
        extern const char* TR_OPCODE;
        extern const char* TR_WID;
        extern const char* TR_PERC;
        extern const char* TR_PERC_TOTAL;
        extern const char* TR_COMMENT;
        extern const char* TR_M;
        extern const char* TR_N;

        // ui
        extern const char* APPDATA_PATH;

        // assets
        extern const char* ASSET_ISSUE;
        extern const char* ASSET_CONSUME;
        extern const char* ASSET_INDEX;
        extern const char* ASSET_ID;

        // Defaults that should be accessible outside
        extern const Amount kMinimumFee;
    }

    enum OptionsFlag : int
    {
        GENERAL_OPTIONS = 1 << 0,
        NODE_OPTIONS    = 1 << 1,
        WALLET_OPTIONS  = 1 << 2,
        UI_OPTIONS      = 1 << 3,

        ALL_OPTIONS     = GENERAL_OPTIONS | NODE_OPTIONS | WALLET_OPTIONS | UI_OPTIONS
    };

    std::pair<po::options_description, po::options_description> createOptionsDescription(int flags = ALL_OPTIONS);

    po::options_description createRulesOptionsDescription();

    po::variables_map getOptions(int argc, char* argv[], const char* configFile, const po::options_description& options, bool walletOptions = false);

    void getRulesOptions(po::variables_map& vm);

    int getLogLevel(const std::string &dstLog, const po::variables_map& vm, int defaultValue = LOG_LEVEL_DEBUG);

    std::vector<std::string> getCfgPeers(const po::variables_map& vm);

    class SecString;

    template <typename T>
    struct Nonnegative {
        static_assert(std::is_unsigned<T>::value, "Nonnegative<T> requires unsigned type.");

        Nonnegative() {}
        explicit Nonnegative(const T& v) : value(v) {}

        T value = 0;
    };

    template <typename T>
    struct Positive {
        static_assert(std::is_arithmetic<T>::value, "Positive<T> requires numerical type.");

        Positive() {}
        explicit Positive(const T& v) : value(v) {}

        T value = 0;
    };

    /** Class thrown when a argument of option is negative */
    class NonnegativeOptionException : public po::error_with_option_name {
    public:
        NonnegativeOptionException()
            : po::error_with_option_name("The argument for option '%canonical_option%' must be equal to or more than 0.")
        {
        }
    };

    // Class thrown when a argument of option is negative or zero
    class PositiveOptionException : public po::error_with_option_name {
    public:
        PositiveOptionException()
            : po::error_with_option_name("The argument for option '%canonical_option%' must be more than 0.")
        {
        }
    };

    template<typename T>
    std::ostream& operator<<(std::ostream& os, const Nonnegative<T>& v)
    {
        os << v.value;
        return os;
    }

    template<typename T>
    std::ostream& operator<<(std::ostream& os, const Positive<T>& v)
    {
        os << v.value;
        return os;
    }

    template <typename T>
    void validate(boost::any& v, const std::vector<std::string>& values, Nonnegative<T>*, int)
    {
        po::validators::check_first_occurrence(v);

        const std::string& s = po::validators::get_single_string(values);

        if (!s.empty() && s[0] == '-') {
            throw NonnegativeOptionException();
        }

        try
        {
            v = Nonnegative<T>(boost::lexical_cast<T>(s));
        }
        catch (const boost::bad_lexical_cast&)
        {
            throw po::invalid_option_value(s);
        }
    }

    template <typename T>
    void validate(boost::any& v, const std::vector<std::string>& values, Positive<T>*, int)
    {
        po::validators::check_first_occurrence(v);
        const std::string& s = po::validators::get_single_string(values);
        T numb;

        if (!s.empty() && s[0] == '-') {
            throw PositiveOptionException();
        }

        try
        {
            numb = boost::lexical_cast<T>(s);
        }
        catch (const boost::bad_lexical_cast&)
        {
            throw po::invalid_option_value(s);
        }

        if (numb <= 0)
            throw PositiveOptionException();

        v = Positive<T>(numb);
    }

    bool read_wallet_pass(SecString& pass, const po::variables_map& vm);
    bool confirm_wallet_pass(const SecString& pass);
}
