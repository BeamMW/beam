#pragma once

#include "wallet/core/wallet_network.h"

namespace beam::wallet {
    struct ServiceNodeConnection final: public proto::FlyClient::NetworkStd
    {
    public:
        ServiceNodeConnection(proto::FlyClient& fc);
        void OnConnectionFailed(const proto::NodeConnection::DisconnectReason& reason) override;
    };
}
