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
#include "wallet_api.h"

namespace beam::wallet
{
    WalletApi::WalletApi(
            IWalletApiHandler& handler,
            ACL acl,
            std::string appid,
            std::string appname,
            IWalletDB::Ptr wdb,
            Wallet::Ptr wallet,
            ISwapsProvider::Ptr swaps,
            IShadersManager::Ptr contracts
        )
        : ApiBase(handler, std::move(acl), std::move(appid), std::move(appname))
        , _wdb(std::move(wdb))
        , _wallet(std::move(wallet))
        , _swaps(std::move(swaps))
        , _contracts(std::move(contracts))
    {
        _ttypesMap[TokenType::RegularOldStyle] = "regular";
        _ttypesMap[TokenType::Offline]         = "offline";
        _ttypesMap[TokenType::MaxPrivacy]      = "max_privacy";
        _ttypesMap[TokenType::Public]          = "public_offline";
        _ttypesMap[TokenType::RegularNewStyle] = "regular_new";

        // MUST BE SAFE TO CALL FROM ANY THREAD
        // Don't do anything with walletdb, providers &c.
        #define REG_FUNC(api, name, writeAccess, isAsync, appsAllowed)    \
        _methods[name] = {                                                \
            [this] (const JsonRpcId &id, const json &msg) {               \
                auto parseRes = onParse##api(id, msg);                    \
                onHandle##api(id, parseRes.first);                        \
            },                                                            \
            [this] (const JsonRpcId &id, const json &msg) -> MethodInfo { \
                auto parseRes = onParse##api(id, msg);                    \
                return parseRes.second;                                   \
            },                                                            \
            writeAccess, isAsync, appsAllowed                             \
        };
        WALLET_API_METHODS(REG_FUNC)
        #undef REG_FUNC
    }

    bool WalletApi::checkTxAccessRights(const TxParameters& params)
    {
        // If this API instance is not for apps, all txs are available
        if (_appId.empty())
        {
            return true;
        }

        // If there is no AppID on transaction app is not allowed to access it
        auto appid = params.GetParameter<std::string>(TxParameterID::AppID);
        if (!appid.is_initialized())
        {
            return false;
        }

        // Only if this tx has appid of the current App it can be accessed
        return _appId == *appid;
    }

    void WalletApi::checkTxAccessRights(const TxParameters& params, ApiError code, const std::string& errmsg)
    {
        if (!checkTxAccessRights(params))
        {
           // we do not throw 'NotAllowed' by default to not to expose that transaction exists
           // we let the caller to provide code & message that should mimic caller's 'not found' state
           throw jsonrpc_exception(code, errmsg);
        }
    }

    std::string WalletApi::getTokenType(TokenType type) const
    {
        auto it = _ttypesMap.find(type);
        return it != _ttypesMap.end() ? it->second : "unknown";
    }

    IWalletDB::Ptr WalletApi::getWalletDB() const
    {
        if (_wdb == nullptr)
        {
            throw jsonrpc_exception(ApiError::NotOpenedError, "WalletDB is nullptr");
        }

        assertWalletThread();
        return _wdb;
    }

    Wallet::Ptr WalletApi::getWallet() const
    {
        if (_wallet == nullptr)
        {
            throw jsonrpc_exception(ApiError::NotOpenedError, "Wallet is nullptr");
        }

        assertWalletThread();
        return _wallet;
    }

    ISwapsProvider::Ptr WalletApi::getSwaps() const
    {
        if (_swaps == nullptr)
        {
            throw jsonrpc_exception(ApiError::NoSwapsError);
        }

        assertWalletThread();
        return _swaps;
    }

    IShadersManager::Ptr WalletApi::getContracts() const
    {
        if (_contracts == nullptr)
        {
            throw jsonrpc_exception(ApiError::NotSupported);
        }

        assertWalletThread();
        return _contracts;
    }

    void WalletApi::assertWalletThread() const
    {
        if (_wallet == nullptr)
        {
            throw jsonrpc_exception(ApiError::NotOpenedError, "Wallet is nullptr");
        }
        _wallet->assertThread();
    }

    Height WalletApi::get_CurrentHeight() const
    {
       return getWallet()->get_CurrentHeight();
    }
}
