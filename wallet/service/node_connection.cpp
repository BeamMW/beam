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
}
