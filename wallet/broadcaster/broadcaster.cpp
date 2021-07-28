// Copyright 2020 The Beam Team
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
#include "broadcaster.h"
#include "version.h"
#include "utility/cli/options.h"
#include "utility/log_rotation.h"
#include "utility/string_helpers.h"
#include "mnemonic/mnemonic.h"
#include "core/ecc.h"
#include "keykeeper/local_private_key_keeper.h"
#include "wallet/core/wallet_network.h"
#include "wallet/client/extensions/broadcast_gateway/broadcast_router.h"
#include "wallet/client/extensions/broadcast_gateway/broadcast_msg_creator.h"
#include "wallet/client/extensions/news_channels/interface.h"

#ifndef LOG_VERBOSE_ENABLED
    #define LOG_VERBOSE_ENABLED 0
#endif

#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>

using namespace beam;
using namespace beam::wallet;

constexpr unsigned LOG_ROTATION_PERIOD_SEC = 3*60*60; // 3 hours
constexpr std::string_view TEMP_DB_FILE_NAME = "temp_wallet.db";

io::Reactor::Ptr reactor;

WalletID channelToWalletID(BbsChannel channel)
{
    WalletID dummyWalletID;
    dummyWalletID.m_Channel = channel;
    return dummyWalletID;
}

bool parseUpdateInfo(const std::string& versionString, const std::string& typeString, VersionInfo& result)
{
    Version version;
    bool res = version.from_string(versionString);
    if (!res)
    {
        return false;
    }

    VersionInfo::Application appType = VersionInfo::from_string(typeString);
    if (appType == VersionInfo::Application::Unknown)
    {
        return false;
    }
    
    result.m_application = appType;
    result.m_version = version;
    return true;
}

bool parseWalletUpdateInfo(const std::string& versionString, const std::string& typeString, WalletImplVerInfo& walletVersionInfo)
{
    VersionInfo::Application appType = VersionInfo::from_string(typeString);
    if (appType == VersionInfo::Application::Unknown) return false;

    Version libVersion = {0,0,0};
    uint32_t uiRevision = 0;
    if (appType == VersionInfo::Application::DesktopWallet)
    {
        auto lastDot = versionString.find_last_of(".");
        if (lastDot == std::string::npos) return false;
        if (bool res = libVersion.from_string(versionString.substr(0, lastDot)); !res) return false;
        auto uiRevisionStr = versionString.substr(lastDot + 1, versionString.length());

        size_t processed = 0;
        uiRevision = std::stoul(uiRevisionStr, &processed);
        if (processed != uiRevisionStr.length()) return false;
    }
    else if (appType == VersionInfo::Application::AndroidWallet ||
             appType == VersionInfo::Application::IOSWallet)
    {
        if (bool res = libVersion.from_string(versionString); !res) return false;
        uiRevision = libVersion.m_revision;
        libVersion.m_revision = 0;
    }
    else
    {
        return false;
    }

    walletVersionInfo.m_application = appType;
    walletVersionInfo.m_version = libVersion;
    walletVersionInfo.m_UIrevision = uiRevision;
    walletVersionInfo.m_title = "New version";

    switch (appType)
    {
        case VersionInfo::Application::DesktopWallet:
            walletVersionInfo.m_message = "Beam Wallet UI " + versionString;
            break;
        case VersionInfo::Application::AndroidWallet:
            walletVersionInfo.m_message = "Android Wallet " + versionString;
            break;
        case VersionInfo::Application::IOSWallet:
            walletVersionInfo.m_message = "IOS Wallet " + versionString;
            break;
        default:
            assert(false);
    }
    
    return true;
}

bool parseExchangeRateInfo(const std::string& from, const std::string& to, const Amount& rate, std::vector<ExchangeRate>& result)
{
    if (!rate)
    {
        return false;
    }

    result = {{Currency(from),  Currency(to), rate, getTimestamp()}};
    return true;
}

ByteBuffer generateExchangeRates(const std::string& from, const std::string& to, const Amount& rate)
{
    std::vector<ExchangeRate> result;
    if (parseExchangeRateInfo(from, to, rate, result))
    {
        return toByteBuffer(result);
    }
    return ByteBuffer();
}

