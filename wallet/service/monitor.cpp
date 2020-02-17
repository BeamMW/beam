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

#include "monitor.h"

#include "wallet/core/wallet_network.h"
#include "wallet/core/default_peers.h"
#include "utility/logger.h"
#include "utility/cli/options.h"
#include "utility/log_rotation.h"
#include "version.h"

#include <memory>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>

using namespace beam;

namespace
{
    class Monitor
        : public proto::FlyClient
        , public proto::FlyClient::IBbsReceiver
        , public wallet::IWalletMessageConsumer
    {
        void OnWalletMessage(const wallet::WalletID& peerID, const wallet::SetTxParameter& msg) override
        {

        }

        void OnMsg(proto::BbsMsg&& msg) override
        {
            LOG_INFO() << "new bbs message on channel: " << msg.m_Channel;
        }

        Block::SystemState::IHistory& get_History() override 
        {
            return m_Headers;
        }

        Block::SystemState::HistoryMap m_Headers;
    };

    static const unsigned LOG_ROTATION_PERIOD_SEC = 3 * 60 * 60; // 3 hours
}

int main(int argc, char* argv[])
{
    using namespace beam;
    namespace po = boost::program_options;

#define LOG_FILES_DIR "logs"
#define LOG_FILES_PREFIX "monitor_"

    const auto path = boost::filesystem::system_complete(LOG_FILES_DIR);
    auto logger = beam::Logger::create(LOG_LEVEL_DEBUG, LOG_LEVEL_DEBUG, LOG_LEVEL_DEBUG, LOG_FILES_PREFIX, path.string());

    try
    {
        struct
        {
            uint16_t port;
            std::string nodeURI;
            Nonnegative<uint32_t> pollPeriod_ms;
            uint32_t logCleanupPeriod;

        } options;

        io::Address nodeAddress;
        io::Reactor::Ptr reactor = io::Reactor::create();
        {
            po::options_description desc("Wallet API general options");
            desc.add_options()
                (cli::HELP_FULL, "list of all options")
                (cli::PORT_FULL, po::value(&options.port)->default_value(8080), "port to start server on")
                (cli::NODE_ADDR_FULL, po::value<std::string>(&options.nodeURI), "address of node")
                (cli::LOG_CLEANUP_DAYS, po::value<uint32_t>(&options.logCleanupPeriod)->default_value(5), "old logfiles cleanup period(days)")
                (cli::NODE_POLL_PERIOD, po::value<Nonnegative<uint32_t>>(&options.pollPeriod_ms)->default_value(Nonnegative<uint32_t>(0)), "Node poll period in milliseconds. Set to 0 to keep connection. Anyway poll period would be no less than the expected rate of blocks if it is less then it will be rounded up to block rate value.")
                ;

            desc.add(createRulesOptionsDescription());

            po::variables_map vm;

            po::store(po::command_line_parser(argc, argv)
                .options(desc)
                .style(po::command_line_style::default_style ^ po::command_line_style::allow_guessing)
                .run(), vm);

            if (vm.count(cli::HELP))
            {
                std::cout << desc << std::endl;
                return 0;
            }

            {
                std::ifstream cfg("monitor.cfg");

                if (cfg)
                {
                    po::store(po::parse_config_file(cfg, desc), vm);
                }
            }

            vm.notify();

            clean_old_logfiles(LOG_FILES_DIR, LOG_FILES_PREFIX, options.logCleanupPeriod);

            getRulesOptions(vm);

            Rules::get().UpdateChecksum();
            LOG_INFO() << "Beam Wallet SBBS Monitor " << PROJECT_VERSION << " (" << BRANCH_NAME << ")";
            LOG_INFO() << "Rules signature: " << Rules::get().get_SignatureStr();

            if (vm.count(cli::NODE_ADDR) == 0)
            {
                LOG_ERROR() << "node address should be specified";
                return -1;
            }

            if (!nodeAddress.resolve(options.nodeURI.c_str()))
            {
                LOG_ERROR() << "unable to resolve node address: `" << options.nodeURI << "`";
                return -1;
            }
        }

        io::Reactor::Scope scope(*reactor);
        io::Reactor::GracefulIntHandler gih(*reactor);

        LogRotation logRotation(*reactor, LOG_ROTATION_PERIOD_SEC, options.logCleanupPeriod);

        LOG_INFO() << "Starting server on port " << options.port;
        
        Monitor monitor;
        proto::FlyClient::NetworkStd nnet(monitor);
        nnet.m_Cfg.m_vNodes.push_back(nodeAddress);
        nnet.Connect();
 
        for (BbsChannel c = 0; c < 1024; ++c)
        {
            nnet.BbsSubscribe(c, getTimestamp(), &monitor);
        }

        reactor->run();

        LOG_INFO() << "Done";
    }
    catch (const std::exception & e)
    {
        LOG_ERROR() << "EXCEPTION: " << e.what();
    }
    catch (...)
    {
        LOG_ERROR() << "NON_STD EXCEPTION";
    }

    return 0;
}
