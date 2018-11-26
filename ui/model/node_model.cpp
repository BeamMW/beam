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
// limitations under the License

#include "node_model.h"
#include "app_model.h"
#include "node/node.h"

using namespace beam;
using namespace beam::io;
using namespace std;

NodeModel::NodeModel()
{
}

NodeModel::~NodeModel()
{
    try
    {
        {
            auto r = m_reactor.lock();
            if (!r)
            {
                return;
            }
            r->stop();

        }
        wait();
    }
    catch (...)
    {

    }
}

void NodeModel::run()
{
    try
    {
        auto reactor = io::Reactor::create();
        m_reactor = reactor;// store weak ref
        io::Reactor::Scope scope(*reactor);

        auto& settings = AppModel::getInstance()->getSettings();

        Node node;
        node.m_Cfg.m_Listen.port(settings.getLocalNodePort());
        node.m_Cfg.m_Listen.ip(INADDR_ANY);
        node.m_Cfg.m_sPathLocal = settings.getLocalNodeStorage();
        {
#ifdef BEAM_USE_GPU
            if (settings.getUseGpu())
            {
                node.m_Cfg.m_UseGpu = true;
                node.m_Cfg.m_MiningThreads = 1;
            }
            else
            {
                node.m_Cfg.m_UseGpu = false;
                node.m_Cfg.m_MiningThreads = settings.getLocalNodeMiningThreads();
            }
#else
            node.m_Cfg.m_MiningThreads = settings.getLocalNodeMiningThreads();
#endif
            node.m_Cfg.m_VerificationThreads = settings.getLocalNodeVerificationThreads();
        }

		node.m_Keys.SetSingleKey(m_pKdf);


        node.m_Cfg.m_HistoryCompression.m_sPathOutput = settings.getTempDir();
        node.m_Cfg.m_HistoryCompression.m_sPathTmp = settings.getTempDir();

        auto qPeers = settings.getLocalNodePeers();

        for (const auto& qPeer : qPeers)
        {
            Address peer_addr;
            if (peer_addr.resolve(qPeer.toStdString().c_str()))
            {
                node.m_Cfg.m_Connect.emplace_back(peer_addr);
            }
        }

        LOG_INFO() << "starting a node on " << node.m_Cfg.m_Listen.port() << " port...";

        if (settings.getGenerateGenesys())
        {
            node.m_Cfg.m_vTreasury.resize(1);
            node.m_Cfg.m_vTreasury[0].ZeroInit();
        }

        node.m_Cfg.m_Observer = this;

        node.Initialize();

        reactor->run();
    }
    catch (const runtime_error& ex)
    {
        LOG_ERROR() << ex.what();
        AppModel::getInstance()->getMessages().addMessage(tr("Failed to start node. Please check your node configuration"));
    }
    catch (...)
    {
        LOG_ERROR() << "Unhandled exception";
    }
}

void NodeModel::OnSyncProgress(int done, int total)
{
    emit syncProgressUpdated(done, total);
}