namespace
{
    struct Options
    {
        std::string walletPath;
        std::string nodeURI;
        Nonnegative<uint32_t> pollPeriod_ms;
        uint32_t logCleanupPeriod;

        std::string privateKey;
        std::string messageType;

        struct WalletUpdateInfo {
            std::string version;
            std::string walletType;
        } walletUpdateInfo;

        struct ExchangeRate {
            std::string  from;
            std::string  to;
            beam::Amount rate;
        } exchangeRate;
    };

    class MyFlyClient 
        : public proto::FlyClient
        , public wallet::IWalletMessageConsumer
    {
        void OnWalletMessage(const WalletID& peerID, const SetTxParameter&) override
        {
        }

        Block::SystemState::IHistory& get_History() override
        {
            return m_Headers;
        }

        Block::SystemState::HistoryMap m_Headers;
    };

    struct MyTimestampHolder : ITimestampHolder
    {
        Timestamp GetTimestamp(BbsChannel channel) override
        {
            return 0;
        }
        void UpdateTimestamp(const proto::BbsMsg& msg) override
        {

        }
    };

    class MyBbsSender
        : public IWalletMessageEndpoint
        , private wallet::BbsProcessor
    {
    public:
        using BbsProcessor::BbsProcessor;

        void Send(const WalletID& peerID, const SetTxParameter& msg) override
        {
        
        }
        
        void SendRawMessage(const WalletID& peerID, const ByteBuffer& msg) override
        {
            BbsProcessor::Send(peerID, msg, 0);
        }

        void OnMessageSent(uint64_t id) override
        {
            io::Reactor::get_Current().stop();
        }
    };

    bool generateKeyPair(ECC::Scalar::Native& sk, PeerID& pk)
    {
        std::string temp_db_file_name(TEMP_DB_FILE_NAME);
        if (WalletDB::isInitialized(temp_db_file_name))
        {
            boost::filesystem::remove(temp_db_file_name);
        }

        SecString pass("tempPass"); // TODO: random password should be used

        WordList phrase;
        phrase = createMnemonic(getEntropy());
        BOOST_ASSERT(phrase.size() == 12);

        auto buf = decodeMnemonic(phrase);
        SecString seed;
        seed.assign(buf.data(), buf.size());

        ECC::NoLeak<ECC::uintBig> walletSeed;
        walletSeed.V = seed.hash().V;
  
        auto walletDB = WalletDB::init(temp_db_file_name, pass, walletSeed);
        if (walletDB)
        {
            walletDB->get_SbbsPeerID(sk, pk, 1);
            return true;
        }
        
        return false;
    }

    int generateKeysCommand(const po::variables_map& vm, const Options& options)
    {
        ECC::Scalar::Native sk;
        PeerID pk;

        LOG_INFO() << "Key generation requested";

        if (!generateKeyPair(sk, pk))
        {
            LOG_ERROR() << "key generation error";
            return -1;
        }

        ECC::Scalar skToPrint;
        sk.Export(skToPrint);
        std::cout << "Private key: " << skToPrint.str() << std::endl;
        std::cout << "Public key: " << pk.str() << std::endl;

        LOG_INFO() << "Key pair successfully generated";
        boost::filesystem::remove(std::string(TEMP_DB_FILE_NAME));

        return 0;
    }

