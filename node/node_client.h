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

#pragma once

#include <thread>
#include <memory>
#include <atomic>
#include <condition_variable>
#include "node/node.h"
#include "core/block_crypt.h"
#include "utility/io/errorhandling.h"
#include "utility/io/reactor.h"

namespace beam
{
    class INodeClientObserver
    {
    public:
        virtual void onNodeCreated() = 0;
        virtual void onNodeDestroyed() = 0;
        virtual void onInitProgressUpdated(uint64_t done, uint64_t total) = 0;
        virtual void onSyncProgressUpdated(int done, int total) = 0;
        virtual void onStartedNode() = 0;
        virtual void onStoppedNode() = 0;
        virtual void onFailedToStartNode(io::ErrorCode errorCode) = 0;
        virtual void onSyncError(Node::IObserver::Error error) = 0;

        virtual uint16_t getLocalNodePort() const = 0;
        virtual std::string getLocalNodeStorage() const = 0;
        virtual std::string getTempDir() const = 0;
        virtual std::vector<std::string> getLocalNodePeers() const = 0;
        virtual bool getPeersPersistent() const = 0;

        virtual void onNodeThreadFinished() = 0;
    };

    class NodeClient
    {
    public:
        NodeClient(const Rules& rules, INodeClientObserver* observer);
        ~NodeClient();
        void setBeforeStartAction(std::function<void()> action);
        void setKdf(beam::Key::IKdf::Ptr);
        void setOwnerKey(beam::Key::IPKdf::Ptr);
        void startNode();
        void stopNode();

        void start();

        bool isNodeRunning() const;

    private:
        void runLocalNode();
        void setRecreateTimer();

    private:
        const Rules& m_rules;
        INodeClientObserver* m_observer;
        std::shared_ptr<std::thread> m_thread;
        std::weak_ptr<beam::io::Reactor> m_reactor;
        std::mutex m_startMutex;
        bool m_shouldStartNode;
        std::atomic<bool> m_shouldTerminateModel;
        std::atomic<bool> m_isRunning;
        std::condition_variable m_waiting;
        Key::IKdf::Ptr m_pKdf;
        Key::IPKdf::Ptr m_ownerKey;
        io::Timer::Ptr m_timer;
        std::function<void()> m_beforeStartAction;
    };
}