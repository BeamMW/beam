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

    bool ReadTreasury(ByteBuffer& bb, const string& sPath)
    {
		if (sPath.empty())
			return false;

		std::FStream f;
		if (!f.Open(sPath.c_str(), true))
			return false;

		size_t nSize = static_cast<size_t>(f.get_Remaining());
		if (!nSize)
			return false;

		bb.resize(f.get_Remaining());
		return f.read(&bb.front(), nSize) == nSize;
    }

	void find_certificates(IExternalPOW::Options& o, const std::string& stratumDir) {
		static const std::string certFileName("stratum.crt");
		static const std::string keyFileName("stratum.key");
		static const std::string apiKeysFileName("stratum.api.keys");

		boost::filesystem::path p(stratumDir);
		p = boost::filesystem::canonical(p);
		o.privKeyFile = (p / keyFileName).string();
		o.certFile = (p / certFileName).string();

		if (boost::filesystem::exists(p / apiKeysFileName))
			o.apiKeysFile = (p / apiKeysFileName).string();
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
		auto [options, visibleOptions] = createOptionsDescription(GENERAL_OPTIONS | NODE_OPTIONS);

		po::variables_map vm;
		try
		{
			vm = getOptions(argc, argv, "beam-node.cfg", options);
		}
		catch (const po::error& e)
		{
			cout << e.what() << std::endl;
			printHelp(visibleOptions);

			return 0;
		}

		if (vm.count(cli::HELP))
		{
			printHelp(visibleOptions);

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
                    find_certificates(powOptions, vm[cli::STRATUM_SECRETS_PATH].as<string>());
					stratumServer = IExternalPOW::create(powOptions, *reactor, io::Address().port(stratumPort));
				}

				{
					beam::Node node;

					node.m_Cfg.m_Listen.port(port);
					node.m_Cfg.m_Listen.ip(INADDR_ANY);
					node.m_Cfg.m_sPathLocal = vm[cli::STORAGE].as<string>();
#if defined(BEAM_USE_GPU)

                    if (!stratumServer)
                    {
                        if (vm[cli::MINER_TYPE].as<string>() == "gpu")
                        {
                            stratumServer = IExternalPOW::create_opencl_solver({-1});
                            // now for GPU only 0 thread
                            node.m_Cfg.m_MiningThreads = 0;
                        }
                        else
                        {
                            node.m_Cfg.m_MiningThreads = vm[cli::MINING_THREADS].as<uint32_t>();
                        }
                    }
#else
					node.m_Cfg.m_MiningThreads = vm[cli::MINING_THREADS].as<uint32_t>();
#endif
					node.m_Cfg.m_VerificationThreads = vm[cli::VERIFICATION_THREADS].as<int>();

					node.m_Cfg.m_LogUtxos = vm[cli::LOG_UTXOS].as<bool>();

					std::string sKeyOwner;
					{
						const auto& var = vm[cli::KEY_OWNER];
						if (!var.empty())
							sKeyOwner = var.as<std::string>();
					}
					std::string sKeyMine;
					{
						const auto& var = vm[cli::KEY_MINE];
						if (!var.empty())
							sKeyMine = var.as<std::string>();
					}

					if (!(sKeyOwner.empty() && sKeyMine.empty()))
					{
						SecString pass;
						if (!beam::read_wallet_pass(pass, vm))
						{
							LOG_ERROR() << "Please, provide password for the keys.";
							return -1;
						}

						KeyString ks;
						ks.SetPassword(Blob(pass.data(), static_cast<uint32_t>(pass.size())));

						if (!sKeyMine.empty())
						{
							ks.m_sRes = sKeyMine;

							std::shared_ptr<HKdf> pKdf = std::make_shared<HKdf>();
							if (!ks.Import(*pKdf))
								throw std::runtime_error("miner key import failed");

							node.m_Keys.m_pMiner = pKdf;
							node.m_Keys.m_nMinerSubIndex = atoi(ks.m_sMeta.c_str());
						}

						if (!sKeyOwner.empty())
						{
							ks.m_sRes = sKeyOwner;

							std::shared_ptr<HKdfPub> pKdf = std::make_shared<HKdfPub>();
							if (!ks.Import(*pKdf))
								throw std::runtime_error("view key import failed");

							node.m_Keys.m_pOwner = pKdf;
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
						if (!ReadTreasury(node.m_Cfg.m_Treasury, sPath))
							node.m_Cfg.m_Treasury.clear();
						else
						{
							if (!node.m_Cfg.m_Treasury.empty())
								LOG_INFO() << "Treasury size: " << node.m_Cfg.m_Treasury.size();
						}
					}

					if (vm.count(cli::RESYNC))
						node.m_Cfg.m_Sync.m_ForceResync = vm[cli::RESYNC].as<bool>();

					node.m_Cfg.m_Bbs = vm[cli::BBS_ENABLE].as<bool>();

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
			printHelp(visibleOptions);
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
	catch (const beam::CorruptionException& e)
	{
		std::cout << "Corruption: " << e.m_sErr << std::endl;
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

