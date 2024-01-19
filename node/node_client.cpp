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

            if (!boost::filesystem::exists(appDataPath) || 
                !boost::filesystem::exists(nodePath))
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
        catch (std::exception& e)
        {
            LOG_ERROR() << e.what();
        }
    }
}

namespace beam
{
    NodeClient::NodeClient(const Rules& rules, INodeClientObserver* observer)
        : m_rules(rules)
        , m_observer(observer)
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
                {
                    auto r = m_reactor.lock();
                    if (r)
                    {
                        r->stop();
                    }
                }
                {
                    if (m_thread && m_thread->joinable())
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

    void NodeClient::setBeforeStartAction(std::function<void()> action)
    {
        m_beforeStartAction = std::move(action);
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
        std::unique_lock<std::mutex> lock(m_startMutex);
        m_shouldStartNode = true;
        m_waiting.notify_one();
    }

    void NodeClient::stopNode()
    {
        {
            std::unique_lock<std::mutex> lock(m_startMutex);
            m_shouldStartNode = false;
            m_waiting.notify_one();
        }
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
                if (m_beforeStartAction)
                {
                    m_beforeStartAction();
                }
                removeNodeDataIfNeeded(m_observer->getLocalNodeStorage());
                Rules::Scope scopeRules(m_rules);
                auto reactor = io::Reactor::create();
                m_reactor = reactor;// store weak ref
                io::Reactor::Scope scope(*reactor);

                while (!m_shouldTerminateModel)
                {
                    {
                        std::unique_lock<std::mutex> lock(m_startMutex);
                        m_waiting.wait(lock, [&]() {return m_shouldStartNode || m_shouldTerminateModel; });
                        m_shouldStartNode = false;
                    }

                    if (!m_shouldTerminateModel)
                    {
                        bool bErr = true;
                        bool recreate = false;
                        try
                        {
                            runLocalNode();
                            bErr = false;
                        }
                        catch (const io::Exception& ex)
                        {
                            LOG_ERROR() << ex.what();
                            m_observer->onFailedToStartNode(ex.errorCode);
                            bErr = false;
                            recreate = true;
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

                        if (recreate)
                        {
                            setRecreateTimer(); // attempt to start again
                        }
                    }
                }
            }
            catch (const std::exception& e)
            {
                LOG_UNHANDLED_EXCEPTION() << "what = " << e.what();
            }
            // commented intentionally to be able to catch crash
            //catch (...)
            //{
            //    LOG_UNHANDLED_EXCEPTION();
            //}

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
            node.m_Cfg.m_PeersPersistent = m_observer->getPeersPersistent();

            if (m_pKdf)
            {
                node.m_Keys.SetSingleKey(m_pKdf);
            }
            else if (m_ownerKey)
            {
                node.m_Keys.m_pOwner = m_ownerKey;
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

            class MyObserver final : public Node::IObserver, public ILongAction
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

                    AdjustProgress(s.m_Done, s.m_Total);
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

                ILongAction* GetLongActionHandler() override
                {
                    return this;
                }

                void Reset(const char* sz, uint64_t nTotal) override
                {
                    SetTotal(nTotal);
                    m_Last_ms = GetTime_ms();
                }

                void SetTotal(uint64_t nTotal) override
                {
                    m_Total = nTotal;
                }

                bool OnProgress(uint64_t pos) override
                {
                    if (m_model.m_shouldTerminateModel)
                    {
                        return false;
                    }
                    uint32_t dt_ms = GetTime_ms() - m_Last_ms;
                    const uint32_t nWindow_ms = 1000; // 1 sec
                    uint32_t n = dt_ms / nWindow_ms;
                    if (n)
                    {
                        m_Last_ms += n * nWindow_ms;
                        uint64_t total = m_Total;
                        AdjustProgress(pos, total);
                        m_model.m_observer->onSyncProgressUpdated(static_cast<int>(pos), static_cast<int>(total));
                    }
                    return true;
                }
            private:
                void AdjustProgress(uint64_t& done, uint64_t& total)
                {
                    // make sure no overflow during conversion from SyncStatus to int,int.
                    constexpr auto threshold = static_cast<unsigned int>(std::numeric_limits<int>::max());
                    while (total > threshold)
                    {
                        total >>= 1;
                        done >>= 1;
                    }
                }

            private:
                Node& m_node;
                NodeClient& m_model;
                Height m_Done0 = MaxHeight;
                uint64_t m_Total = 0;
                uint32_t m_Last_ms = 0;
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

    void NodeClient::setRecreateTimer()
    {
        if (!m_timer)
        {
            m_timer = io::Timer::create(io::Reactor::get_Current());
        }
        m_timer->start(5000, false, [this]()
        {
            io::Reactor::get_Current().stop();
            startNode();
        });
        io::Reactor::get_Current().run();

    }

}