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

#include "options.h"

#include "boost/lexical_cast.hpp"
#include "core/block_crypt.h"
#include "core/ecc.h"
#include "utility/string_helpers.h"
#include "utility/helpers.h"
#include "wallet/secstring.h"
#include "mnemonic/mnemonic.h"

using namespace std;
using namespace ECC;

namespace beam
{
    namespace cli
    {
        const char* HELP = "help";
        const char* HELP_FULL = "help,h";
        const char* PORT = "port";
        const char* PORT_FULL = "port,p";
        const char* STRATUM_PORT = "stratum_port";
        const char* STRATUM_SECRETS_PATH = "stratum_secrets_path";
        const char* STRATUM_USE_TLS = "stratum_use_tls";
        const char* STORAGE = "storage";
        const char* WALLET_STORAGE = "wallet_path";
        const char* MINING_THREADS = "mining_threads";
        const char* VERIFICATION_THREADS = "verification_threads";
        const char* NONCEPREFIX_DIGITS = "nonceprefix_digits";
        const char* NODE_PEER = "peer";
        const char* PASS = "pass";
        const char* BTC_PASS = "btc_pass";
        const char* BTC_USER_NAME = "btc_user";
        const char* LTC_PASS = "ltc_pass";
        const char* LTC_USER_NAME = "ltc_user";
        const char* AMOUNT = "amount";
        const char* AMOUNT_FULL = "amount,a";
        const char* RECEIVER_ADDR = "receiver_addr";
        const char* RECEIVER_ADDR_FULL = "receiver_addr,r";
        const char* NODE_ADDR = "node_addr";
        const char* NODE_ADDR_FULL = "node_addr,n";
        const char* BTC_NODE_ADDR = "btc_node_addr";
        const char* LTC_NODE_ADDR = "ltc_node_addr";
        const char* COMMAND = "command";
        const char* LISTEN = "listen";
        const char* TREASURY = "treasury";
        const char* TREASURY_BLOCK = "treasury_path";
        const char* RESYNC = "resync";
        const char* RESET_ID = "reset_id";
        const char* ERASE_ID = "erase_id";
        const char* CHECKDB = "check_db";
        const char* CRASH = "crash";
        const char* INIT = "init";
        const char* RESTORE = "restore";
        const char* EXPORT_MINER_KEY = "export_miner_key";
        const char* EXPORT_OWNER_KEY = "export_owner_key";
        const char* KEY_SUBKEY = "subkey";
        const char* KEY_OWNER = "key_owner";  // deprecated
        const char* OWNER_KEY = "owner_key";
        const char* KEY_MINE = "key_mine"; // deprecated
        const char* MINER_KEY = "miner_key";
        const char* BBS_ENABLE = "bbs_enable";
        const char* NEW_ADDRESS = "new_addr";
        const char* NEW_ADDRESS_COMMENT = "comment";
        const char* EXPIRATION_TIME = "expiration_time";
        const char* SEND = "send";
        const char* INFO = "info";
        const char* TX_HISTORY = "tx_history";
        const char* CANCEL_TX = "cancel_tx";
        const char* DELETE_TX = "delete_tx";
        const char* TX_DETAILS = "tx_details";
        const char* PAYMENT_PROOF_EXPORT = "payment_proof_export";
        const char* PAYMENT_PROOF_VERIFY = "payment_proof_verify";
        const char* PAYMENT_PROOF_DATA = "payment_proof";
        const char* PAYMENT_PROOF_REQUIRED = "payment_proof_required";
        const char* TX_ID = "tx_id";
        const char* SEED_PHRASE = "seed_phrase";
        const char* GENERATE_PHRASE = "generate_phrase";
        const char* FEE = "fee";
        const char* FEE_FULL = "fee,f";
        const char* LOG_LEVEL = "log_level";
        const char* FILE_LOG_LEVEL = "file_log_level";
        const char* LOG_INFO = "info";
        const char* LOG_DEBUG = "debug";
        const char* LOG_VERBOSE = "verbose";
        const char* LOG_CLEANUP_DAYS = "log_cleanup_days";
        const char* LOG_UTXOS = "log_utxos";
        const char* VERSION = "version";
        const char* VERSION_FULL = "version,v";
        const char* GIT_COMMIT_HASH = "git_commit_hash";
        const char* WALLET_ADDR = "address";
        const char* CHANGE_ADDRESS_EXPIRATION = "change_address_expiration";
        const char* WALLET_ADDRESS_LIST = "address_list";
        const char* WALLET_RESCAN = "rescan";
        const char* UTXO = "utxo";
        const char* EXPORT_DATA = "export_data";
        const char* IMPORT_DATA = "import_data";
        const char* IMPORT_EXPORT_PATH = "file_location";
        const char* IP_WHITELIST = "ip_whitelist";
        const char* HORIZON_HI = "horizon_hi";
        const char* HORIZON_LO = "horizon_lo";
		const char* GENERATE_RECOVERY_PATH = "generate_recovery";
		const char* RECOVERY_AUTO_PATH = "recovery_auto_path";
		const char* RECOVERY_AUTO_PERIOD = "recovery_auto_period";
        const char* COLD_WALLET = "cold_wallet";
        const char* SWAP_INIT = "swap_init";
        const char* SWAP_LISTEN = "swap_listen";
        const char* SWAP_AMOUNT = "swap_amount";
        const char* SWAP_FEERATE = "swap_feerate";
        const char* SWAP_COIN = "swap_coin";
        const char* SWAP_NETWORK = "swap_network";
        const char* SWAP_BEAM_SIDE = "swap_beam_side";
        const char* SWAP_TX_HISTORY = "swap_tx_history";
        const char* NODE_POLL_PERIOD = "node_poll_period";

