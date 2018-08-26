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

#include "wallet/wallet_network.h"
#include "core/common.h"

#include "node.h"
#include "wallet/wallet.h"
#include "wallet/wallet_db.h"
#include "wallet/wallet_network.h"
#include "wallet/keystore.h"
#include "wallet/secstring.h"
#include "core/ecc_native.h"
#include "core/serialization_adapters.h"
#include "utility/logger.h"
#include "utility/options.h"
#include "utility/helpers.h"
#include <iomanip>

#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <iterator>
#include <future>
#include "version.h"

using namespace std;
using namespace beam;
using namespace ECC;

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
        size_t c = 13 - str.length();
        for (size_t i = 0; i < c; ++i) os << ' ';
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
	int Generate(uint32_t nCount, Height dh, Amount);
private:
	void Proceed(uint32_t i);
};

int TreasuryBlockGenerator::Generate(uint32_t nCount, Height dh, Amount v)
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
		coin.m_amount = v;
		coin.m_status = Coin::Unconfirmed;
		coin.m_createHeight = h + Rules::HeightGenesis;


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
		pKrn->get_Hash(hv);
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

	for (size_t i = i0; i < m_Coins.size(); i += m_vThreads.size())
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
	for (size_t i = i0; i < m_Coins.size(); i += m_vThreads.size(), iOutp++)
	{
		Block::Body& block = get_WriteBlock();

		block.m_vOutputs.push_back(std::move(vOut[iOutp]));
		block.m_Subsidy += m_Coins[i].m_amount;
		m_Offset += m_vIncubationAndKeys[i].second;
	}
}

#define LOG_VERBOSE_ENABLED 0

io::Reactor::Ptr reactor;

static const unsigned LOG_ROTATION_PERIOD = 3*60*60*1000; // 3 hours

