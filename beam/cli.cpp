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
#include "core/ecc_native.h"
#include "core/ecc.h"
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

    bool read_wallet_seed(NoLeak<uintBig>& walletSeed) {
        static const size_t MAX_SEED_LEN = 2000;
        char buf[MAX_SEED_LEN];
        size_t len = MAX_SEED_LEN;
        read_password("Enter seed: ", buf, len);

        if (len == 0) {
            return false;
        }

        Hash::Processor hp;
        hp.Write(buf, (uint32_t)len);
        hp >> walletSeed.V;

        SecureErase(buf, MAX_SEED_LEN);

        return true;
    }
}

#define LOG_VERBOSE_ENABLED 0

io::Reactor::Ptr reactor;

static const unsigned LOG_ROTATION_PERIOD = 3*60*60*1000; // 3 hours

int main_impl(int argc, char* argv[])
{
	try
	{
		auto options = createOptionsDescription(GENERAL_OPTIONS | NODE_OPTIONS);

		po::variables_map vm;
		try
		{
			vm = getOptions(argc, argv, "beam-node.cfg", options);
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

		int logLevel = getLogLevel(cli::LOG_LEVEL, vm, LOG_LEVEL_DEBUG);
		int fileLogLevel = getLogLevel(cli::FILE_LOG_LEVEL, vm, LOG_LEVEL_INFO);

#if LOG_VERBOSE_ENABLED
		logLevel = LOG_LEVEL_VERBOSE;
#endif

		const auto path = boost::filesystem::system_complete("./logs");
		auto logger = beam::Logger::create(logLevel, logLevel, fileLogLevel, "node_", path.string());

		try
		{
			po::notify(vm);

			Rules::get().UpdateChecksum();
			LOG_INFO() << "Rules signature: " << Rules::get().Checksum;

			auto port = vm[cli::PORT].as<uint16_t>();

			{
				reactor = io::Reactor::create();
				io::Reactor::Scope scope(*reactor);

				io::Reactor::GracefulIntHandler gih(*reactor);

				NoLeak<uintBig> walletSeed;
				walletSeed.V = Zero;

				io::Timer::Ptr logRotateTimer = io::Timer::create(reactor);
				logRotateTimer->start(
					LOG_ROTATION_PERIOD, true,
					[]() {
						Logger::get()->rotate();
					}
				);

				{
					beam::Node node;

					node.m_Cfg.m_Listen.port(port);
					node.m_Cfg.m_Listen.ip(INADDR_ANY);
					node.m_Cfg.m_sPathLocal = vm[cli::STORAGE].as<string>();
					node.m_Cfg.m_MiningThreads = vm[cli::MINING_THREADS].as<uint32_t>();
					node.m_Cfg.m_MinerID = vm[cli::MINER_ID].as<uint32_t>();
					node.m_Cfg.m_VerificationThreads = vm[cli::VERIFICATION_THREADS].as<int>();
					if (node.m_Cfg.m_MiningThreads > 0)
					{
                        if (!read_wallet_seed(node.m_Cfg.m_WalletKey)) {
                            LOG_ERROR() << " wallet seed is not provided. You have pass wallet seed for mining node.";
                            return -1;
                        }
					}

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

