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

#include "wallet/laser/connection.h"
#include "wallet/core/wallet_request_bbs_msg.h"
#include "utility/logger.h"

namespace beam::wallet::laser
{
Connection::Connection(const FlyClient::NetworkStd::Ptr& net)
    : m_pNet(net)
{
}

void Connection::Connect()
{
    m_pNet->Connect();
}

void Connection::Disconnect()
{
    m_pNet->Disconnect();
}

void Connection::BbsSubscribe(
        BbsChannel ch,
        Timestamp timestamp,
        FlyClient::IBbsReceiver* receiver)
{
    m_pNet->BbsSubscribe(ch, timestamp, receiver);
}

void Connection::PostRequestInternal(FlyClient::Request& r)
{
    if (FlyClient::Request::Type::Transaction == r.get_Type())
        BEAM_LOG_DEBUG() << "### Broadcasting transaction ###";

    m_pNet->PostRequestInternal(r);
}

}  // namespace beam::wallet::laser