        // wallet api
        const char* API_USE_HTTP = "use_http";
        const char* API_USE_TLS = "use_tls";
        const char* API_TLS_CERT = "tls_cert";
        const char* API_TLS_KEY = "tls_key";
        const char* API_USE_ACL= "use_acl";
        const char* API_ACL_PATH = "acl_path";

        // treasury
        const char* TR_OPCODE = "tr_op";
        const char* TR_WID = "tr_wid";
        const char* TR_PERC = "tr_pecents";
        const char* TR_PERC_TOTAL = "tr_pecents_total";
        const char* TR_COMMENT = "tr_comment";
        const char* TR_M = "tr_M";
        const char* TR_N = "tr_N";
        // ui
        const char* APPDATA_PATH = "appdata";
        // Defaults
        const Amount kMinimumFee = 100;
    }


    template <typename T> struct TypeCvt {

        static const T& get(const T& x) {
            return x;
        }

        static const T& get(const Difficulty& x) {
            return x.m_Packed;
        }
    };

    pair<po::options_description, po::options_description> createOptionsDescription(int flags)
    {
        po::options_description general_options("General options");
        general_options.add_options()
            (cli::HELP_FULL, "list of all options")
            (cli::LOG_LEVEL, po::value<string>(), "log level [info|debug|verbose]")
            (cli::FILE_LOG_LEVEL, po::value<string>(), "file log level [info|debug|verbose]")
            (cli::LOG_CLEANUP_DAYS, po::value<uint32_t>()->default_value(5), "old logfiles cleanup period(days)")
            (cli::VERSION_FULL, "return project version")
            (cli::GIT_COMMIT_HASH, "return commit hash");

        po::options_description node_options("Node options");
        node_options.add_options()
            (cli::PORT_FULL, po::value<uint16_t>()->default_value(10000), "port to start the server on")
            (cli::STORAGE, po::value<string>()->default_value("node.db"), "node storage path")
            //(cli::MINING_THREADS, po::value<uint32_t>()->default_value(0), "number of mining threads(there is no mining if 0)")

            (cli::VERIFICATION_THREADS, po::value<int>()->default_value(-1), "number of threads for cryptographic verifications (0 = single thread, -1 = auto)")
            (cli::NONCEPREFIX_DIGITS, po::value<unsigned>()->default_value(0), "number of hex digits for nonce prefix for stratum client (0..6)")
            (cli::NODE_PEER, po::value<vector<string>>()->multitoken(), "nodes to connect to")
            (cli::STRATUM_PORT, po::value<uint16_t>()->default_value(0), "port to start stratum server on")
            (cli::STRATUM_SECRETS_PATH, po::value<string>()->default_value("."), "path to stratum server api keys file, and tls certificate and private key")
            (cli::STRATUM_USE_TLS, po::value<bool>()->default_value(true), "enable TLS on startum server")
            (cli::RESYNC, po::value<bool>()->default_value(false), "Enforce re-synchronization (soft reset)")
            (cli::RESET_ID, po::value<bool>()->default_value(false), "Reset self ID (used for network authentication). Must do if the node is cloned")
            (cli::ERASE_ID, po::value<bool>()->default_value(false), "Reset self ID (used for network authentication) and stop before re-creating the new one.")
            (cli::CHECKDB, po::value<bool>()->default_value(false), "DB integrity check and compact (vacuum)")
            (cli::BBS_ENABLE, po::value<bool>()->default_value(true), "Enable SBBS messaging")
            (cli::CRASH, po::value<int>()->default_value(0), "Induce crash (test proper handling)")
            (cli::OWNER_KEY, po::value<string>(), "Owner viewer key")
            (cli::KEY_OWNER, po::value<string>(), "Owner viewer key (deprecated)")
            (cli::MINER_KEY, po::value<string>(), "Standalone miner key")
            (cli::KEY_MINE, po::value<string>(), "Standalone miner key (deprecated)")
            (cli::PASS, po::value<string>(), "password for keys")
            (cli::LOG_UTXOS, po::value<bool>()->default_value(false), "Log recovered UTXOs (make sure the log file is not exposed)")
            (cli::HORIZON_HI, po::value<Height>()->default_value(MaxHeight), "spent TXO Hi-Horizon")
            (cli::HORIZON_LO, po::value<Height>()->default_value(MaxHeight), "spent TXO Lo-Horizon")
			(cli::GENERATE_RECOVERY_PATH, po::value<string>(), "Recovery file to generate immediately after start")
			(cli::RECOVERY_AUTO_PATH, po::value<string>(), "path and file prefix for recovery auto-generation")
			(cli::RECOVERY_AUTO_PERIOD, po::value<uint32_t>()->default_value(30), "period (in blocks) for recovery auto-generation")
            ;

        po::options_description node_treasury_options("Node treasury options");
        node_treasury_options.add_options()
            (cli::TREASURY_BLOCK, po::value<string>()->default_value("treasury.mw"), "Block pack to import treasury from");

        po::options_description wallet_options("Wallet options");
        wallet_options.add_options()
            (cli::PASS, po::value<string>(), "password for the wallet")
            (cli::SEED_PHRASE, po::value<string>(), "phrase to generate secret key according to BIP-39.")
            (cli::AMOUNT_FULL, po::value<Positive<double>>(), "amount to send (in Beams, 1 Beam = 100,000,000 groth)")
            (cli::FEE_FULL, po::value<Nonnegative<Amount>>()->default_value(Nonnegative<Amount>(cli::kMinimumFee)), "fee (in Groth, 100,000,000 groth = 1 Beam)")
            (cli::RECEIVER_ADDR_FULL, po::value<string>(), "address of receiver")
            (cli::NODE_ADDR_FULL, po::value<string>(), "address of node")
            (cli::WALLET_STORAGE, po::value<string>()->default_value("wallet.db"), "path to wallet file")
            (cli::TX_HISTORY, "print transacrions' history in info command")
            (cli::LISTEN, "start listen after new_addr command")
            (cli::TX_ID, po::value<string>()->default_value(""), "tx id")
            (cli::NEW_ADDRESS_COMMENT, po::value<string>()->default_value(""), "comment for new own address")
            (cli::EXPIRATION_TIME, po::value<string>()->default_value("24h"), "expiration time for new own address [24h|never]")
            (cli::GENERATE_PHRASE, "command to generate phrases which will be used to create a secret according to BIP-39")
            (cli::KEY_SUBKEY, po::value<Nonnegative<uint32_t>>()->default_value(Nonnegative<uint32_t>(0)), "Child key index.")
            (cli::WALLET_ADDR, po::value<string>()->default_value("*"), "wallet address")
            (cli::PAYMENT_PROOF_DATA, po::value<string>(), "payment proof data to verify")
            (cli::PAYMENT_PROOF_REQUIRED, po::value<bool>(), "Set to disallow outgoing payments if the receiver doesn't supports the payment proof (older wallets)")
            (cli::UTXO, po::value<vector<string>>()->multitoken(), "preselected utxos to transfer")
            (cli::IMPORT_EXPORT_PATH, po::value<string>()->default_value("export.dat"), "path to import or export data (import_data|export_data)")
            (cli::COLD_WALLET, "used to init cold wallet")
            (cli::COMMAND, po::value<string>(), "command to execute [new_addr|send|listen|init|restore|info|export_miner_key|export_owner_key|generate_phrase|change_address_expiration|address_list|rescan|export_data|import_data|tx_details|payment_proof_export|payment_proof_verify|utxo|cancel_tx|delete_tx|swap_init|swap_listen]")
            (cli::NODE_POLL_PERIOD, po::value<Nonnegative<uint32_t>>()->default_value(Nonnegative<uint32_t>(0)), "Node poll period in milliseconds. Set to 0 to keep connection. Anyway poll period would be no less than the expected rate of blocks if it is less then it will be rounded up to block rate value.");

        po::options_description wallet_treasury_options("Wallet treasury options");
        wallet_treasury_options.add_options()
            (cli::TR_OPCODE, po::value<uint32_t>()->default_value(0), "treasury operation: 0=print ID, 1=plan, 2=response, 3=import, 4=generate, 5=print")
            (cli::TR_WID, po::value<std::string>(), "treasury WalletID")
            (cli::TR_PERC, po::value<double>(), "treasury percent of the total emission, designated to this WalletID")
            (cli::TR_PERC_TOTAL, po::value<double>(), "Total treasury percent of the total emission")
            (cli::TR_M, po::value<uint32_t>()->default_value(0), "naggle index")
            (cli::TR_N, po::value<uint32_t>()->default_value(1), "naggle count")
            (cli::TR_COMMENT, po::value<std::string>(), "treasury custom message");

        po::options_description uioptions("UI options");
        uioptions.add_options()
            (cli::WALLET_ADDR, po::value<vector<string>>()->multitoken())
            (cli::APPDATA_PATH, po::value<string>());

        po::options_description swap_options("Atomic swap options");
        swap_options.add_options()
            (cli::BTC_NODE_ADDR, po::value<string>(), "address of bitcoin node")
            (cli::BTC_USER_NAME, po::value<string>(), "user name for the bitcoin node")
            (cli::BTC_PASS, po::value<string>(), "password for the bitcoin node")
            (cli::LTC_NODE_ADDR, po::value<string>(), "address of litecoin node")
            (cli::LTC_USER_NAME, po::value<string>(), "user name for the litecoin node")
            (cli::LTC_PASS, po::value<string>(), "password for the litecoin node")
            (cli::SWAP_COIN, po::value<string>(), "swap coin(btc, ltc, qtum)")
            (cli::SWAP_AMOUNT, po::value<Positive<Amount>>(), "swap amount in the smallest unit of the coin")
            (cli::SWAP_FEERATE, po::value<Positive<Amount>>(), "The specific feerate you are willing to pay(the smallest unit of the coin per KB)")
            (cli::SWAP_NETWORK, po::value<string>(), "type of second side network(mainnet, testnet)")
            (cli::SWAP_BEAM_SIDE, "Should be set by Beam owner")
            (cli::SWAP_TX_HISTORY, "show swap transactions history in info command");

        po::options_description options{ "Allowed options" };
        po::options_description visible_options{ "Allowed options" };
        if (flags & GENERAL_OPTIONS)
        {
            options.add(general_options);
            visible_options.add(general_options);
        }
        if (flags & NODE_OPTIONS)
        {
            options.add(node_options);
            options.add(node_treasury_options);
            visible_options.add(node_options);
        }
        if (flags & WALLET_OPTIONS)
        {
            options.add(wallet_options);
            options.add(wallet_treasury_options);
            options.add(swap_options);
            visible_options.add(wallet_options);
            visible_options.add(swap_options);
        }
        if (flags & UI_OPTIONS)
        {
            options.add(uioptions);
            visible_options.add(uioptions);
        }

        po::options_description rules_options = createRulesOptionsDescription();
        options.add(rules_options);
        visible_options.add(rules_options);
        return { options, visible_options };
    }

