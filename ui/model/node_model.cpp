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
#include <mutex>

#include "pow/external_pow.h"

#include <boost/filesystem.hpp>

using namespace beam;
using namespace beam::io;
using namespace std;

namespace 
{
    constexpr int kVerificationThreadsMaxAvailable = -1;
}

NodeModel::NodeModel()
    : m_shouldStartNode(false)
    , m_shouldTerminateModel(false)
    , m_isRunning(false)
{
}

NodeModel::~NodeModel()
{
    try
    {
        m_shouldTerminateModel = true;
        m_waiting.notify_all();
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

void NodeModel::setKdf(beam::Key::IKdf::Ptr kdf)
{
    m_pKdf = kdf;
}

void NodeModel::startNode()
{
    m_shouldStartNode = true;
    m_waiting.notify_all();
}

void NodeModel::stopNode()
{
    m_shouldStartNode = false;
    auto reactor = m_reactor.lock();
    if (reactor)
    {
        reactor->stop();
    }
}

bool NodeModel::isNodeRunning() const
{
    return m_isRunning;
}

void NodeModel::run()
{
    try
    {
        auto reactor = io::Reactor::create();
        m_reactor = reactor;// store weak ref
        io::Reactor::Scope scope(*reactor);

        mutex localMutex;

        while (!m_shouldTerminateModel)
        {
            if (!m_shouldStartNode)
            {
                unique_lock<mutex> lock(localMutex);

                while (!m_shouldStartNode && !m_shouldTerminateModel)
                {
                    m_waiting.wait(lock);
                }
            }

            if (!m_shouldTerminateModel)
            {
                try
                {
                    m_shouldStartNode = false;
                    runLocalNode();
                }
                catch (const runtime_error& ex)
                {
                    LOG_ERROR() << ex.what();
                    AppModel::getInstance()->getMessages().addMessage(tr("Failed to start node. Please check your node configuration"));
                }
            }
        }
    }
    catch (...)
    {
        LOG_ERROR() << "Unhandled exception";
    }
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

void NodeModel::runLocalNode()
{
    auto& settings = AppModel::getInstance()->getSettings();

    Node node;
    node.m_Cfg.m_Listen.port(settings.getLocalNodePort());
    node.m_Cfg.m_Listen.ip(INADDR_ANY);
    node.m_Cfg.m_sPathLocal = settings.getLocalNodeStorage();


    {
        node.m_Cfg.m_UseGpu = false;
        node.m_Cfg.m_MiningThreads = 0;

//#ifdef BEAM_USE_GPU
//        if (settings.getUseGpu())
//        {
//            node.m_Cfg.m_UseGpu = true;
//            node.m_Cfg.m_MiningThreads = 0;
//        }
//        else
//        {
//            node.m_Cfg.m_UseGpu = false;
//            node.m_Cfg.m_MiningThreads = settings.getLocalNodeMiningThreads();
//        }
//#else
//        node.m_Cfg.m_MiningThreads = settings.getLocalNodeMiningThreads();
//#endif
        node.m_Cfg.m_VerificationThreads = kVerificationThreadsMaxAvailable;
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

	struct MyObserver
		:public Node::IObserver
	{
		Node* m_pNode;
		NodeModel* m_pModel;

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

			emit m_pModel->syncProgressUpdated(static_cast<int>(s.m_Done), static_cast<int>(s.m_Total));
		}

	} obs;

	obs.m_pNode = &node;
	obs.m_pModel = this;

    node.m_Cfg.m_Observer = &obs;

    unique_ptr<IExternalPOW> stratumServer = IExternalPOW::create_opencl_solver();

    node.Initialize(stratumServer.get());

  //  std::thread minerThread(&NodeModel::runOpenclMiner, this);

	m_isRunning = true;
	emit startedNode();

	io::Reactor::get_Current().run();
    //minerThread.join();

	m_isRunning = false;
	emit stoppedNode();
}
