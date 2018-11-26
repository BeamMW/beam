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

#include "node/node.h"
#include "core/ecc_native.h"
#include "core/ecc.h"
#include "core/serialization_adapters.h"
#include "utility/logger.h"
#include "utility/options.h"
#include "utility/helpers.h"
#include <iomanip>

#include "pow/external_pow.h"

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
}

#ifndef LOG_VERBOSE_ENABLED
    #define LOG_VERBOSE_ENABLED 0
#endif

io::Reactor::Ptr reactor;

static const unsigned LOG_ROTATION_PERIOD = 3*60*60*1000; // 3 hours

int main_impl(int argc, char* argv[])
{
	beam::Crash::InstallHandler(NULL);

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
		int fileLogLevel = getLogLevel(cli::FILE_LOG_LEVEL, vm, LOG_LEVEL_DEBUG);

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

				io::Timer::Ptr logRotateTimer = io::Timer::create(*reactor);
				logRotateTimer->start(
					LOG_ROTATION_PERIOD, true,
					[]() {
						Logger::get()->rotate();
					}
				);

				std::unique_ptr<IExternalPOW> stratumServer;
				auto stratumPort = vm[cli::STRATUM_PORT].as<uint16_t>();

				if (stratumPort > 0) {
					IExternalPOW::Options powOptions;
                    powOptions.certFile = PROJECT_SOURCE_DIR "/utility/unittest/test.crt";
                    powOptions.privKeyFile = PROJECT_SOURCE_DIR "/utility/unittest/test.key";
					stratumServer = IExternalPOW::create(powOptions, *reactor, io::Address().port(stratumPort));
				}

				{
					beam::Node node;

					node.m_Cfg.m_Listen.port(port);
					node.m_Cfg.m_Listen.ip(INADDR_ANY);
					node.m_Cfg.m_sPathLocal = vm[cli::STORAGE].as<string>();
#if defined(BEAM_USE_GPU)
                    if (vm[cli::MINER_TYPE].as<string>() == "gpu")
                    {
                        node.m_Cfg.m_UseGpu = true;
                        // now for GPU only 1 thread
                        node.m_Cfg.m_MiningThreads = 1;
                    }
                    else
                    {
                        node.m_Cfg.m_UseGpu = false;
                        node.m_Cfg.m_MiningThreads = vm[cli::MINING_THREADS].as<uint32_t>();
                    }
#else
					node.m_Cfg.m_MiningThreads = vm[cli::MINING_THREADS].as<uint32_t>();
#endif
					node.m_Cfg.m_VerificationThreads = vm[cli::VERIFICATION_THREADS].as<int>();
					if (node.m_Cfg.m_MiningThreads > 0)
					{
						ECC::NoLeak<ECC::uintBig> seed;
						if (!beam::read_wallet_seed(seed, vm)) {
                            LOG_ERROR() << " wallet seed is not provided. You have pass wallet seed for mining node.";
                            return -1;
                        }

						node.m_Keys.InitSingleKey(seed.V);
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

#ifdef BEAM_TESTNET
                    node.m_Cfg.m_ControlState.m_Height = Rules::HeightGenesis;
					node.m_Cfg.m_ControlState.m_Hash = {
						0xf6, 0xf9, 0x01, 0x39, 0x3a, 0x10, 0x30, 0x80, 0x86, 0x4f, 0x75, 0xb6, 0x6b, 0x78, 0xa9, 0x6e,
						0x6d, 0xf0, 0x10, 0xb5, 0x3f, 0x9a, 0xaf, 0x32, 0xe3, 0xcb, 0xc7, 0x5f, 0xa3, 0x6a, 0x21, 0x97
					};
#endif

					if (vm.count(cli::RESYNC))
						node.m_Cfg.m_Sync.m_ForceResync = vm[cli::RESYNC].as<bool>();

					node.Initialize(stratumServer.get());

					Height hImport = vm[cli::IMPORT].as<Height>();
					if (hImport)
						node.ImportMacroblock(hImport);

					io::Timer::Ptr pCrashTimer;

					int nCrash = vm.count(cli::CRASH) ? vm[cli::CRASH].as<int>() : 0;
					if (nCrash)
					{
						pCrashTimer = io::Timer::create(*reactor);

						pCrashTimer->start(5000, false, [nCrash]() {
							Crash::Induce((Crash::Type) (nCrash - 1));
						});
					}

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
    block_sigpipe();
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

