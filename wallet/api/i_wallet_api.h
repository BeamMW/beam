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
            IShadersManager::Ptr contracts;
            ISwapsProvider::Ptr swaps;
            IWalletDB::Ptr walletDB;
            Wallet::Ptr wallet;
        };

        static bool ValidateAPIVersion(const std::string& version);

        // returns nullptr if wrong API version requested
        static Ptr CreateInstance(const std::string& version, IWalletApiHandler& handler, const InitData& data);

        // returns nullptr if wrong API version requested
        static Ptr CreateInstance(uint32_t version, IWalletApiHandler& handler, const InitData& data);

        struct ParseInfo {
            std::string method;
            json rpcid;
            json message;
            json params;
        };

        // this should be safe to call in any thread.
        // returns info on success / calls sendAPIResponse on parse error
        virtual boost::optional<ParseInfo> parseAPIRequest(const char* data, size_t size) = 0;

        // doesn't throw
        // should be called in API's/InitData's reactor thread
        // calls handler::sendAPIResponse on result (can be async)
        virtual ApiSyncMode executeAPIRequest(const char* data, size_t size) = 0;

        virtual ~IWalletApi() = default;
    };
}

