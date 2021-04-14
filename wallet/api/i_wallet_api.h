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

#include <cstdint>
#include <memory>
#include <string>
#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "utility/logger.h"
#include "wallet/core/contracts/i_shaders_manager.h"
#include "i_swaps_provider.h"
#include "sync_mode.h"
#include "api_errors.h"

namespace beam::wallet
{
    using json = nlohmann::json;

    const uint32_t ApiVer6_0     = 60;
    const uint32_t ApiVerCurrent = ApiVer6_0;
    const uint32_t ApiVerMin     = ApiVer6_0;
    const uint32_t ApiVerMax     = ApiVer6_0;

    class IWalletApiHandler
    {
    public:
        virtual ~IWalletApiHandler() = default;
        virtual void sendAPIResponse(const json& result) = 0;

        virtual void onParseError(const json& msg)
        {
            LOG_DEBUG() << "on API parse error: " << msg;
            sendAPIResponse(msg);
        }
    };

    class IWalletApi
    {
    public:
        typedef std::shared_ptr<IWalletApi> Ptr;
        typedef std::weak_ptr<IWalletApi> WeakPtr;
        typedef boost::optional<std::map<std::string, bool>> ACL;

        struct InitData
        {
            ACL acl;
            std::string appid;
            IShadersManager::Ptr contracts;
            ISwapsProvider::Ptr swaps;
            IWalletDB::Ptr walletDB;
            Wallet::Ptr wallet;
        };

        static bool ValidateAPIVersion(const std::string& version);

        // returns nullptr if wrong API version requested, should be safe to call from any thread
        static Ptr CreateInstance(const std::string& version, IWalletApiHandler& handler, const InitData& data);

        // returns nullptr if wrong API version requested, should be safe to call from any thread
        static Ptr CreateInstance(uint32_t version, IWalletApiHandler& handler, const InitData& data);

        struct MethodInfo
        {
            typedef std::map<beam::Asset::ID, beam::AmountBig::Type> Funds;

            Funds spend;
            Funds receive;
            beam::Amount fee = 0UL;
            std::string comment;
            std::string token;
            bool spendOffline = true;
        };

        struct ApiCallInfo
        {
            std::string method;
            json rpcid;
            json message;
            json params;
            bool appsAllowed;
        };

        struct ParseResult
        {
            ApiCallInfo acinfo;
            MethodInfo  minfo;

            ParseResult(const ApiCallInfo& aci, const MethodInfo& mi)
                : acinfo(aci)
                , minfo(mi)
            {}
        };

        // this should be safe to call in any thread.
        // returns info on success / calls sendAPIResponse on parse error
        virtual boost::optional<ParseResult> parseAPIRequest(const char* data, size_t size) = 0;

        // doesn't throw
        // should be called in API's/InitData's reactor thread
        // calls handler::sendAPIResponse on result (can be async)
        virtual ApiSyncMode executeAPIRequest(const char* data, size_t size) = 0;

        // form correct error json for given code and optional message
        virtual std::string fromError(const std::string& request, ApiError code, const std::string& optionalErrorText) = 0;

        virtual ~IWalletApi() = default;
    };
}

