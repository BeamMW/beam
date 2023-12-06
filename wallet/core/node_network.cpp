// Copyright 2019 The Beam Team
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

#include "node_network.h"
#include "utility/logger.h"
#include "wallet/core/default_peers.h"

namespace beam::wallet
{
    void NodeNetwork::tryToConnect()
    {
        m_Cfg.m_vNodes.clear();

        if (!m_timer)
        {
            m_timer = io::Timer::create(io::Reactor::get_Current());
        }

        if (m_attemptToConnect < MAX_ATTEMPT_TO_CONNECT)
        {
            ++m_attemptToConnect;
        }
        else if (m_attemptToConnect == MAX_ATTEMPT_TO_CONNECT)
        {
            m_attemptToConnect = 0;
            proto::NodeConnection::DisconnectReason reason;
            reason.m_Type = proto::NodeConnection::DisconnectReason::Io;
            reason.m_IoError = io::EC_HOST_RESOLVED_ERROR;
            for (const auto& observer : m_observers)
            {
                observer->onNodeConnectionFailed(reason);
            }
        }

        m_timer->start(RECONNECTION_TIMEOUT, false, [this]() {
            auto nodeAddresses = getDefaultPeers();
            nodeAddresses.push_back(m_nodeAddress);
            for (const auto& addr : nodeAddresses)
            {
                io::Address nodeAddr;
                if (!addr.empty() && nodeAddr.resolve(addr.c_str()))
                {
                    m_Cfg.m_vNodes.push_back(nodeAddr);
                }
                else
                {
                    LOG_WARNING() << "Unable to resolve node address: " << addr;
                }
            }
            if (!m_Cfg.m_vNodes.empty())
            {
                std::sort(m_Cfg.m_vNodes.begin(), m_Cfg.m_vNodes.end());
                m_Cfg.m_vNodes.erase(std::unique(m_Cfg.m_vNodes.begin(), m_Cfg.m_vNodes.end()), m_Cfg.m_vNodes.end());
                Connect();
                return;
            }

            LOG_WARNING() << "User-specified nodes cannot be resolved.";
            if (!m_fallbackAddresses.empty())
            {
                LOG_WARNING() << "Attempting to connect to fallback nodes";
                // try to solve DNS problem with known ip addresses
                std::copy(m_fallbackAddresses.begin(), m_fallbackAddresses.end(), std::back_inserter(m_Cfg.m_vNodes));
                Connect();
            }
            else 
            {
                LOG_WARNING() << "There are no fallback nodes. Trying to re-connect in " << RECONNECTION_TIMEOUT << " ms";
                tryToConnect();
            }
        });
    }

    void NodeNetwork::OnNodeConnected(bool bConnected)
    {
        if (bConnected)
        {
            m_disconnectReason.clear();
            ++m_connections;
        }
        else
        {
            --m_connections;
        }
        for (const auto observer : m_observers)
        {
            observer->onNodeConnectedStatusChanged(bConnected);
        }
    }

    void NodeNetwork::OnConnectionFailed(const proto::NodeConnection::DisconnectReason& reason)
    {
        std::stringstream ss;
        ss << reason;
        m_disconnectReason = ss.str();
        for (const auto observer : m_observers)
        {
            observer->onNodeConnectionFailed(reason);
        }
    }

    std::string NodeNetwork::getNodeAddress() const
    {
        return m_nodeAddress;
    }

    void NodeNetwork::setNodeAddress(const std::string& nodeAddr)
    {
        m_attemptToConnect = 0;
        Disconnect();
        m_nodeAddress = nodeAddr;
        tryToConnect();
    }

    const std::string& NodeNetwork::getLastError() const
    {
        return m_disconnectReason;
    }

    size_t NodeNetwork::getConnections() const
    {
        return m_connections;
    }

    void NodeNetwork::Subscribe(INodeConnectionObserver* observer)
    {
        auto it = std::find(std::cbegin(m_observers),
                            std::cend(m_observers),
                            observer);
        assert(it == m_observers.end());
        if (it == m_observers.end()) m_observers.push_back(observer);
    }

    void NodeNetwork::Unsubscribe(INodeConnectionObserver* observer)
    {
        auto it = std::find(std::cbegin(m_observers),
                            std::cend(m_observers),
                            observer);
        assert(it != m_observers.end());
        m_observers.erase(it);
    }
    
} // namespace beam::wallet
