// Copyright 2020 The Beam Team
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
#include "node_connection.h"

namespace beam::wallet {
    ServiceNodeConnection::ServiceNodeConnection(proto::FlyClient& fc)
        : proto::FlyClient::NetworkStd(fc)
    {
    }

    void ServiceNodeConnection::OnConnectionFailed(const proto::NodeConnection::DisconnectReason& reason)
    {
        LOG_ERROR() << "Node connection failed, reason: " << reason;
    }

    MonitorNodeConnection::MonitorNodeConnection(proto::FlyClient& fc)
        : proto::FlyClient::NetworkStd(fc)
    {
    }

    void MonitorNodeConnection::OnConnectionFailed(const proto::NodeConnection::DisconnectReason& reason)
    {
        LOG_ERROR() << "Node connection failed, reason: " << reason;
    }
}
