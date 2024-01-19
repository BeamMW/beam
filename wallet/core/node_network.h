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

#include <string>
#include <vector>
#include "core/fly_client.h"
#include "wallet/core/node_connection_observer.h"

namespace beam::wallet
{

    class NodeNetwork final: public proto::FlyClient::NetworkStd
    {
    public:
        using Ptr = std::shared_ptr<NodeNetwork>;

        NodeNetwork(proto::FlyClient& fc, const std::string& nodeAddress = {}, std::vector<io::Address>&& fallbackAddresses = {})
            : proto::FlyClient::NetworkStd(fc)
            , m_nodeAddress(nodeAddress)
            , m_fallbackAddresses(std::move(fallbackAddresses))
        {
        }

        void tryToConnect();

        void Subscribe(INodeConnectionObserver* observer);
        void Unsubscribe(INodeConnectionObserver* observer);

        std::string getNodeAddress() const;
        void setNodeAddress(const std::string&);
        const std::string& getLastError() const;
        size_t getConnections() const;

    private:
        void OnNodeConnected(bool bConnected) override;
        void OnConnectionFailed(const proto::NodeConnection::DisconnectReason& reason) override;
        
        const uint8_t MAX_ATTEMPT_TO_CONNECT = 5;
        const uint16_t RECONNECTION_TIMEOUT = 1000;
        io::Timer::Ptr m_timer;
        uint8_t m_attemptToConnect = 0;
        std::vector<INodeConnectionObserver*> m_observers;
        std::string m_nodeAddress;
        std::vector<io::Address> m_fallbackAddresses;
        size_t m_connections{ 0 };
        std::string m_disconnectReason;
    };

} // namespace beam::wallet
