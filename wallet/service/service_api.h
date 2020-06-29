// Copyright 2018-2020 The Beam Team
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
#include "wallet/core/common.h"
#include "wallet/api/api_handler.h"

namespace beam::wallet {
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
        bool freshKeeper = true;

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

    struct CalcChange
    {
        Amount amount;
        struct Response
        {
            Amount change;
        };
    };

    struct ChangePassword
    {
        std::string newPassword;
        struct Response
        {
        };
    };

#define WALLET_SERVICE_API_METHODS(macro) \
    macro(CreateWallet,     "create_wallet",    API_WRITE_ACCESS)   \
    macro(OpenWallet,       "open_wallet",      API_WRITE_ACCESS)   \
    macro(Ping,             "ping",             API_READ_ACCESS)    \
    macro(Release,          "release",          API_READ_ACCESS)    \
    macro(CalcChange,       "calc_change",      API_READ_ACCESS)    \
    macro(ChangePassword,   "change_password",  API_WRITE_ACCESS)

    class WalletServiceApi
        : public WalletApi
        , public WalletApiHandler // TODO: May be move up ??
    {
    public:
        WalletServiceApi(IWalletData& walletData);
        WalletServiceApi(const WalletServiceApi&) = delete;

    protected:
        #define MESSAGE_FUNC(api, name, _) \
        virtual void onWalletApiMessage(const JsonRpcId& id, const api& data) = 0;
        WALLET_SERVICE_API_METHODS(MESSAGE_FUNC)
        #undef MESSAGE_FUNC

        #define RESPONSE_FUNC(api, name, _) \
        void getWalletApiResponse(const JsonRpcId& id, const api::Response& data, json& msg);
        WALLET_SERVICE_API_METHODS(RESPONSE_FUNC)
        #undef RESPONSE_FUNC

    private:
        #define MESSAGE_FUNC(api, name, _) \
        void on##api##Message(const JsonRpcId& id, const json& msg);
        WALLET_SERVICE_API_METHODS(MESSAGE_FUNC)
        #undef MESSAGE_FUNC

        // TODO: add throttling
        // unsigned _createCnt = 0;
        // const unsigned _createLimit = 1;
    };
}