    int transmitCommand(const po::variables_map& vm, const Options& options)
    {
        LOG_INFO() << "Message transmit requested";

        if (vm.count(cli::NODE_ADDR) == 0)
        {
            LOG_ERROR() << "node address should be specified";
            return -1;
        }

        if (vm.count(cli::MESSAGE_TYPE) == 0)
        {
            LOG_ERROR() << "message type has to be specified";
            return -1;
        }

        if (vm.count(cli::PRIVATE_KEY) == 0)
        {
            LOG_ERROR() << "private key has to be specified";
            return -1;
        }

        io::Address nodeAddress;
        if (!nodeAddress.resolve(options.nodeURI.c_str()))
        {
            LOG_ERROR() << "unable to resolve node address: " << options.nodeURI;
            return -1;
        }

        MyFlyClient client;
        auto nnet = std::make_shared<proto::FlyClient::NetworkStd>(client);

        nnet->m_Cfg.m_PollPeriod_ms = options.pollPeriod_ms.value;
        
        if (nnet->m_Cfg.m_PollPeriod_ms)
        {
            LOG_INFO() << "Node poll period = " << nnet->m_Cfg.m_PollPeriod_ms << " ms";
            uint32_t timeout_ms = std::max(Rules::get().DA.Target_s * 1000, nnet->m_Cfg.m_PollPeriod_ms);
            if (timeout_ms != nnet->m_Cfg.m_PollPeriod_ms)
            {
                LOG_INFO() << "Node poll period has been automatically rounded up to block rate: " << timeout_ms << " ms";
            }
        }
        uint32_t responceTime_s = Rules::get().DA.Target_s * wallet::kDefaultTxResponseTime;
        if (nnet->m_Cfg.m_PollPeriod_ms >= responceTime_s * 1000)
        {
            LOG_WARNING() << "The \"--node_poll_period\" parameter set to more than " << uint32_t(responceTime_s / 3600) << " hours may cause transaction problems.";
        }
        nnet->m_Cfg.m_vNodes.push_back(nodeAddress);
        nnet->Connect();

        auto tsHolder = std::make_shared<MyTimestampHolder>();
        MyBbsSender wnet(nnet, tsHolder);

        BroadcastRouter broadcastRouter(nnet, wnet, tsHolder);

        ECC::Scalar::Native key;
        if (!BroadcastMsgCreator::stringToPrivateKey(options.privateKey, key))
        {
            LOG_ERROR() << "Invalid private key.";
            return -1;
        }

        ByteBuffer rawMessage;
        BroadcastContentType contentType;
        WalletImplVerInfo walletVersionInfo;
        if (options.messageType == "update")
        {
            bool res =
                parseWalletUpdateInfo(options.walletUpdateInfo.version, options.walletUpdateInfo.walletType, walletVersionInfo);
            if (res)
                rawMessage = toByteBuffer(walletVersionInfo);
            contentType = BroadcastContentType::WalletUpdates;
        }
        else if (options.messageType == "exchange")
        {
            rawMessage = generateExchangeRates(options.exchangeRate.from, options.exchangeRate.to, options.exchangeRate.rate);
            contentType = BroadcastContentType::ExchangeRates;
        }
        else
        {
            LOG_ERROR() << "Invalid type of message: " << options.messageType;
            return -1;
        }

        if (!rawMessage.empty())
        {
            BroadcastMsg msg = BroadcastMsgCreator::createSignedMessage(rawMessage, key);
            broadcastRouter.sendMessage(contentType, msg);

            // broadcast for 4.2 desktop ui // deprecated
            if (contentType == BroadcastContentType::WalletUpdates &&
                walletVersionInfo.m_application == VersionInfo::Application::DesktopWallet)
            {
                VersionInfo versionInfo;
                versionInfo.m_application = walletVersionInfo.m_application;
                versionInfo.m_version = walletVersionInfo.m_version;

                auto rawMessageOldStyle = toByteBuffer(versionInfo);
                BroadcastMsg msgOldStyle = BroadcastMsgCreator::createSignedMessage(rawMessageOldStyle, key);
                broadcastRouter.sendMessage(BroadcastContentType::SoftwareUpdates, msgOldStyle);
            }
        }
        else
        {
            LOG_ERROR() << "Invalid message parameters";
            return -1;
        }

        io::Reactor::get_Current().run();
        return 0;
    }
}