int main_impl(int argc, char* argv[])
{
	try
	{
		auto options = createOptionsDescription();

		po::variables_map vm;
		try
		{
			vm = getOptions(argc, argv, "beam.cfg", options);
		}
		catch (const po::error& e)
		{
			cout << e.what() << std::endl;
			printHelp(options);

			return 0;
		}

		if (vm.count(cli::HELP))
		{
			printHelp(options);

			return 0;
		}

		if (vm.count(cli::VERSION))
		{
			cout << PROJECT_VERSION << endl;
			return 0;
		}

		if (vm.count(cli::GIT_COMMIT_HASH))
		{
			cout << GIT_COMMIT_HASH << endl;
			return 0;
		}

		// init logger here to determine node/wallet name

		int logLevel = getLogLevel(cli::LOG_LEVEL, vm, LOG_LEVEL_DEBUG);
		int fileLogLevel = getLogLevel(cli::FILE_LOG_LEVEL, vm, LOG_LEVEL_INFO);

#if LOG_VERBOSE_ENABLED
		logLevel = LOG_LEVEL_VERBOSE;
#endif
		std::string prefix = "beam_";
		if (vm.count(cli::MODE))
		{
			auto mode = vm[cli::MODE].as<string>();
			if (mode == cli::NODE) prefix += "node_";
			else if (mode == cli::WALLET) prefix += "wallet_";
		}

		const auto path = boost::filesystem::system_complete("./logs");
		auto logger = beam::Logger::create(logLevel, logLevel, fileLogLevel, prefix, path.string());

		try
		{
			po::notify(vm);

			Rules::get().UpdateChecksum();
			LOG_INFO() << "Rules signature: " << Rules::get().Checksum;

			auto port = vm[cli::PORT].as<uint16_t>();
			auto hasWalletSeed = vm.count(cli::WALLET_SEED) > 0;

			if (vm.count(cli::MODE))
			{
				reactor = io::Reactor::create();
				io::Reactor::Scope scope(*reactor);

				io::Reactor::GracefulIntHandler gih(*reactor);

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

				io::Timer::Ptr logRotateTimer = io::Timer::create(reactor);
				logRotateTimer->start(
					LOG_ROTATION_PERIOD, true,
					[]() {
						Logger::get()->rotate();
					}
				);

				auto mode = vm[cli::MODE].as<string>();
				if (mode == cli::NODE)
				{
					beam::Node node;

					node.m_Cfg.m_Listen.port(port);
					node.m_Cfg.m_Listen.ip(INADDR_ANY);
					node.m_Cfg.m_sPathLocal = vm[cli::STORAGE].as<string>();
					node.m_Cfg.m_MiningThreads = vm[cli::MINING_THREADS].as<uint32_t>();
					node.m_Cfg.m_MinerID = vm[cli::MINER_ID].as<uint32_t>();
					node.m_Cfg.m_VerificationThreads = vm[cli::VERIFICATION_THREADS].as<int>();
					if (node.m_Cfg.m_MiningThreads > 0 && !hasWalletSeed)
					{
						LOG_ERROR() << " wallet seed is not provided. You have pass wallet seed for mining node.";
						return -1;
					}
					node.m_Cfg.m_WalletKey = walletSeed;

					std::vector<std::string> vPeers = getCfgPeers(vm);

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
							&& command != cli::RECEIVE
							&& command != cli::LISTEN
							&& command != cli::TREASURY
							&& command != cli::INFO)
						{
							LOG_ERROR() << "unknown command: \'" << command << "\'";
							return -1;
						}

						assert(vm.count(cli::WALLET_STORAGE) > 0);
						auto walletPath = vm[cli::WALLET_STORAGE].as<string>();

                        assert(vm.count(cli::BBS_STORAGE) > 0);
                        auto bbsKeysPath = vm[cli::BBS_STORAGE].as<string>();

						if (!Keychain::isInitialized(walletPath) && command != cli::INIT)
						{
							LOG_ERROR() << "Please initialize your wallet first... \nExample: beam wallet --command=init --pass=<password to access wallet> --wallet_seed=<seed to generate secret keys>";
							return -1;
						}

						LOG_INFO() << "starting a wallet...";

						// TODO: we should use secure string
						SecString pass;
						if (vm.count(cli::PASS))
						{
							pass = SecString(string_view(vm[cli::PASS].as<string>()));
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

                                IKeyStore::Options options;
                                options.flags = IKeyStore::Options::local_file | IKeyStore::Options::enable_all_keys;
                                options.fileName = bbsKeysPath;

                                IKeyStore::Ptr ks = IKeyStore::create(options, pass.data(), pass.size());

                                // generate default address
                                WalletAddress defaultAddress = {};
                                defaultAddress.m_own = true;
                                defaultAddress.m_label = "default";
                                defaultAddress.m_createTime = getTimestamp();
                                defaultAddress.m_duration = numeric_limits<uint64_t>::max();
                                ks->gen_keypair(defaultAddress.m_walletID);
                                ks->save_keypair(defaultAddress.m_walletID, true);
                                keychain->saveAddress(defaultAddress);

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
							tbg.m_sPath = vm[cli::TREASURY_BLOCK].as<string>();
							tbg.m_pKeyChain = keychain.get();

							Amount v = vm[cli::TR_BEAMS].as<uint32_t>();
							v *= Rules::Coin;
							Height dh = vm[cli::TR_DH].as<uint32_t>();
							uint32_t nCount = vm[cli::TR_COUNT].as<uint32_t>();

							return tbg.Generate(nCount, dh, v);
						}

						if (command == cli::INFO)
						{
							Block::SystemState::ID stateID = {};
							keychain->getSystemStateID(stateID);
							cout << "____Wallet summary____\n\n"
								<< "Current height............" << stateID.m_Height << '\n'
								<< "Current state ID.........." << stateID.m_Hash << "\n\n"
								<< "Available................." << PrintableAmount(wallet::getAvailable(keychain)) << '\n'
								<< "Unconfirmed..............." << PrintableAmount(wallet::getTotal(keychain, Coin::Unconfirmed)) << '\n'
								<< "Locked...................." << PrintableAmount(wallet::getTotal(keychain, Coin::Locked)) << '\n'
								<< "Available coinbase ......." << PrintableAmount(wallet::getAvailableByType(keychain, Coin::Unspent, KeyType::Coinbase)) << '\n'
								<< "Total coinbase............" << PrintableAmount(wallet::getTotalByType(keychain, Coin::Unspent, KeyType::Coinbase)) << '\n'
								<< "Avaliable fee............." << PrintableAmount(wallet::getAvailableByType(keychain, Coin::Unspent, KeyType::Comission)) << '\n'
								<< "Total fee................." << PrintableAmount(wallet::getTotalByType(keychain, Coin::Unspent, KeyType::Comission)) << '\n'
								<< "Total unspent............." << PrintableAmount(wallet::getTotal(keychain, Coin::Unspent)) << "\n\n";
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
									cout << "  " << format_timestamp("%Y.%m.%d %H:%M:%S", tx.m_createTime * 1000, false)
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
									<< setw(16) << static_cast<int64_t>(c.m_createHeight)
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
						Amount amount = 0;
						Amount fee = 0;
                        ECC::Hash::Value receiverWalletID;
						bool isTxInitiator = command == cli::SEND || command == cli::RECEIVE;
						if (isTxInitiator)
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

                            ECC::Hash::Value receiverID = from_hex(vm[cli::RECEIVER_ADDR].as<string>());
                            receiverWalletID = receiverID;

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

							auto signedFee = vm[cli::FEE].as<double>();
							if (signedFee < 0)
							{
								LOG_ERROR() << "Unable to take negative fee";
								return -1;
							}

							signedFee *= Rules::Coin; // convert beams to coins

							fee = static_cast<ECC::Amount>(signedFee);
						}

						bool is_server = command == cli::LISTEN;

                        IKeyStore::Options options;
                        options.flags = IKeyStore::Options::local_file | IKeyStore::Options::enable_all_keys;
                        options.fileName = bbsKeysPath;

                        IKeyStore::Ptr keystore = IKeyStore::create(options, pass.data(), pass.size());

                        auto wallet_io = make_shared<WalletNetworkIO >( node_addr
						                                              , keychain
                                                                      , keystore
							                                          , reactor);
						Wallet wallet{ keychain
									 , wallet_io
                                     , false
									 , is_server ? Wallet::TxCompletedAction() : [wallet_io](auto) { wallet_io->stop(); } };

						if (isTxInitiator)
						{
                            // TODO: make db request by 'default' label
                            auto addresses = keychain->getAddresses(true);
                            assert(!addresses.empty());
							wallet.transfer_money(addresses[0].m_walletID, receiverWalletID, move(amount), move(fee), command == cli::SEND);
						}

						wallet_io->start();
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
		catch (const po::error& e)
		{
			LOG_ERROR() << e.what();
			printHelp(options);
		}
		catch (const std::runtime_error& e)
		{
			LOG_ERROR() << e.what();
		}
	}
	catch (const std::exception& e)
	{
		std::cout << e.what() << std::endl;
	}

    return 0;
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    return main_impl(argc, argv);
#else
    auto f = std::async(
        std::launch::async,
        [argc, argv]() -> int {
            // TODO: this hungs app on OSX
            //lock_signals_in_this_thread();
            int ret = main_impl(argc, argv);
            kill(0, SIGINT);
            return ret;
        }
    );

    wait_for_termination(0);

    if (reactor) reactor->stop();

    return f.get();
#endif
}

