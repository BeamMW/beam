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

    const uint32_t ApiVer6_0 = 60;
    const uint32_t ApiVerCurrent = ApiVer6_0;

    class IWalletApiHandler
    {
    public:
         virtual void sendAPIResponse(const json& result) = 0;

         virtual void onParseError(const json& msg)
         {
             LOG_DEBUG() << "onInvalidJsonRpc: " << msg;
             sendAPIResponse(msg);
         }
    };

    struct bad_api_version: public std::runtime_error {
        bad_api_version()
            : std::runtime_error("unsupported version of API requested")
        {
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

        // throws std::runtime_error
        static Ptr CreateInstance(uint32_t version, IWalletApiHandler& handler, const InitData& data);

        // doesn't throw
        virtual ApiSyncMode executeAPIRequest(const char* data, size_t size) = 0;
    };
}

