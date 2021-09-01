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

#include "wallet/core/wallet_network.h"
#include "core/common.h"

#include "node/node.h"
#include "core/ecc_native.h"
#include "core/serialization_adapters.h"
#include "core/block_rw.h"
#include "bvm/bvm2.h"
#include "utility/cli/options.h"
#include "utility/log_rotation.h"
#include "utility/helpers.h"
#include <iomanip>

#include "pow/external_pow.h"
#include "websocket/websocket_server.h"


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
	using namespace beam;
	using namespace beam::wallet;

	class WebClient
		: public WebSocketServer::ClientHandler  // We handle web socket client
		, public std::enable_shared_from_this<WebClient>
	{
	public:
		WebClient(WebSocketServer::SendFunc wsSend, WebSocketServer::CloseFunc wsClose, uint16_t nodePort)
			: m_wsSend(std::move(wsSend))
			, m_wsClose(std::move(wsClose))
		{
			auto& r = io::Reactor::get_Current();
			r.tcp_connect(io::Address::localhost().port(nodePort), uint64_t(this), BIND_THIS_MEMFN(OnConnected));
		}

		~WebClient() noexcept override
		{
			auto& r = io::Reactor::get_Current();
			r.cancel_tcp_connect(uint64_t(this));
		}
	private:
		void ReactorThread_onWSDataReceived(const std::string& data) override
		{
			m_DataQueue.push(data);
			if (m_Stream)
			{
				ProcessDataQueue();
			}
		}

		void ProcessDataQueue()
		{
			while (!m_DataQueue.empty())
			{
				auto& d = m_DataQueue.front();
				m_Stream->write(d.data(), d.size());
				m_DataQueue.pop();
			}
		}

		void OnConnected(uint64_t tag, io::TcpStream::Ptr&& newStream, io::ErrorCode errorCode)
		{
			if (newStream)
			{
				LOG_DEBUG() << "Websocket proxy connected to the node";
				m_Stream = std::move(newStream);
				m_Stream->enable_read(
					[this](io::ErrorCode what, void* data, size_t size) -> bool
					{
						m_wsSend(std::string((const char*)data, size));
						return true;
					});
				ProcessDataQueue();
			}
			else
			{
				std::stringstream ss;
				ss << "Websocket proxy failed connected to the node: " << io::error_str(errorCode);
				LOG_DEBUG() << ss.str();
				m_wsClose(ss.str());
			}
		}
	private:
		WebSocketServer::SendFunc m_wsSend;
		WebSocketServer::CloseFunc m_wsClose;
		io::TcpStream::Ptr m_Stream;
		std::queue<std::string> m_DataQueue;

	};

	class WebSocketProxy : public WebSocketServer
	{
	public:
		WebSocketProxy(SafeReactor::Ptr reactor, const Options& options, uint16_t nodePort)
			: WebSocketServer(std::move(reactor), options)
			, m_NodePort(nodePort)
		{
		}

		virtual ~WebSocketProxy() = default;

	private:
		WebSocketServer::ClientHandler::Ptr ReactorThread_onNewWSClient(WebSocketServer::SendFunc wsSend, WebSocketServer::CloseFunc wsClose) override
		{
			return std::make_shared<WebClient>(wsSend, wsClose, m_NodePort);
		}

	private:
		uint16_t m_NodePort;
	};
}

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

	void FindWSCertificates(WebSocketServer::Options& o, const std::string& dir)
	{
		boost::filesystem::path p(dir);
		p = boost::filesystem::canonical(p);

		static const std::string certFileName("wscert.pem");
		static const std::string keyFileName("wskey.pem");
		static const std::string dhParamsName("wsdhparams.pem");
		o.keyPath = (p / keyFileName).string();
		o.certificatePath = (p / certFileName).string();
		o.dhParamsPath = (p / dhParamsName).string();
	}

	template<typename T>
	void get_parametr_with_deprecated_synonym(const po::variables_map& vm, const char* name, const char* deprecatedName, T* result)
	{
		auto var = vm[name];
		if (var.empty())
		{
			var = vm[deprecatedName];
			if (!var.empty())
				LOG_WARNING() << "The \"" << deprecatedName << "\"" << " parameter is deprecated, use " << "\"" << name << "\" instead.";
		}

		if (!var.empty()) {
			*result = var.as<T>();
		}
	}
}

#ifndef LOG_VERBOSE_ENABLED
	#define LOG_VERBOSE_ENABLED 0
#endif

io::Reactor::Ptr reactor;

static const unsigned LOG_ROTATION_PERIOD_SEC = 3*60*60; // 3 hours

class NodeObserver : public Node::IObserver
{
	Height m_Done0 = MaxHeight;
public:
	NodeObserver(Node& node) : m_pNode(&node)
	{
	}

private:

