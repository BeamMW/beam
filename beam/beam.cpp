#include "wallet/wallet_network.h"
#include "core/common.h"

#include "node.h"
#include "wallet/wallet.h"
#include "wallet/wallet_db.h"
#include "wallet/wallet_network.h"
#include "core/ecc_native.h"
#include "core/serialization_adapters.h"
#include "utility/logger.h"
#include <iomanip>

#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <iterator>

namespace po = boost::program_options;
using namespace std;
using namespace beam;
using namespace ECC;

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
}
namespace beam
{
    std::ostream& operator<<(std::ostream& os, Coin::Status s)
    {
        stringstream ss;
        ss << "[";
        switch (s)
        {
        case Coin::Locked: ss << "Locked"; break;
        case Coin::Spent: ss << "Spent"; break;
        case Coin::Unconfirmed: ss << "Unconfirmed"; break;
        case Coin::Unspent: ss << "Unspent"; break;
        default:
            assert(false && "Unknown coin status");
        }
        ss << "]";
        string str = ss.str();
        os << str;
        int c = 13 - str.length();
        for (int i = 0; i < c; ++i) os << ' ';
        return os;
    }

    std::ostream& operator<<(std::ostream& os, KeyType keyType)
    {
        os << "[";
        switch (keyType)
        {
        case KeyType::Coinbase: os << "Coinbase"; break;
        case KeyType::Comission: os << "Commission"; break;
        case KeyType::Kernel: os << "Kernel"; break;
        case KeyType::Regular: os << "Regular"; break;
        default:
            assert(false && "Unknown key type");
        }
        os << "]";
        return os;
    }

    const char* getTxStatus(const TxDescription& tx)
    {
        static const char* Pending = "Pending";
        static const char* Sending = "Sending";
        static const char* Receiving = "Receiving";
        static const char* Cancelled = "Cancelled";
        static const char* Sent = "Sent";
        static const char* Received = "Received";
        static const char* Failed = "Failed";
        
        switch (tx.m_status)
        {
        case TxDescription::Pending: return Pending;
        case TxDescription::InProgress: return tx.m_sender ? Sending : Receiving;
        case TxDescription::Cancelled: return Cancelled;
        case TxDescription::Completed: return tx.m_sender ? Sent : Received;
        case TxDescription::Failed: return Failed;
        default:
            assert(false && "Unknown key type");
        }
        
        return "";
    }

    Amount getAvailable(beam::IKeyChain::Ptr keychain)
    {
        auto currentHeight = keychain->getCurrentHeight();
        Amount total = 0;
        keychain->visit([&total, &currentHeight](const Coin& c)->bool
        {
            Height lockHeight = c.m_height + (c.m_key_type == KeyType::Coinbase
                ? Rules::get().MaturityCoinbase
                : Rules::get().MaturityStd);

            if (c.m_status == Coin::Unspent
                && lockHeight <= currentHeight)
            {
                total += c.m_amount;
            }
            return true;
        });
        return total;
    }

    Amount getAvailableByType(beam::IKeyChain::Ptr keychain, Coin::Status status, KeyType keyType)
    {
        auto currentHeight = keychain->getCurrentHeight();
        Amount total = 0;
        keychain->visit([&total, &currentHeight, &status, &keyType](const Coin& c)->bool
        {
            Height lockHeight = c.m_height + (c.m_key_type == KeyType::Coinbase
                ? Rules::get().MaturityCoinbase
                : Rules::get().MaturityStd);

            if (c.m_status == status
             && c.m_key_type == keyType
             && lockHeight <= currentHeight)
            {
                total += c.m_amount;
            }
            return true;
        });
        return total;
    }

    Amount getTotal(beam::IKeyChain::Ptr keychain, Coin::Status status)
    {
        Amount total = 0;
        keychain->visit([&total, &status](const Coin& c)->bool
        {
            if (c.m_status == status)
            {
                total += c.m_amount;
            }
            return true;
        });
        return total;
    }

