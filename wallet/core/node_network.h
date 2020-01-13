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

#pragma once

#include "core/fly_client.h"
#include "wallet/core/node_connection_observer.h"

namespace beam::wallet
{

    class NodeNetwork final: public proto::FlyClient::NetworkStd
    {
    public:
        NodeNetwork(proto::FlyClient& fc, INodeConnectionObserver& observer, const std::string& nodeAddress)
            : proto::FlyClient::NetworkStd(fc)
            , m_observer(observer)
            , m_nodeAddrStr(nodeAddress)
        {
        }

        void tryToConnect();

    private:
        void OnNodeConnected(size_t, bool bConnected) override;
        void OnConnectionFailed(size_t, const proto::NodeConnection::DisconnectReason& reason) override;

        INodeConnectionObserver& m_observer;
    public:
        std::string m_nodeAddrStr;

        io::Timer::Ptr m_timer;
        uint8_t m_attemptToConnect = 0;

        const uint8_t MAX_ATTEMPT_TO_CONNECT = 5;
        const uint16_t RECONNECTION_TIMEOUT = 1000;
    };

} // namespace beam::wallet
