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
#include <mutex>
#include "pow/external_pow.h"
#include "utility/logger.h"
#include <boost/filesystem.hpp>

namespace
{
    constexpr int kVerificationThreadsMaxAvailable = -1;

    boost::filesystem::path pathFromStdString(const std::string& path)
    {
#ifdef WIN32
        boost::filesystem::path boostPath{ beam::Utf8toUtf16(path.c_str()) };
#else
        boost::filesystem::path boostPath{ path };
#endif
        return boostPath;
    }

    void removeNodeDataIfNeeded(const std::string& nodePathStr)
    {
        try
        {
            auto nodePath = pathFromStdString(nodePathStr);
            auto appDataPath = nodePath.parent_path();

            if (!boost::filesystem::exists(appDataPath))
            {
                return;
            }
            try
            {
                beam::NodeDB nodeDB;
                nodeDB.Open(nodePathStr.c_str());
                return;
            }
            catch (const beam::NodeDBUpgradeException&)
            {
            }

            boost::filesystem::remove(nodePath);

            std::vector<boost::filesystem::path> macroBlockFiles;
            for (boost::filesystem::directory_iterator endDirIt, it{ appDataPath }; it != endDirIt; ++it)
            {
                if (it->path().filename().wstring().find(L"tempmb") == 0)
                {
                    macroBlockFiles.push_back(it->path());
                }
            }

            for (auto& path : macroBlockFiles)
            {
                boost::filesystem::remove(path);
            }
        }
        catch (std::exception & e)
        {
            LOG_ERROR() << e.what();
        }
    }
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
        LOG_UNHANDLED_EXCEPTION() << "what = " << e.what();
    }
    catch (...) {
        LOG_UNHANDLED_EXCEPTION();
    }
}

void NodeClient::setKdf(beam::Key::IKdf::Ptr kdf)
{
    m_pKdf = kdf;
}

void NodeClient::setOwnerKey(beam::Key::IPKdf::Ptr key)
{
    m_ownerKey = key;
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
            removeNodeDataIfNeeded(m_observer->getLocalNodeStorage());

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
                    catch (const io::Exception& ex)
                    {
                        LOG_ERROR() << ex.what();
                        m_observer->onFailedToStartNode(ex.errorCode);
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
                        m_observer->onSyncError(Node::IObserver::Error::Unknown);
                }
            }
        }
        catch (const std::exception& e)
        {
            LOG_UNHANDLED_EXCEPTION() << "what = " << e.what();
        }
        catch (...) {
            LOG_UNHANDLED_EXCEPTION();
        }

        m_observer->onNodeThreadFinished();
    });
}

bool NodeClient::isNodeRunning() const
{
    return m_isRunning;
}

void NodeClient::runLocalNode()
{
    class ScopedNotifier final {
    public:
        explicit ScopedNotifier(INodeClientObserver& observer)
            : _observer(observer)
            , _nodeCreated(false)
        {}

        void notifyNodeCreated()
        {
            assert(!_nodeCreated);
            _nodeCreated = true;
            _observer.onNodeCreated();
        }

        ~ScopedNotifier()
        {
            if (_nodeCreated)
            {
                _observer.onNodeDestroyed();
            }
        }
    private:
        INodeClientObserver& _observer;
        bool _nodeCreated = false;
    } notifier(*m_observer);

    // Scope, just for clarity. Notifier created above
    // should be destroyed the last
    {
        Node node;
        node.m_Cfg.m_Listen.port(m_observer->getLocalNodePort());
        node.m_Cfg.m_Listen.ip(INADDR_ANY);
        node.m_Cfg.m_sPathLocal = m_observer->getLocalNodeStorage();
        node.m_Cfg.m_MiningThreads = 0;
        node.m_Cfg.m_VerificationThreads = kVerificationThreadsMaxAvailable;

        if(m_ownerKey)
        {
            node.m_Keys.m_pOwner = m_ownerKey;
        }
        else
        {
            node.m_Keys.SetSingleKey(m_pKdf);
        }

		node.m_Cfg.m_Horizon.SetStdFastSync();

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

        class MyObserver final: public Node::IObserver
        {
        public:
            MyObserver(Node& node, NodeClient& model)
                : m_node(node)
                , m_model(model)
            {
                assert(m_model.m_observer);
            }

            ~MyObserver()
            {
                assert(m_model.m_observer);
                if (m_reportedStarted) m_model.m_observer->onStoppedNode();
            }

            void OnSyncProgress() override
            {
                Node::SyncStatus s = m_node.m_SyncStatus;

				if (MaxHeight == m_Done0)
					m_Done0 = s.m_Done;
				s.ToRelative(m_Done0);

                if (!m_reportedStarted && (s.m_Done == s.m_Total))
                {
                    m_reportedStarted = true;
                    m_model.m_observer->onStartedNode();
                }

                // make sure no overflow during conversion from SyncStatus to int,int.
                const auto threshold = static_cast<unsigned int>(std::numeric_limits<int>::max());
                while (s.m_Total > threshold)
                {
                    s.m_Total >>= 1;
                    s.m_Done >>= 1;
                }

                m_model.m_observer->onSyncProgressUpdated(static_cast<int>(s.m_Done), static_cast<int>(s.m_Total));
            }

            void OnSyncError(Node::IObserver::Error error) override
            {
                m_model.m_observer->onSyncError(error);
            }

            void InitializeUtxosProgress(uint64_t done, uint64_t total) override
            {
                m_model.m_observer->onInitProgressUpdated(done, total);
            }

        private:
            Node& m_node;
            NodeClient& m_model;
			Height m_Done0 = MaxHeight;
            bool m_reportedStarted = false;
        } obs(node, *this);

        node.m_Cfg.m_Observer = &obs;
        node.Initialize();
        notifier.notifyNodeCreated();

        if (node.get_AcessiblePeerCount() == 0)
        {
            throw std::runtime_error("Resolved peer list is empty");
        }

        m_isRunning = true;
        io::Reactor::get_Current().run();
        m_isRunning = false;
    }
}
}
