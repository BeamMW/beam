// Copyright 2018 The Beam Team
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

#include <boost/optional.hpp>

#include "wallet/api/api.h"

namespace beam::wallet
{

#define WALLET_SERVICE_API_METHODS(macro) \
    macro(CreateWallet,     "create_wallet",    API_WRITE_ACCESS)   \
    macro(OpenWallet,       "open_wallet",      API_WRITE_ACCESS)   \
    macro(Ping,             "ping",             API_READ_ACCESS)    \
    macro(Release,          "release",          API_READ_ACCESS)    \

    struct CreateWallet
    {
        std::string pass;
        std::string ownerKey;

        struct Response
        {
            std::string id;
        };
    };

    struct OpenWallet
    {
        std::string id;
        std::string pass;

        struct Response
        {
            std::string session;
        };
    };

    struct Ping
    {
        struct Response {};
    };

    struct Release
    {
        struct Response {};
    };


    class IWalletServiceApiHandler : public IWalletApiHandler
    {
    public:

#define MESSAGE_FUNC(api, name, _) \
        virtual void onMessage(const JsonRpcId& id, const api& data) = 0; 

        WALLET_SERVICE_API_METHODS(MESSAGE_FUNC)

#undef MESSAGE_FUNC
    };

    class WalletServiceApi : public WalletApi
    {
    public:
        WalletServiceApi(IWalletServiceApiHandler& handler, ACL acl = boost::none);

#define RESPONSE_FUNC(api, name, _) \
        void getResponse(const JsonRpcId& id, const api::Response& data, json& msg); 

        WALLET_SERVICE_API_METHODS(RESPONSE_FUNC)

#undef RESPONSE_FUNC

    private:

#define MESSAGE_FUNC(api, name, _) \
        void on##api##Message(const JsonRpcId& id, const json& msg);

        WALLET_SERVICE_API_METHODS(MESSAGE_FUNC)

#undef MESSAGE_FUNC
    };
}
