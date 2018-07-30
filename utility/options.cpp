#include "options.h"
#include "core/common.h"

using namespace std;

namespace beam
{
    namespace cli
    {
        const char* HELP = "help";
        const char* HELP_FULL = "help,h";
        const char* MODE = "mode";
        const char* PORT = "port";
        const char* PORT_FULL = "port,p";
        const char* STORAGE = "storage";
        const char* WALLET_STORAGE = "wallet_path";
        const char* HISTORY = "history_dir";
        const char* TEMP = "temp_dir";
        const char* IMPORT = "import";
        const char* MINING_THREADS = "mining_threads";
        const char* VERIFICATION_THREADS = "verification_threads";
        const char* MINER_ID = "miner_id";
        const char* NODE_PEER = "peer";
        const char* PASS = "pass";
        const char* AMOUNT = "amount";
        const char* AMOUNT_FULL = "amount,a";
        const char* RECEIVER_ADDR = "receiver_addr";
        const char* RECEIVER_ADDR_FULL = "receiver_addr,r";
        const char* NODE_ADDR = "node_addr";
        const char* NODE_ADDR_FULL = "node_addr,n";
        const char* COMMAND = "command";
        const char* NODE = "node";
        const char* WALLET = "wallet";
        const char* LISTEN = "listen";
        const char* TREASURY = "treasury";
        const char* TREASURY_BLOCK = "treasury_path";
        const char* INIT = "init";
        const char* SEND = "send";
        const char* INFO = "info";
        const char* TX_HISTORY = "tx_history";
        const char* WALLET_SEED = "wallet_seed";
        const char* FEE = "fee";
        const char* FEE_FULL = "fee,f";
        const char* RECEIVE = "receive";
        // ui
        const char* WALLET_ADDR = "addr";
    }

    po::options_description createOptionsDescription()
    {
#ifdef WIN32
        char szLocalDir[] = ".\\";
        char szTempDir[MAX_PATH] = { 0 };
        GetTempPath(_countof(szTempDir), szTempDir);

#else // WIN32
        char szLocalDir[] = "./";
        char szTempDir[] = "/tmp/";
#endif // WIN32

        po::options_description general_options("General options");
        general_options.add_options()
            (cli::HELP_FULL, "list of all options")
            (cli::MODE, po::value<string>()->required(), "mode to execute [node|wallet]")
            (cli::PORT_FULL, po::value<uint16_t>()->default_value(10000), "port to start the server on")
            (cli::WALLET_SEED, po::value<string>(), "secret key generation seed");

        po::options_description node_options("Node options");
        node_options.add_options()
            (cli::STORAGE, po::value<string>()->default_value("node.db"), "node storage path")
            (cli::HISTORY, po::value<string>()->default_value(szLocalDir), "directory for compressed history")
            (cli::TEMP, po::value<string>()->default_value(szTempDir), "temp directory for compressed history, must be on the same volume")
            (cli::MINING_THREADS, po::value<uint32_t>()->default_value(0), "number of mining threads(there is no mining if 0)")
            (cli::VERIFICATION_THREADS, po::value<int>()->default_value(-1), "number of threads for cryptographic verifications (0 = single thread, -1 = auto)")
            (cli::MINER_ID, po::value<uint32_t>()->default_value(0), "seed for miner nonce generation")
            (cli::NODE_PEER, po::value<vector<string>>()->multitoken(), "nodes to connect to")
            (cli::IMPORT, po::value<Height>()->default_value(0), "Specify the blockchain height to import. The compressed history is asumed to be downloaded the the specified directory")
            ;

        po::options_description wallet_options("Wallet options");
        wallet_options.add_options()
            (cli::PASS, po::value<string>()->default_value(""), "password for the wallet")
            (cli::AMOUNT_FULL, po::value<double>(), "amount to send (in Beams, 1 Beam = 1000000 chattle)")
            (cli::FEE_FULL, po::value<double>()->default_value(0), "fee (in Beams, 1 Beam = 1000000 chattle)")
            (cli::RECEIVER_ADDR_FULL, po::value<string>(), "address of receiver")
            (cli::NODE_ADDR_FULL, po::value<string>(), "address of node")
            (cli::TREASURY_BLOCK, po::value<string>()->default_value("treasury.mw"), "Block to create/append treasury to")
            (cli::WALLET_STORAGE, po::value<string>()->default_value("wallet.db"), "path to wallet file")
            (cli::TX_HISTORY, "print transacrions' history in info command")
            (cli::COMMAND, po::value<string>(), "command to execute [send|receive|listen|init|info|treasury]");

        po::options_description uioptions("UI options");
        uioptions.add_options()
            (cli::WALLET_ADDR, po::value<vector<string>>()->multitoken());

#define RulesParams(macro) \
	macro(Amount, CoinbaseEmission, "coinbase emission in a single block") \
	macro(Height, MaturityCoinbase, "num of blocks before coinbase UTXO can be spent") \
	macro(Height, MaturityStd, "num of blocks before non-coinbase UTXO can be spent") \
	macro(size_t, MaxBodySize, "Max block body size [bytes]") \
	macro(uint32_t, DesiredRate_s, "Desired rate of generated blocks [seconds]") \
	macro(uint32_t, DifficultyReviewCycle, "num of blocks after which the mining difficulty can be adjusted") \
	macro(uint32_t, MaxDifficultyChange, "Max difficulty change after each cycle (each step is roughly x2 complexity)") \
	macro(uint32_t, TimestampAheadThreshold_s, "Block timestamp tolerance [seconds]") \
	macro(uint32_t, WindowForMedian, "How many blocks are considered in calculating the timestamp median") \
	macro(bool, FakePoW, "Don't verify PoW. Mining is simulated by the timer. For tests only")

#define THE_MACRO(type, name, comment) (#name, po::value<type>()->default_value(Rules::get().name), comment)

        po::options_description rules_options("Rules configuration");
        rules_options.add_options() RulesParams(THE_MACRO);

#undef THE_MACRO

        po::options_description options{ "Allowed options" };
        options
            .add(general_options)
            .add(node_options)
            .add(wallet_options)
            .add(uioptions)
            .add(rules_options);
        return options;
    }

    po::variables_map getOptions(int argc, char* argv[], const char* configFile, const po::options_description& options)
    {
        po::positional_options_description pos;
        pos.add(cli::MODE, 1);

        po::variables_map vm;

        po::store(po::command_line_parser(argc, argv) // value stored first is preferred
            .options(options)
            .positional(pos)
            .run(), vm);

        {
            std::ifstream cfg(configFile);

            if (cfg)
            {
                po::store(po::parse_config_file(cfg, options), vm);
            }
        }


        #define THE_MACRO(type, name, comment) Rules::get().name = vm[#name].as<type>();
        		RulesParams(THE_MACRO);
        #undef THE_MACRO

        return vm;
    }
}