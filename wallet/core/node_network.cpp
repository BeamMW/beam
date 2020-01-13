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

namespace beam::wallet
{
    void NodeNetwork::tryToConnect()
    {
        // if user changed address to correct (using of setNodeAddress)
        if (m_Cfg.m_vNodes.size() > 0)
            return;

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
            proto::NodeConnection::DisconnectReason reason;
            reason.m_Type = proto::NodeConnection::DisconnectReason::Io;
            reason.m_IoError = io::EC_HOST_RESOLVED_ERROR;
            m_observer.nodeConnectionFailed(reason);
        }

        m_timer->start(RECONNECTION_TIMEOUT, false, [this]() {
            io::Address nodeAddr;
            if (nodeAddr.resolve(m_nodeAddrStr.c_str()))
            {
                m_Cfg.m_vNodes.push_back(nodeAddr);
                Connect();
            }
            else
            {
                tryToConnect();
            }
        });
    }

    void NodeNetwork::OnNodeConnected(size_t, bool bConnected)
    {
        m_observer.nodeConnectedStatusChanged(bConnected);
    }

    void NodeNetwork::OnConnectionFailed(size_t, const proto::NodeConnection::DisconnectReason& reason)
    {
        m_observer.nodeConnectionFailed(reason);
    }
    
} // namespace beam::wallet