    Amount getTotalByType(beam::IKeyChain::Ptr keychain, Coin::Status status, KeyType keyType)
    {
        Amount total = 0;
        keychain->visit([&total, &status, &keyType](const Coin& c)->bool
        {
            if (c.m_status == status && c.m_key_type == keyType)
            {
                total += c.m_amount;
            }
            return true;
        });
        return total;
    }

}
namespace
{
    void printHelp(const po::options_description& options)
    {
        cout << options << std::endl;
    }

    bool ReadTreasury(std::vector<Block::Body>& vBlocks, const string& sPath)
    {
		if (sPath.empty())
			return false;

		std::FStream f;
		if (!f.Open(sPath.c_str(), true))
			return false;

		yas::binary_iarchive<std::FStream, SERIALIZE_OPTIONS> arc(f);
        arc & vBlocks;
        
		return true;
    }
}

struct TreasuryBlockGenerator
{
	std::string m_sPath;
	IKeyChain* m_pKeyChain;

	std::vector<Block::Body> m_vBlocks;
	ECC::Scalar::Native m_Offset;

	std::vector<Coin> m_Coins;
	std::vector<std::pair<Height, ECC::Scalar::Native> > m_vIncubationAndKeys;

	std::mutex m_Mutex;
	std::vector<std::thread> m_vThreads;

	Block::Body& get_WriteBlock();
	void FinishLastBlock();
	int Generate(uint32_t nCount, Height dh);
private:
	void Proceed(uint32_t i);
};

int TreasuryBlockGenerator::Generate(uint32_t nCount, Height dh)
{
	if (m_sPath.empty())
	{
		LOG_ERROR() << "Treasury block path not specified";
		return -1;
	}

	boost::filesystem::path path{ m_sPath };
	boost::filesystem::path dir = path.parent_path();
	if (!dir.empty() && !boost::filesystem::exists(dir) && !boost::filesystem::create_directory(dir))
	{
		LOG_ERROR() << "Failed to create directory: " << dir.c_str();
		return -1;
	}

	if (ReadTreasury(m_vBlocks, m_sPath))
		LOG_INFO() << "Treasury already contains " << m_vBlocks.size() << " blocks, appending.";

	if (!m_vBlocks.empty())
	{
		m_Offset = m_vBlocks.back().m_Offset;
		m_Offset = -m_Offset;
	}

	LOG_INFO() << "Generating coins...";

	m_Coins.resize(nCount);
	m_vIncubationAndKeys.resize(nCount);

	Height h = 0;

	for (uint32_t i = 0; i < nCount; i++, h += dh)
	{
		Coin& coin = m_Coins[i];
		coin.m_key_type = KeyType::Regular;
		coin.m_amount = Rules::Coin * 10;
		coin.m_status = Coin::Unconfirmed;
		coin.m_height = h + Rules::HeightGenesis;


		m_vIncubationAndKeys[i].first = h;
	}

	m_pKeyChain->store(m_Coins); // we get coin id only after store

	for (uint32_t i = 0; i < nCount; ++i)
        m_vIncubationAndKeys[i].second = m_pKeyChain->calcKey(m_Coins[i]);

	m_vThreads.resize(std::thread::hardware_concurrency());
	assert(!m_vThreads.empty());

	for (uint32_t i = 0; i < m_vThreads.size(); i++)
		m_vThreads[i] = std::thread(&TreasuryBlockGenerator::Proceed, this, i);

	for (uint32_t i = 0; i < m_vThreads.size(); i++)
		m_vThreads[i].join();

	// at least 1 kernel
	{
		Coin dummy; // not a coin actually
		dummy.m_key_type = KeyType::Kernel;
		dummy.m_status = Coin::Unconfirmed;

		ECC::Scalar::Native k = m_pKeyChain->calcKey(dummy);

		TxKernel::Ptr pKrn(new TxKernel);
		pKrn->m_Excess = ECC::Point::Native(Context::get().G * k);

		Merkle::Hash hv;
		pKrn->get_HashForSigning(hv);
		pKrn->m_Signature.Sign(hv, k);

		get_WriteBlock().m_vKernelsOutput.push_back(std::move(pKrn));
		m_Offset += k;
	}

	FinishLastBlock();

	for (auto i = 0u; i < m_vBlocks.size(); i++)
	{
		m_vBlocks[i].Sort();
		m_vBlocks[i].DeleteIntermediateOutputs();
	}

	std::FStream f;
	f.Open(m_sPath.c_str(), false, true);

	yas::binary_oarchive<std::FStream, SERIALIZE_OPTIONS> arc(f);
	arc & m_vBlocks;
	f.Flush();

/*
	for (auto i = 0; i < m_vBlocks.size(); i++)
		m_vBlocks[i].IsValid(i + 1, true);
*/

	LOG_INFO() << "Done";

	return 0;
}