    po::options_description createRulesOptionsDescription()
    {
        #define RulesParams(macro) \
            macro(Amount, Emission.Value0, "initial coinbase emission in a single block") \
            macro(Amount, Emission.Drop0, "height of the last block that still has the initial emission, the drop is starting from the next block") \
            macro(Amount, Emission.Drop1, "Each such a cycle there's a new drop") \
            macro(Height, Maturity.Coinbase, "num of blocks before coinbase UTXO can be spent") \
            macro(Height, Maturity.Std, "num of blocks before non-coinbase UTXO can be spent") \
            macro(size_t, MaxBodySize, "Max block body size [bytes]") \
            macro(uint32_t, DA.Target_s, "Desired rate of generated blocks [seconds]") \
            macro(uint32_t, DA.MaxAhead_s, "Block timestamp tolerance [seconds]") \
            macro(uint32_t, DA.WindowWork, "num of blocks in the window for the mining difficulty adjustment") \
            macro(uint32_t, DA.WindowMedian0, "How many blocks are considered in calculating the timestamp median") \
            macro(uint32_t, DA.WindowMedian1, "Num of blocks taken at both endings of WindowWork, to pick medians") \
            macro(uint32_t, DA.Difficulty0, "Initial difficulty") \
            macro(bool, AllowPublicUtxos, "set to allow regular (non-coinbase) UTXO to have non-confidential signature") \
            macro(bool, FakePoW, "Don't verify PoW. Mining is simulated by the timer. For tests only")

        #define THE_MACRO(type, name, comment) (#name, po::value<type>()->default_value(TypeCvt<type>::get(Rules::get().name)), comment)

            po::options_description rules_options("Rules configuration");
            rules_options.add_options() RulesParams(THE_MACRO);

        #undef THE_MACRO

        return rules_options;
    }

