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
#include "wallet/core/node_network.h"
#include "wallet/ipfs/ipfs.h"
#include "base/api_errors.h"
#include "i_swaps_provider.h"
#include "sync_mode.h"

namespace beam::wallet
{
    using json = nlohmann::json;

    const uint32_t ApiVer6_0     = 60;
    const uint32_t ApiVer6_1     = 61;
    const uint32_t ApiVer6_2     = 62;
    const uint32_t ApiVer7_0     = 70;
    const uint32_t ApiVer7_1     = 71;
    const uint32_t ApiVerCurrent = ApiVer7_1;
    const uint32_t ApiVerMax     = ApiVer7_1;
    const uint32_t ApiVerMin     = ApiVer6_0;

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

    typedef boost::optional<std::map<std::string, bool>> ApiACL;
    struct ApiInitData
    {
        ApiACL acl;
        std::string appId;
        std::string appName;
        IShadersManager::Ptr contracts;
        ISwapsProvider::Ptr swaps;
        IWalletDB::Ptr walletDB;
        Wallet::Ptr wallet;
        NodeNetwork::Ptr nodeNetwork;
        #ifdef BEAM_IPFS_SUPPORT
        IPFSService::Ptr ipfs;
        #endif
    };

    class IWalletApi
    {
    public:
        typedef std::shared_ptr<IWalletApi> Ptr;
        typedef std::weak_ptr<IWalletApi> WeakPtr;

        static bool ValidateAPIVersion(const std::string& version);

        // SAFE TO CALL FROM ANY THREAD
        // API SHOULD BE DESTROYED IN CONTEXT OF InitData/Wallet thread
        // returns nullptr if wrong API version requested
        static Ptr CreateInstance(const std::string& version, IWalletApiHandler& handler, const ApiInitData& data);

        // SAFE TO CALL FROM ANY THREAD
        // API SHOULD BE DESTROYED IN CONTEXT OF InitData/Wallet thread
        // returns nullptr if wrong API version requested
        static Ptr CreateInstance(uint32_t version, IWalletApiHandler& handler, const ApiInitData& data);

        struct MethodInfo
        {
            typedef std::map<beam::Asset::ID, beam::AmountBig::Type> Funds;

            Funds spend;
            Funds receive;
            beam::Amount fee = 0UL;
            bool spendOffline = false;

            boost::optional<std::string> title;
            boost::optional<std::string> comment;
            boost::optional<std::string> confirm_comment;
            boost::optional<std::string> token;

            inline void appendReceive(beam::Asset::ID id, const beam::AmountBig::Type& val)
            {
                if (receive.find(id) == receive.end())
                {
                    receive[id] = val;
                }
                else
                {
                    receive[id] += val;
                }
            }

            inline void appendSpend(beam::Asset::ID id, const beam::AmountBig::Type& val)
            {
                if (spend.find(id) == spend.end())
                {
                    spend[id] = val;
                }
                else
                {
                    spend[id] += val;
                }
            }
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

            ParseResult(ApiCallInfo aci, MethodInfo mi)
                : acinfo(std::move(aci))
                , minfo(std::move(mi))
            {}
        };

        // should be called in API's/InitData's reactor thread
        // returns info on success / calls sendAPIResponse on parse error
        virtual boost::optional<ParseResult> parseAPIRequest(const char* data, size_t size) = 0;

        // should be called in API's/InitData's reactor thread
        // calls handler::sendAPIResponse on result (can be async)
        virtual ApiSyncMode executeAPIRequest(const char* data, size_t size) = 0;

        // Safe to call from any thread
        // form correct error json for given code and optional message
        virtual std::string fromError(const std::string& request, ApiError code, const std::string& optionalErrorText) = 0;

        virtual ~IWalletApi() = default;
    };
}