void TreasuryBlockGenerator::FinishLastBlock()
{
	m_Offset = -m_Offset;
	m_vBlocks.back().m_Offset = m_Offset;
}

Block::Body& TreasuryBlockGenerator::get_WriteBlock()
{
	if (m_vBlocks.empty() || m_vBlocks.back().m_vOutputs.size() >= 1000)
	{
		if (!m_vBlocks.empty())
			FinishLastBlock();

		m_vBlocks.resize(m_vBlocks.size() + 1);
		m_vBlocks.back().ZeroInit();
		m_Offset = ECC::Zero;
	}
	return m_vBlocks.back();
}

void TreasuryBlockGenerator::Proceed(uint32_t i0)
{
	std::vector<Output::Ptr> vOut;

	for (uint32_t i = i0; i < m_Coins.size(); i += m_vThreads.size())
	{
		const Coin& coin = m_Coins[i];

		Output::Ptr pOutp(new Output);
		pOutp->m_Incubation = m_vIncubationAndKeys[i].first;

		const ECC::Scalar::Native& k = m_vIncubationAndKeys[i].second;
		pOutp->Create(k, coin.m_amount);

		vOut.push_back(std::move(pOutp));
		//offset += k;
		//subBlock.m_Subsidy += coin.m_amount;
	}

	std::unique_lock<std::mutex> scope(m_Mutex);

	uint32_t iOutp = 0;
	for (uint32_t i = i0; i < m_Coins.size(); i += m_vThreads.size(), iOutp++)
	{
		Block::Body& block = get_WriteBlock();

		block.m_vOutputs.push_back(std::move(vOut[iOutp]));
		block.m_Subsidy += m_Coins[i].m_amount;
		m_Offset += m_vIncubationAndKeys[i].second;
	}
}

#define LOG_VERBOSE_ENABLED 0


