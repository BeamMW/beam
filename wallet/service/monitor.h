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

#pragma once

#include "wallet/api/api.h"

namespace beam::wallet
{

#define WALLET_MONITOR_API_METHODS(macro) \
    macro(Subscribe,     "subscribe",     API_WRITE_ACCESS)   \
    macro(UnSubscribe,   "unsubscribe",   API_WRITE_ACCESS)   \
    

    struct Subscribe
    {
        WalletID address;
        ECC::Scalar::Native privateKey;
        Timestamp expires;

        struct Response
        {
        };
    };

    struct UnSubscribe
    {
        WalletID address;
        struct Response
        {
        };
    };

    class ISbbsMonitorApiHandler : public IApiHandler
    {
    public:

#define MESSAGE_FUNC(api, name, _) \
        virtual void onMessage(const JsonRpcId& id, const api& data) = 0; 

        WALLET_MONITOR_API_METHODS(MESSAGE_FUNC)

#undef MESSAGE_FUNC
    };

    class SbbsMonitorApi : public Api
    {
    public:
        SbbsMonitorApi(ISbbsMonitorApiHandler& handler, ACL acl = boost::none);

#define RESPONSE_FUNC(api, name, _) \
        void getResponse(const JsonRpcId& id, const api::Response& data, json& msg); 

        WALLET_MONITOR_API_METHODS(RESPONSE_FUNC)

#undef RESPONSE_FUNC

    private:

        ISbbsMonitorApiHandler& getHandler() const;

#define MESSAGE_FUNC(api, name, _) \
        void on##api##Message(const JsonRpcId& id, const json& msg);

        WALLET_MONITOR_API_METHODS(MESSAGE_FUNC)

#undef MESSAGE_FUNC
    };
} // beam::wallet