    po::variables_map getOptions(int argc, char* argv[], const char* configFile, const po::options_description& options, bool walletOptions)
    {
        po::variables_map vm;
        po::positional_options_description positional;
        po::command_line_parser parser(argc, argv);
        parser.options(options);
        parser.style(po::command_line_style::default_style ^ po::command_line_style::allow_guessing);
        if (walletOptions)
        {
            positional.add(cli::COMMAND, 1);
            parser.positional(positional);
        }
        po::store(parser.run(), vm); // value stored first is preferred

        {
            std::ifstream cfg(configFile);

            if (cfg)
            {
                po::store(po::parse_config_file(cfg, options), vm);
            }
        }

        getRulesOptions(vm);

        return vm;
    }

    void getRulesOptions(po::variables_map& vm)
    {
        #define THE_MACRO(type, name, comment) Rules::get().name = vm[#name].as<type>();
                RulesParams(THE_MACRO);
        #undef THE_MACRO
    }

    int getLogLevel(const std::string &dstLog, const po::variables_map& vm, int defaultValue)
    {
        const map<std::string, int> logLevels
        {
            { cli::LOG_DEBUG, LOG_LEVEL_DEBUG },
            { cli::INFO, LOG_LEVEL_INFO },
            { cli::LOG_VERBOSE, LOG_LEVEL_VERBOSE }
        };

        if (vm.count(dstLog))
        {
            auto level = vm[dstLog].as<string>();
            if (auto it = logLevels.find(level); it != logLevels.end())
            {
                return it->second;
            }
        }

        return defaultValue;
    }

