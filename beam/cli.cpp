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
#include "utility/log_rotation.h"
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

	void find_certificates(IExternalPOW::Options& o, const std::string& stratumDir, bool useTLS) {

		boost::filesystem::path p(stratumDir);
		p = boost::filesystem::canonical(p);

        if (useTLS)
        {
		    static const std::string certFileName("stratum.crt");
		    static const std::string keyFileName("stratum.key");

		    o.privKeyFile = (p / keyFileName).string();
		    o.certFile = (p / certFileName).string();
        }

		static const std::string apiKeysFileName("stratum.api.keys");
		if (boost::filesystem::exists(p / apiKeysFileName))
			o.apiKeysFile = (p / apiKeysFileName).string();
	}
}

#ifndef LOG_VERBOSE_ENABLED
    #define LOG_VERBOSE_ENABLED 0
#endif

io::Reactor::Ptr reactor;

static const unsigned LOG_ROTATION_PERIOD_SEC = 3*60*60; // 3 hours

class NodeObserver : public Node::IObserver
{
public:
    NodeObserver(Node& node) : m_pNode(&node)
    {
    }

private:

    void OnSyncProgress() override
    {
        // make sure no overflow during conversion from SyncStatus to int,int.
        Node::SyncStatus s = m_pNode->m_SyncStatus;

        unsigned int nThreshold = static_cast<unsigned int>(std::numeric_limits<int>::max());
        while (s.m_Total > nThreshold)
        {
            s.m_Total >>= 1;
            s.m_Done >>= 1;
        }
        int p = static_cast<int>((s.m_Done * 100) / s.m_Total);
        LOG_INFO() << "Updating node: " << p << "% (" << s.m_Done << "/" << s.m_Total << ")";
    }

    Node* m_pNode;
};

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

#define LOG_FILES_DIR "logs"
#define LOG_FILES_PREFIX "node_"

		const auto path = boost::filesystem::system_complete(LOG_FILES_DIR);
		auto logger = beam::Logger::create(logLevel, logLevel, fileLogLevel, LOG_FILES_PREFIX, path.string());

		try
		{
			po::notify(vm);

			unsigned logCleanupPeriod = vm[cli::LOG_CLEANUP_DAYS].as<uint32_t>() * 24 * 3600;

			clean_old_logfiles(LOG_FILES_DIR, LOG_FILES_PREFIX, logCleanupPeriod);

			Rules::get().UpdateChecksum();
            LOG_INFO() << "Beam Node " << PROJECT_VERSION << " (" << BRANCH_NAME << ")";
			LOG_INFO() << "Rules signature: " << Rules::get().Checksum;

			auto port = vm[cli::PORT].as<uint16_t>();

			{
				reactor = io::Reactor::create();
				io::Reactor::Scope scope(*reactor);

				io::Reactor::GracefulIntHandler gih(*reactor);

				LogRotation logRotation(*reactor, LOG_ROTATION_PERIOD_SEC, logCleanupPeriod);

				std::unique_ptr<IExternalPOW> stratumServer;
				auto stratumPort = vm[cli::STRATUM_PORT].as<uint16_t>();

				if (stratumPort > 0) {
					IExternalPOW::Options powOptions;
                    find_certificates(powOptions, vm[cli::STRATUM_SECRETS_PATH].as<string>(), vm[cli::STRATUM_USE_TLS].as<bool>());
					stratumServer = IExternalPOW::create(powOptions, *reactor, io::Address().port(stratumPort));
				}

				{
					beam::Node node;

                    NodeObserver observer(node);

                    node.m_Cfg.m_Observer = &observer;

					node.m_Cfg.m_Listen.port(port);
					node.m_Cfg.m_Listen.ip(INADDR_ANY);
					node.m_Cfg.m_sPathLocal = vm[cli::STORAGE].as<string>();
					node.m_Cfg.m_MiningThreads = vm[cli::MINING_THREADS].as<uint32_t>();
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
						node.m_Cfg.m_ProcessorParams.m_ResetCursor = vm[cli::RESYNC].as<bool>();

					if (vm.count(cli::CHECKDB))
						node.m_Cfg.m_ProcessorParams.m_CheckIntegrityAndVacuum = vm[cli::CHECKDB].as<bool>();

					node.m_Cfg.m_Bbs = vm[cli::BBS_ENABLE].as<bool>();

					node.m_Cfg.m_Horizon.m_Branching = Rules::get().Macroblock.MaxRollback / 4; // inferior branches would be pruned when height difference is this.
					node.m_Cfg.m_Horizon.m_SchwarzschildHi = vm[cli::HORIZON_HI].as<Height>();
					node.m_Cfg.m_Horizon.m_SchwarzschildLo = vm[cli::HORIZON_LO].as<Height>();

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

