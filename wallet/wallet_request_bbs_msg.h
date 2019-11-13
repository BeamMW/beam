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
#include <boost/intrusive/list.hpp>

namespace beam::wallet
{
struct WalletRequestBbsMsg
    :public proto::FlyClient::RequestBbsMsg
    ,public boost::intrusive::list_base_hook<>
{
    typedef boost::intrusive_ptr<WalletRequestBbsMsg> Ptr;
    virtual ~WalletRequestBbsMsg() {}

    uint64_t m_MessageID;
};

typedef boost::intrusive::list<WalletRequestBbsMsg> BbsMsgList;
}  // namespace beam::wallet
