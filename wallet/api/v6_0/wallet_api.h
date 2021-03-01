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
#include "wallet_api_defs.h"
#include "wallet/core/contracts/i_shaders_manager.h"

namespace beam::wallet
{
    class WalletApi
        : public ApiBase
        , public IShadersManager::IDone
    {
    public:
        WalletApi(IWalletApiHandler& handler,
                  ACL acl,
                  IWalletDB::Ptr wdb,
                  Wallet::Ptr wallet,
                  ISwapsProvider::Ptr swaps,
                  IShadersManager::Ptr contracts);

        virtual IWalletDB::Ptr getWalletDB() const
        {
            if (_wdb == nullptr)
            {
                throw jsonrpc_exception(ApiError::NotOpenedError, "WalletDB is nullptr");
            }
            return _wdb;
        }

        virtual Wallet::Ptr getWallet() const
        {
            if (_wallet == nullptr)
            {
                throw jsonrpc_exception(ApiError::NotOpenedError, "Wallet is nullptr");
            }
            return _wallet;
        }

        virtual ISwapsProvider::Ptr getSwaps() const
        {
            if (_swaps == nullptr)
            {
                throw jsonrpc_exception(ApiError::NoSwapsError);
            }
            return _swaps;
        }

        virtual IShadersManager::Ptr getContracts() const
        {
            if (_contracts == nullptr)
            {
                throw jsonrpc_exception(ApiError::NotSupported);
            }
            return _contracts;
        }

        virtual Height get_CurrentHeight() const
        {
            return _wallet->get_CurrentHeight();
        }

        void onShaderDone(
            boost::optional<TxID> txid,
            boost::optional<std::string> result,
            boost::optional<std::string> error
        ) override;

        #define RESPONSE_FUNC(api, name, ...) \
        void getResponse(const JsonRpcId& id, const api::Response& data, json& msg);
        WALLET_API_METHODS(RESPONSE_FUNC)
        #undef RESPONSE_FUNC

        // TODO: rename onMessage -> onParse or ParseMessage
        #define MESSAGE_FUNC(api, name, ...) \
        virtual void onMessage(const JsonRpcId& id, const api& data);
        WALLET_API_METHODS(MESSAGE_FUNC)
        #undef MESSAGE_FUNC

        template<typename T>
        void doResponse(const JsonRpcId& id, const T& response)
        {
            json msg;
            getResponse(id, response, msg);
            _handler.sendAPIResponse(msg);
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
        #define MESSAGE_FUNC(api, name, ...) \
        void on##api##Message(const JsonRpcId& id, const json& msg);
        WALLET_API_METHODS(MESSAGE_FUNC)
        #undef MESSAGE_FUNC

        template<typename T>
        void onIssueConsumeMessage(bool issue, const JsonRpcId& id, const json& params);
        void checkCAEnabled(const JsonRpcId& id);

        // If no fee read and no min fee provided this function calculates minimum fee itself
        Amount getBeamFeeParam(const json& params, const std::string& name, Amount feeMin) const;
        Amount getBeamFeeParam(const json& params, const std::string& name) const;

    protected:
        // Do not access these directly, use getters
        IWalletDB::Ptr       _wdb;
        Wallet::Ptr          _wallet;
        ISwapsProvider::Ptr  _swaps;

        IShadersManager::Ptr _contracts;
        JsonRpcId _ccallId;
    };
}
