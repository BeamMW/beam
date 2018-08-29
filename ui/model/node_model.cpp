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
#include "beam/node.h"

using namespace beam;
using namespace beam::io;
using namespace std;

NodeModel::NodeModel(const ECC::NoLeak<ECC::uintBig>& seed)
    : m_seed{seed}
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
    auto reactor = io::Reactor::create();
    m_reactor = reactor;// store weak ref
    io::Reactor::Scope scope(*reactor);
    
    auto& settings = AppModel::getInstance()->getSettings();

    Node node;
    node.m_Cfg.m_Listen.port(settings.getLocalNodePort());
    node.m_Cfg.m_Listen.ip(INADDR_ANY);
    node.m_Cfg.m_sPathLocal = settings.getLocalNodeStorage();
    node.m_Cfg.m_MiningThreads = settings.getLocalNodeMiningThreads();
   
    node.m_Cfg.m_VerificationThreads = settings.getLocalNodeVerificationThreads();

    node.m_Cfg.m_WalletKey = m_seed;


    node.m_Cfg.m_HistoryCompression.m_sPathOutput = settings.getTempDir();
    node.m_Cfg.m_HistoryCompression.m_sPathTmp = settings.getTempDir();

    LOG_INFO() << "starting a node on " << node.m_Cfg.m_Listen.port() << " port...";

    node.Initialize();

    reactor->run();

}