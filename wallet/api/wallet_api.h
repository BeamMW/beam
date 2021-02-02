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

#include "api_base.h"
#include "wallet/core/wallet.h"
#include "wallet/core/wallet_db.h"
#include "api_swaps_provider.h"
#include "wallet_api_defs.h"

namespace beam::wallet {
    class WalletApi : public ApiBase
    {
    public:
        explicit WalletApi(ACL acl = boost::none);

        virtual IWalletDB::Ptr getWalletDB() const
        {
             throw jsonrpc_exception(ApiError::NotOpenedError, "WalletDB is nullptr");
        }

         virtual Wallet::Ptr getWallet() const
         {
            throw jsonrpc_exception(ApiError::NotOpenedError, "Wallet is nullptr");
         }

         ISwapsProvider::Ptr getSwaps() const
         {
            throw jsonrpc_exception(ApiError::NoSwapsError);
         }

        #define RESPONSE_FUNC(api, name, _) \
        void getResponse(const JsonRpcId& id, const api::Response& data, json& msg);
        WALLET_API_METHODS(RESPONSE_FUNC)
        #undef RESPONSE_FUNC

        #define MESSAGE_FUNC(api, name, _) \
        virtual void onMessage(const JsonRpcId& id, const api& data);
        WALLET_API_METHODS(MESSAGE_FUNC)
        #undef MESSAGE_FUNC

        template<typename T>
        void doResponse(const JsonRpcId& id, const T& response)
        {
            json msg;
            getResponse(id, response, msg);
            sendMessage(msg);
        }

        void FillAddressData(const AddressData& data, WalletAddress& address);
        void doTxAlreadyExistsError(const JsonRpcId& id);

         template<typename T>
        static void doPagination(size_t skip, size_t count, std::vector<T>& res)
        {
            if (count > 0)
            {
                size_t start = skip;
                size_t size = res.size();

                if (start < size)
                {
                    res.erase(res.begin(), res.begin() + start);
                    if (count < res.size())
                    {
                        res.erase(res.begin() + count, res.end());
                    }
                }
                else res = {};
            }
            else res = {};
        }

        template<typename T>
        void onIssueConsumeMessage(bool issue, const JsonRpcId& id, const T& data);

        template<typename T>
        void setTxAssetParams(const JsonRpcId& id, TxParameters& tx, const T& data);

    private:
        #define MESSAGE_FUNC(api, name, _) \
        void on##api##Message(const JsonRpcId& id, const json& msg);
        WALLET_API_METHODS(MESSAGE_FUNC)
        #undef MESSAGE_FUNC

        template<typename T>
        void onIssueConsumeMessage(bool issue, const JsonRpcId& id, const json& params);
        void checkCAEnabled(const JsonRpcId& id);
    };
}