int main(int argc, char* argv[])
{
    int logLevel = LOG_LEVEL_DEBUG;
#if LOG_VERBOSE_ENABLED
    logLevel = LOG_LEVEL_VERBOSE;
#endif
    auto logger = beam::Logger::create(logLevel, logLevel);

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
        (cli::RECEIVER_ADDR_FULL, po::value<string>(), "address of receiver")
        (cli::NODE_ADDR_FULL, po::value<string>(), "address of node")
		(cli::TREASURY_BLOCK, po::value<string>()->default_value("treasury.mw"), "Block to create/append treasury to")
        (cli::WALLET_STORAGE, po::value<string>()->default_value("wallet.db"), "path to wallet file")
        (cli::TX_HISTORY, "print transacrions' history in info command")
		(cli::COMMAND, po::value<string>(), "command to execute [send|listen|init|info|treasury]");

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
		.add(rules_options);

    po::positional_options_description pos;
    pos.add(cli::MODE, 1);

    try
    {
        po::variables_map vm;

		{
			std::ifstream cfg("beam.cfg");

			if (cfg)
			{
				po::store(po::parse_config_file(cfg, options), vm);
			}
		}

        po::store(po::command_line_parser(argc, argv)
            .options(options)
            .positional(pos)
            .run(), vm);

        if (vm.count(cli::HELP))
        {
            printHelp(options);

            return 0;
        }

        po::notify(vm);

#define THE_MACRO(type, name, comment) Rules::get().name = vm[#name].as<type>();
		RulesParams(THE_MACRO);
#undef THE_MACRO

		Rules::get().UpdateChecksum();
		LOG_INFO() << "Rules signature: " << Rules::get().Checksum;

        auto port = vm[cli::PORT].as<uint16_t>();
        auto hasWalletSeed = vm.count(cli::WALLET_SEED) > 0;

        if (vm.count(cli::MODE))
        {
            io::Reactor::Ptr reactor(io::Reactor::create());
            io::Reactor::Scope scope(*reactor);
            NoLeak<uintBig> walletSeed;
            walletSeed.V = Zero;
            if (hasWalletSeed)
            {
                // TODO: use secure string here
                string seed = vm[cli::WALLET_SEED].as<string>();
                Hash::Value hv;
                Hash::Processor() << seed.c_str() >> hv;
                walletSeed.V = hv;
            }

            auto mode = vm[cli::MODE].as<string>();
            if (mode == cli::NODE)
            {
                beam::Node node;

                node.m_Cfg.m_Listen.port(port);
                node.m_Cfg.m_Listen.ip(INADDR_ANY);
                node.m_Cfg.m_sPathLocal = vm[cli::STORAGE].as<std::string>();
                node.m_Cfg.m_MiningThreads = vm[cli::MINING_THREADS].as<uint32_t>();
                node.m_Cfg.m_MinerID = vm[cli::MINER_ID].as<uint32_t>();
				node.m_Cfg.m_VerificationThreads = vm[cli::VERIFICATION_THREADS].as<int>();
                if (node.m_Cfg.m_MiningThreads > 0 && !hasWalletSeed)
                {
                    LOG_ERROR() << " wallet seed is not provided. You have pass wallet seed for mining node.";
                    return -1;
                }
                node.m_Cfg.m_WalletKey = walletSeed;

				std::vector<std::string> vPeers;

				if (vm.count(cli::NODE_PEER))
					vPeers = vm[cli::NODE_PEER].as<std::vector<std::string> >();

				node.m_Cfg.m_Connect.resize(vPeers.size());

				for (size_t i = 0; i < vPeers.size(); i++)
				{
					io::Address& addr = node.m_Cfg.m_Connect[i];
					if (!addr.resolve(vPeers[i].c_str()))
					{
						LOG_ERROR() << "unable to resolve: " << vPeers[i];
						return -1;
					}

					if (!addr.port())
					{
						if (!port)
						{
							LOG_ERROR() << "Port must be specified";
							return -1;
						}
						addr.port(port);
					}
				}

				node.m_Cfg.m_HistoryCompression.m_sPathOutput = vm[cli::HISTORY].as<string>();
				node.m_Cfg.m_HistoryCompression.m_sPathTmp = vm[cli::TEMP].as<string>();

                LOG_INFO() << "starting a node on " << node.m_Cfg.m_Listen.port() << " port...";

                if (vm.count(cli::TREASURY_BLOCK))
                {
                    string sPath = vm[cli::TREASURY_BLOCK].as<string>();
					ReadTreasury(node.m_Cfg.m_vTreasury, sPath);

					if (!node.m_Cfg.m_vTreasury.empty())
						LOG_INFO() << "Treasury blocs read: " << node.m_Cfg.m_vTreasury.size();
                }

				node.Initialize();

				Height hImport = vm[cli::IMPORT].as<Height>();
				if (hImport)
					node.ImportMacroblock(hImport);

                reactor->run();
            }
            else if (mode == cli::WALLET)
            {
                if (vm.count(cli::COMMAND))
                {
                    auto command = vm[cli::COMMAND].as<string>();
                    if (command != cli::INIT
                     && command != cli::SEND
                     && command != cli::LISTEN
                     && command != cli::TREASURY
                     && command != cli::INFO)
                    {
                        LOG_ERROR() << "unknown command: \'" << command << "\'";
                        return -1;
                    }

                    assert(vm.count(cli::WALLET_STORAGE) > 0);
                    auto walletPath = vm[cli::WALLET_STORAGE].as<string>();

                    if (!Keychain::isInitialized(walletPath) && command != cli::INIT)
                    {
                        LOG_ERROR() << "Please initialize your wallet first... \nExample: beam wallet --command=init --pass=<password to access wallet> --wallet_seed=<seed to generate secret keys>";
                        return -1;
                    }

                    LOG_INFO() << "starting a wallet...";

                    // TODO: we should use secure string
                    string pass;
                    if (vm.count(cli::PASS))
                    {
                        pass = vm[cli::PASS].as<string>();
                    }

                    if (!pass.size())
                    {
                        LOG_ERROR() << "Please, provide password for the wallet.";
                        return -1;
                    }

                    if (command == cli::INIT)
                    {
                        if (!hasWalletSeed)
                        {
                            LOG_ERROR() << "Please, provide seed phrase for the wallet.";
                            return -1;
                        }
                        auto keychain = Keychain::init(walletPath, pass, walletSeed);
                        if (keychain)
                        {
                            LOG_INFO() << "wallet successfully created...";
                            return 0;
                        }
                        else
                        {
                            LOG_ERROR() << "something went wrong, wallet not created...";
                            return -1;
                        }
                    }


                    auto keychain = Keychain::open(walletPath, pass);
                    if (!keychain)
                    {
                        LOG_ERROR() << "Wallet data unreadable, restore wallet.db from latest backup or delete it and reinitialize the wallet";
                        return -1;
                    }

                    LOG_INFO() << "wallet sucessfully opened...";

					if (command == cli::TREASURY)
					{
						TreasuryBlockGenerator tbg;
						tbg.m_sPath = vm[cli::TREASURY_BLOCK].as<std::string>();
						tbg.m_pKeyChain = keychain.get();

						// TODO: command-line parameter
						Height dh = 60 * 2; // 2 hours, 12 per day
						uint32_t nCount = 12 * 30; // 360 total. 1 month roughly

						return tbg.Generate(nCount, dh);
					}

                    if (command == cli::INFO)
                    {
                        Block::SystemState::ID stateID = {};
                        keychain->getSystemStateID(stateID);
                        cout << "____Wallet summary____\n\n"
                            << "Current height............" << stateID.m_Height << '\n'
                            << "Current state ID.........." << stateID.m_Hash << "\n\n"
                            << "Available................." << PrintableAmount(getAvailable(keychain)) << '\n'
                            << "Unconfirmed..............." << PrintableAmount(getTotal(keychain, Coin::Unconfirmed)) << '\n'
                            << "Locked...................." << PrintableAmount(getTotal(keychain, Coin::Locked)) << '\n'
                            << "Available coinbase ......." << PrintableAmount(getAvailableByType(keychain, Coin::Unspent, KeyType::Coinbase)) << '\n'
                            << "Total coinbasde..........." << PrintableAmount(getTotalByType(keychain, Coin::Unspent, KeyType::Coinbase)) << '\n'
                            << "Avaliable fee............." << PrintableAmount(getAvailableByType(keychain, Coin::Unspent, KeyType::Comission)) << '\n'
                            << "Total fee................." << PrintableAmount(getTotalByType(keychain, Coin::Unspent, KeyType::Comission)) << '\n'
                            << "Total unspent............." << PrintableAmount(getTotal(keychain, Coin::Unspent)) << "\n\n";
                             //<< "Total spent..............." << PrintableAmount(getTotal(keychain, Coin::Spent)) << "\n\n"
                        if (vm.count(cli::TX_HISTORY))
                        {
                            auto txHistory = keychain->getTxHistory();
                            if (txHistory.empty())
                            {
                                cout << "No transactions\n";
                                return 0;
                            }

                            cout << "TRANSACTIONS\n\n"
                                 << "| datetime          | amount, BEAM    | status\t|\n";
                            for (auto& tx : txHistory)
                            {
                                cout << "  " << put_time(localtime((const time_t*)(&tx.m_createTime)), "%D  %T")
                                     << setw(17) << PrintableAmount(tx.m_amount, true)
                                     << "  " << getTxStatus(tx) << '\n';
                            }
                            return 0;
                        }
                        
                        cout << "| id\t| amount(Beam)\t| amount(c)\t| height\t| maturity\t| status \t| key type\t|\n";
                        keychain->visit([](const Coin& c)->bool
                        {
                            cout << setw(8) << c.m_id
                                 << setw(16) << PrintableAmount(Rules::Coin * ((Amount)(c.m_amount / Rules::Coin)))
                                 << setw(16) << PrintableAmount(c.m_amount % Rules::Coin)
                                 << setw(16) << static_cast<int64_t>(c.m_height)
                                 << setw(16) << static_cast<int64_t>(c.m_maturity)
                                 << "  " << c.m_status
                                 << "  " << c.m_key_type << '\n';
                            return true;
                        });
                        return 0;
                    }

                    if (vm.count(cli::NODE_ADDR) == 0)
                    {
                        LOG_ERROR() << "node address should be specified";
                        return -1;
                    }

                    string nodeURI = vm[cli::NODE_ADDR].as<string>();
                    io::Address node_addr;
                    if (!node_addr.resolve(nodeURI.c_str()))
                    {
                        LOG_ERROR() << "unable to resolve node address: " << nodeURI;
                        return -1;
                    }

                    io::Address receiverAddr;
                    ECC::Amount amount = 0;
                    if (command == cli::SEND)
                    {
                        if (vm.count(cli::RECEIVER_ADDR) == 0)
                        {
                            LOG_ERROR() << "receiver's address is missing";
                            return -1;
                        }
                        if (vm.count(cli::AMOUNT) == 0)
                        {
                            LOG_ERROR() << "amount is missing";
                            return -1;
                        }
                        string receiverURI = vm[cli::RECEIVER_ADDR].as<string>();
                        if (!receiverAddr.resolve(receiverURI.c_str()))
                        {
                            LOG_ERROR() << "unable to resolve receiver address: " << receiverURI;
                            return -1;
                        }
                        auto signedAmount = vm[cli::AMOUNT].as<double>();
                        if (signedAmount < 0)
                        {
                            LOG_ERROR() << "Unable to send negative amount of coins";
                            return -1;
                        }

                        signedAmount *= Rules::Coin; // convert beams to coins

                        amount = static_cast<ECC::Amount>(signedAmount);
                        if (amount == 0)
                        {
                            LOG_ERROR() << "Unable to send zero coins";
                            return -1;
                        }
                    }

                    bool is_server = command == cli::LISTEN;
                    WalletNetworkIO wallet_io{ io::Address().ip(INADDR_ANY).port(port)
                        , node_addr
                        , is_server
                        , keychain
                        , reactor };
                    if (command == cli::SEND)
                    {
                        wallet_io.transfer_money(receiverAddr, move(amount), 0, {});
                    }
                    wallet_io.start();
                }
                else
                {
                    LOG_ERROR() << "command parameter not specified.";
                    printHelp(options);
                }
            }
            else
            {
                LOG_ERROR() << "unknown mode \'" << mode << "\'.";
                printHelp(options);
            }
        }
    }
    catch(const po::error& e)
    {
        LOG_ERROR() << e.what();
        printHelp(options);
    }
	catch (const std::runtime_error& e)
	{
		LOG_ERROR() << e.what();
	}

    return 0;
}
