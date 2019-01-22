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

#include "node_client.h"

#include "node/node.h"
#include <mutex>

#include "pow/external_pow.h"
#include "utility/logger.h"

#include <boost/filesystem.hpp>
#ifdef  BEAM_USE_GPU
#include "utility/gpu/gpu_tools.h"
#endif //  BEAM_USE_GPU

namespace
{
    constexpr int kVerificationThreadsMaxAvailable = -1;
}

namespace beam
{
NodeClient::NodeClient(INodeClientObserver* observer)
    : m_observer(observer)
    , m_shouldStartNode(false)
    , m_shouldTerminateModel(false)
    , m_isRunning(false)
{
}

NodeClient::~NodeClient()
{
    try
    {
        m_shouldTerminateModel = true;
        m_waiting.notify_all();
        {
            auto r = m_reactor.lock();
            if (r)
            {
                r->stop();
                if (m_thread)
                {
                    // TODO: check this
                    m_thread->join();
                }
            }
        }
    }
    catch (const std::exception& e)
    {
        LOG_UNHANDLE_EXCEPTION() << "what = " << e.what();
    }
    catch (...) {
        LOG_UNHANDLE_EXCEPTION();
    }
}

void NodeClient::setKdf(beam::Key::IKdf::Ptr kdf)
{
    m_pKdf = kdf;
}

void NodeClient::startNode()
{
    m_shouldStartNode = true;
    m_waiting.notify_all();
}

void NodeClient::stopNode()
{
    m_shouldStartNode = false;
    auto reactor = m_reactor.lock();
    if (reactor)
    {
        reactor->stop();
    }
}

void NodeClient::start()
{
    m_thread = std::make_shared<std::thread>([this]()
    {
        try
        {
            auto reactor = io::Reactor::create();
            m_reactor = reactor;// store weak ref
            io::Reactor::Scope scope(*reactor);

            std::mutex localMutex;

            while (!m_shouldTerminateModel)
            {
                if (!m_shouldStartNode)
                {
                    std::unique_lock<std::mutex> lock(localMutex);

                    while (!m_shouldStartNode && !m_shouldTerminateModel)
                    {
                        m_waiting.wait(lock);
                    }
                }

                if (!m_shouldTerminateModel)
                {
                    bool bErr = true;
                    try
                    {
                        m_shouldStartNode = false;
                        runLocalNode();
                        bErr = false;
                    }
                    catch (const std::runtime_error& ex)
                    {
                        LOG_ERROR() << ex.what();
                    }
                    catch (const CorruptionException& ex)
                    {
                        LOG_ERROR() << "Corruption: " << ex.m_sErr;
                    }

                    if (bErr)
                        m_observer->onFailedToStartNode();
                }
            }
        }
        catch (const std::exception& e)
        {
            LOG_UNHANDLE_EXCEPTION() << "what = " << e.what();
        }
        catch (...) {
            LOG_UNHANDLE_EXCEPTION();
        }
    });
}

bool NodeClient::isNodeRunning() const
{
    return m_isRunning;
}

void NodeClient::runLocalNode()
{
#ifdef BEAM_USE_GPU
    std::unique_ptr<IExternalPOW> stratumServer = m_observer->getStratumServer();
#endif //  BEAM_USE_GPU
    Node node;
    node.m_Cfg.m_Listen.port(m_observer->getLocalNodePort());
    node.m_Cfg.m_Listen.ip(INADDR_ANY);
    node.m_Cfg.m_sPathLocal = m_observer->getLocalNodeStorage();

    {
#ifdef BEAM_USE_GPU
        node.m_Cfg.m_MiningThreads = 0;
#else
        node.m_Cfg.m_MiningThreads = m_observer->getLocalNodeMiningThreads();
#endif //  BEAM_USE_GPU
        node.m_Cfg.m_VerificationThreads = kVerificationThreadsMaxAvailable;
    }

    node.m_Keys.SetSingleKey(m_pKdf);

    node.m_Cfg.m_HistoryCompression.m_sPathOutput = m_observer->getTempDir();
    node.m_Cfg.m_HistoryCompression.m_sPathTmp = m_observer->getTempDir();

    auto peers = m_observer->getLocalNodePeers();

    for (const auto& peer : peers)
    {
        io::Address peer_addr;
        if (peer_addr.resolve(peer.c_str()))
        {
            node.m_Cfg.m_Connect.emplace_back(peer_addr);
        }
        else
        {
            LOG_ERROR() << "Unable to resolve node address: " << peer;
        }
    }

    LOG_INFO() << "starting a node on " << node.m_Cfg.m_Listen.port() << " port...";

    struct MyObserver
        :public Node::IObserver
    {
        Node* m_pNode;
        NodeClient* m_pModel;

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

            m_pModel->m_observer->onSyncProgressUpdated(static_cast<int>(s.m_Done), static_cast<int>(s.m_Total));
        }

    } obs;

    obs.m_pNode = &node;
    obs.m_pModel = this;

    node.m_Cfg.m_Observer = &obs;

#ifdef BEAM_USE_GPU
    node.Initialize(stratumServer.get());
#else
    node.Initialize();
#endif //  BEAM_USE_GPU

    m_isRunning = true;
    m_observer->onStartedNode();

    io::Reactor::get_Current().run();

    m_isRunning = false;
    m_observer->onStoppedNode();
}
}