int main_impl(int argc, char* argv[])
{
    using namespace beam;
    namespace po = boost::program_options;

    const auto path = boost::filesystem::system_complete("./logs");
    auto logger = beam::Logger::create(LOG_LEVEL_DEBUG, LOG_LEVEL_DEBUG, LOG_LEVEL_DEBUG, "broadcast_", path.string());

    try
    {
        Options options;
        po::variables_map vm;

        {
            po::options_description desc("General options");
            desc.add_options()
                (cli::HELP_FULL, "list of all options")
                (cli::NODE_ADDR_FULL, po::value<std::string>(&options.nodeURI), "address of node")
                (cli::LOG_CLEANUP_DAYS, po::value<uint32_t>(&options.logCleanupPeriod)->default_value(5), "old logfiles cleanup period(days)")
                (cli::NODE_POLL_PERIOD, po::value<Nonnegative<uint32_t>>(&options.pollPeriod_ms)->default_value(Nonnegative<uint32_t>(0)), "Node poll period in milliseconds. Set to 0 to keep connection. Anyway poll period would be no less than the expected rate of blocks if it is less then it will be rounded up to block rate value.")
                (cli::COMMAND, po::value<std::string>(), "command to execute [generate_keys|transmit]")
                (cli::CONFIG_FILE_PATH, po::value<std::string>()->default_value("bbs.cfg"), "path to the config file")
            ;

            po::options_description messageDesc("Broadcast message options");
            messageDesc.add_options()
                (cli::PRIVATE_KEY, po::value<std::string>(&options.privateKey), "private key to sign message")
                (cli::MESSAGE_TYPE, po::value<std::string>(&options.messageType), "type of message: 'update' - info about available software updates, 'exchange' - info about current exchange rates")
                (cli::UPDATE_VERSION, po::value<std::string>(&options.walletUpdateInfo.version), "available software version in format 'x.x.x.x' for desktop or 'x.x.x' for IOS and Android")
                (cli::UPDATE_TYPE, po::value<std::string>(&options.walletUpdateInfo.walletType), "updated software: 'desktop', 'android', 'ios'")
#if defined(BITCOIN_CASH_SUPPORT)
                (cli::EXCHANGE_CURR, po::value<std::string>(&options.exchangeRate.currency), "currency: 'beam', 'btc', 'ltc', 'qtum', 'doge', 'dash', 'ethereum', 'dai', 'usdt', 'wbtc', 'bch'")
#else
                (cli::EXCHANGE_CURR, po::value<std::string>(&options.exchangeRate.from), "currency: 'beam', 'btc', 'ltc', 'qtum', 'doge', 'dash', 'ethereum', 'dai', 'usdt', 'wbtc', and so on")
#endif // BITCOIN_CASH_SUPPORT)
                (cli::EXCHANGE_RATE, po::value<Amount>(&options.exchangeRate.rate), "exchange rate in decimal format: 100,000,000 = 1 usd")
                (cli::EXCHANGE_UNIT, po::value<std::string>(&options.exchangeRate.to)->default_value("usd"), "unit currency: 'btc', 'usd'")
            ;
            
            desc.add(messageDesc);
            desc.add(createRulesOptionsDescription());

            po::store(po::command_line_parser(argc, argv)
                .options(desc)
                .style(po::command_line_style::default_style ^ po::command_line_style::allow_guessing)
                .run(), vm);

            if (vm.count(cli::HELP))
            {
                std::cout << desc << std::endl;
                return 0;
            }

            ReadCfgFromFileCommon(vm, desc);
            ReadCfgFromFile(vm, desc);
        }

        vm.notify();
        getRulesOptions(vm);

        reactor = io::Reactor::create();
        io::Reactor::Scope scope(*reactor);
        io::Reactor::GracefulIntHandler gih(*reactor);

        LogRotation logRotation(*reactor, LOG_ROTATION_PERIOD_SEC, options.logCleanupPeriod);

        Rules::get().UpdateChecksum();
        LOG_INFO() << "Broadcasting utility " << PROJECT_VERSION << " (" << BRANCH_NAME << ")";
        LOG_INFO() << "Rules signature: " << Rules::get().get_SignatureStr();

        if (vm.count(cli::COMMAND) == 0)
        {
            return transmitCommand(vm, options);
        }
        else
        {
            auto command = vm[cli::COMMAND].as<std::string>();
            
            if (command == cli::GENERATE_KEYS)
            {
                generateKeysCommand(vm, options); 
            }
            else if (command == cli::TRANSMIT)
            {
                return transmitCommand(vm, options);
            }
            else
            {
                LOG_ERROR() << "unknown command: " << command;
                return -1;
            }
        }

        LOG_INFO() << "Done";
    }
    catch (const std::exception& e)
    {
        LOG_ERROR() << "EXCEPTION: " << e.what();
    }
    catch (...)
    {
        LOG_ERROR() << "NON_STD EXCEPTION";
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