    vector<string> getCfgPeers(const po::variables_map& vm)
    {
        vector<string> peers;

        if (vm.count(cli::NODE_PEER))
        {
            auto tempPeers = vm[cli::NODE_PEER].as<vector<string>>();

            for (const auto& peer : tempPeers)
            {
                auto csv = string_helpers::split(peer, ',');

                peers.insert(peers.end(), csv.begin(), csv.end());
            }
        }

        return peers;
    }

    namespace
    {
        bool read_secret_impl(SecString& pass, const char* prompt, const char* optionName, const po::variables_map& vm)
        {
            if (vm.count(optionName)) {
                const std::string& s = vm[optionName].as<std::string>();
                size_t len = s.size();
                if (len > SecString::MAX_SIZE) len = SecString::MAX_SIZE;
                pass.assign(s.data(), len);
            }
            else {
                read_password(prompt, pass, false);
            }

            if (pass.empty()) {
                return false;
            }
            return true;
        }
    }

    bool read_wallet_pass(SecString& pass, const po::variables_map& vm)
    {
        return read_secret_impl(pass, "Enter password: ", cli::PASS, vm);
    }

    bool confirm_wallet_pass(const SecString& pass)
    {
        SecString passConfirm;
        read_password("Confirm password: ", passConfirm, false);
        return passConfirm.hash().V == pass.hash().V;
    }

    bool read_btc_pass(SecString& pass, po::variables_map& vm)
    {
        return read_secret_impl(pass, "Enter password of bitcoin node: ", cli::BTC_PASS, vm);
    }
}
