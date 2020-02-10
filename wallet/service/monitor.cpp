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
}

int main(int argc, char* argv[])
{
#define LOG_FILES_DIR "logs"
#define LOG_FILES_PREFIX "monitor_"

    const auto path = boost::filesystem::system_complete(LOG_FILES_DIR);
    auto logger = beam::Logger::create(LOG_LEVEL_DEBUG, LOG_LEVEL_DEBUG, LOG_LEVEL_DEBUG, LOG_FILES_PREFIX, path.string());

    Rules::get().UpdateChecksum();

    auto reactor = io::Reactor::create();
    io::Reactor::Scope scope(*reactor);
    io::Reactor::GracefulIntHandler gih(*reactor);


    Monitor monitor;
    proto::FlyClient::NetworkStd nnet(monitor);

    for (const auto& peer : getDefaultPeers())
    {
        io::Address nodeAddress;
        if (nodeAddress.resolve(peer.c_str()))
        {
            nnet.m_Cfg.m_vNodes.push_back(nodeAddress);
        }
    }

    nnet.Connect();
    for (BbsChannel c = 0; c < 1024; ++c)
    {
        nnet.BbsSubscribe(c, getTimestamp(), &monitor);
    }

    LOG_INFO() << "Beam Wallet SBBS Monitor " << PROJECT_VERSION << " (" << BRANCH_NAME << ")";
    LOG_INFO() << "Rules signature: " << Rules::get().get_SignatureStr();

    reactor->run();

    return 0;
}