	void OnSyncProgress() override
	{
		// make sure no overflow during conversion from SyncStatus to int,int.
		Node::SyncStatus s = m_pNode->m_SyncStatus;

		if (MaxHeight == m_Done0)
			m_Done0 = s.m_Done;
		s.ToRelative(m_Done0);

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
		auto [options, visibleOptions] = createOptionsDescription(GENERAL_OPTIONS | NODE_OPTIONS, "beam-node.cfg");

		po::variables_map vm;
		try
		{
			vm = getOptions(argc, argv, options);
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
			LOG_INFO() << "Rules signature: " << Rules::get().get_SignatureStr();

			auto port = vm[cli::PORT].as<uint16_t>();

			if (!port)
			{
				LOG_ERROR() << "Port must be specified";
				return -1;
			}

			{
				SafeReactor::Ptr safeReactor = SafeReactor::create();
				reactor = safeReactor->ptr();
				io::Reactor::Scope scope(*reactor);

				io::Reactor::GracefulIntHandler gih(*reactor);

				LogRotation logRotation(*reactor, LOG_ROTATION_PERIOD_SEC, logCleanupPeriod);

				std::unique_ptr<IExternalPOW> stratumServer;
				auto stratumPort = vm[cli::STRATUM_PORT].as<uint16_t>();

				if (stratumPort > 0) 
				{
					IExternalPOW::Options powOptions;
					find_certificates(powOptions, vm[cli::STRATUM_SECRETS_PATH].as<string>(), vm[cli::STRATUM_USE_TLS].as<bool>());
					unsigned noncePrefixDigits = vm[cli::NONCEPREFIX_DIGITS].as<unsigned>();
					if (noncePrefixDigits > 6) noncePrefixDigits = 6;
					stratumServer = IExternalPOW::create(powOptions, *reactor, io::Address().port(stratumPort), noncePrefixDigits);
				}

				{
					beam::Node node;

					std::unique_ptr<WebSocketProxy> webSocketProxy;
					if (auto wsPort = vm[cli::WEBSOCKET_PORT].as<uint16_t>(); wsPort > 0)
					{
						WebSocketServer::Options wsOptions;
						wsOptions.port = wsPort;
						wsOptions.useTls = vm[cli::WEBSOCKET_USE_TLS].as<bool>();
						if (wsOptions.useTls)
						{
							FindWSCertificates(wsOptions, vm[cli::WEBSOCKET_SECRETS_PATH].as<string>());
						}
						webSocketProxy = std::make_unique<WebSocketProxy>(safeReactor, wsOptions, port);
					}

					NodeObserver observer(node);

					node.m_Cfg.m_Observer = &observer;

					node.m_Cfg.m_Listen.port(port);
					node.m_Cfg.m_Listen.ip(INADDR_ANY);
					node.m_Cfg.m_sPathLocal = vm[cli::STORAGE].as<string>();

					if (Rules::get().FakePoW)
					{
						node.m_Cfg.m_MiningThreads = vm[cli::MINING_THREADS].as<uint32_t>();
						node.m_Cfg.m_TestMode.m_FakePowSolveTime_ms = vm[cli::POW_SOLVE_TIME].as<uint32_t>();
					}
					else
					{
						node.m_Cfg.m_MiningThreads = 0; // by default disabled
					}

					node.m_Cfg.m_VerificationThreads = vm[cli::VERIFICATION_THREADS].as<int>();

					node.m_Cfg.m_LogEvents = vm[cli::LOG_UTXOS].as<bool>();

					std::string sKeyOwner;
					get_parametr_with_deprecated_synonym(vm, cli::OWNER_KEY, cli::KEY_OWNER, &sKeyOwner);

					std::string sKeyMine;
					get_parametr_with_deprecated_synonym(vm, cli::MINER_KEY, cli::KEY_MINE, &sKeyMine);

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
							ks.m_sRes = move(sKeyMine);

							std::shared_ptr<HKdf> pKdf = std::make_shared<HKdf>();
							if (!ks.Import(*pKdf))
								throw std::runtime_error("miner key import failed");

							node.m_Keys.m_pMiner = pKdf;
							node.m_Keys.m_nMinerSubIndex = atoi(ks.m_sMeta.c_str());
						}

						if (!sKeyOwner.empty())
						{
							ks.m_sRes = move(sKeyOwner);

							std::shared_ptr<HKdfPub> pKdf = std::make_shared<HKdfPub>();
							if (!ks.Import(*pKdf))
								throw std::runtime_error("view key import failed");

							node.m_Keys.m_pOwner = pKdf;
						}
					}

					std::vector<std::string> vPeers = getCfgPeers(vm);

					for (size_t i = 0; i < vPeers.size(); i++)
					{
						io::Address addr;

						if (addr.resolve(vPeers[i].c_str()))
						{
							if (!addr.port())
							{
								LOG_WARNING() << "No port is specified for \"" << vPeers[i] << "\", the default value is " << port;
								addr.port(port);
							}

							node.m_Cfg.m_Connect.push_back(addr);
						}
						else
						{
							LOG_ERROR() << "unable to resolve: " << vPeers[i];
						}
					}

					node.m_Cfg.m_PeersPersistent = vm[cli::NODE_PEERS_PERSISTENT].as<bool>();

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

					if (vm.count(cli::CHECKDB))
						node.m_Cfg.m_ProcessorParams.m_CheckIntegrity = vm[cli::CHECKDB].as<bool>();

					if (vm.count(cli::VACUUM))
						node.m_Cfg.m_ProcessorParams.m_Vacuum = vm[cli::VACUUM].as<bool>();

					if (vm.count(cli::RESET_ID))
						node.m_Cfg.m_ProcessorParams.m_ResetSelfID = vm[cli::RESET_ID].as<bool>();

					if (vm.count(cli::ERASE_ID))
						node.m_Cfg.m_ProcessorParams.m_EraseSelfID = vm[cli::ERASE_ID].as<bool>();

					if (!vm[cli::BBS_ENABLE].as<bool>())
						ZeroObject(node.m_Cfg.m_Bbs.m_Limit);

					auto var = vm[cli::FAST_SYNC];
					if (!var.empty())
					{
						if (var.as<bool>())
							node.m_Cfg.m_Horizon.SetStdFastSync();
						else
							node.m_Cfg.m_Horizon.SetInfinite();
					}

					ByteBuffer bufRichParser;

					if (vm.count(cli::CONTRACT_RICH_INFO))
					{
						uint8_t nFlag = vm[cli::CONTRACT_RICH_INFO].as<bool>() ?
							NodeProcessor::StartParams::RichInfo::On :
							NodeProcessor::StartParams::RichInfo::Off;

						node.m_Cfg.m_ProcessorParams.m_RichInfoFlags |= nFlag;
					}

					if (vm.count(cli::CONTRACT_RICH_PARSER))
					{
						auto sPath = vm[cli::CONTRACT_RICH_PARSER].as<std::string>();
						if (!sPath.empty())
						{
							std::FStream fs;
							fs.Open(sPath.c_str(), true, true);

							bufRichParser.resize(static_cast<size_t>(fs.get_Remaining()));
							if (!bufRichParser.empty())
								fs.read(&bufRichParser.front(), bufRichParser.size());

							bvm2::Processor::Compile(bufRichParser, bufRichParser, bvm2::Processor::Kind::Manager);

							node.m_Cfg.m_ProcessorParams.m_RichParser = bufRichParser;
						}

						node.m_Cfg.m_ProcessorParams.m_RichInfoFlags |= NodeProcessor::StartParams::RichInfo::UpdShader;
					}

					node.Initialize(stratumServer.get());

					if (vm[cli::PRINT_TXO].as<bool>())
						node.PrintTxos();

					if (vm[cli::PRINT_ROLLBACK_STATS].as<bool>())
						node.PrintRollbackStats();

					if (vm.count(cli::GENERATE_RECOVERY_PATH))
					{
						string sPath = vm[cli::GENERATE_RECOVERY_PATH].as<string>();
						LOG_INFO() << "Writing recovery info...";
						node.GenerateRecoveryInfo(sPath.c_str());
						LOG_INFO() << "Recovery info written";
					}

					if (vm.count(cli::RECOVERY_AUTO_PATH))
					{
						node.m_Cfg.m_Recovery.m_sPathOutput = vm[cli::RECOVERY_AUTO_PATH].as<string>();
						node.m_Cfg.m_Recovery.m_Granularity = vm[cli::RECOVERY_AUTO_PERIOD].as<uint32_t>();
					}

					io::Timer::Ptr pCrashTimer;

					int nCrash = vm.count(cli::CRASH) ? vm[cli::CRASH].as<int>() : 0;
					if (nCrash)
					{
						pCrashTimer = io::Timer::create(*reactor);

						pCrashTimer->start(5000, false, [nCrash]() {
							Crash::Induce((Crash::Type) (nCrash - 1));
						});
					}

					if (vm.count(cli::MANUAL_ROLLBACK))
					{
						Height h = vm[cli::MANUAL_ROLLBACK].as<Height>();
						if (h >= Rules::HeightGenesis)
							node.get_Processor().ManualRollbackTo(h);
						else
							node.get_Processor().m_ManualSelection.ResetAndSave();

						node.RefreshCongestions();
					}

					if (vm.count(cli::MANUAL_SELECT))
					{
						node.get_Processor().m_ManualSelection.Reset();

						auto s = vm[cli::MANUAL_SELECT].as<std::string>();

						Block::SystemState::ID sid;
						auto iPos = s.find('-');
						if ((s.npos != iPos) && (s.size() > iPos + sid.m_Hash.nTxtLen))
						{
							sid.m_Height = std::stoull(s);
							sid.m_Hash.Scan(&s.front() + iPos + 1);

							node.get_Processor().ManualSelect(sid);
						}
						else
							node.get_Processor().m_ManualSelection.ResetAndSave();

						node.RefreshCongestions();
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

