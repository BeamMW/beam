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
#include "v6_api.h"

namespace beam::wallet
{
    V6Api::V6Api(IWalletApiHandler& handler, const ApiInitData& init)
        : ApiBase(handler, init)
        , _wdb(init.walletDB)
        , _wallet(init.wallet)
        , _swaps(init.swaps)
        , _contracts(init.contracts)
    {
        // MUST BE SAFE TO CALL FROM ANY THREAD
        _ttypesMap[TokenType::RegularOldStyle] = "regular";
        _ttypesMap[TokenType::Offline]         = "offline";
        _ttypesMap[TokenType::MaxPrivacy]      = "max_privacy";
        _ttypesMap[TokenType::Public]          = "public_offline";
        _ttypesMap[TokenType::RegularNewStyle] = "regular_new";
        V6_API_METHODS(BEAM_API_REG_METHOD)
    }

    bool V6Api::checkTxAccessRights(const TxParameters& params)
    {
        // If this API instance is not for apps, all txs are available
        if (!isApp())
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
        return getAppId() == *appid;
    }

    void V6Api::checkTxAccessRights(const TxParameters& params, ApiError code, const std::string& errmsg)
    {
        if (!checkTxAccessRights(params))
        {
           // we do not throw 'NotAllowed' by default to not to expose that transaction exists
           // we let the caller to provide code & message that should mimic caller's 'not found' state
           throw jsonrpc_exception(code, errmsg);
        }
    }

    std::string V6Api::getTokenType(TokenType type) const
    {
        auto it = _ttypesMap.find(type);
        return it != _ttypesMap.end() ? it->second : "unknown";
    }

    IWalletDB::Ptr V6Api::getWalletDB() const
    {
        if (_wdb == nullptr)
        {
            throw jsonrpc_exception(ApiError::NotOpenedError, "WalletDB is nullptr");
        }

        assertWalletThread();
        return _wdb;
    }

    Wallet::Ptr V6Api::getWallet() const
    {
        if (_wallet == nullptr)
        {
            throw jsonrpc_exception(ApiError::NotOpenedError, "Wallet is nullptr");
        }

        assertWalletThread();
        return _wallet;
    }

    ISwapsProvider::Ptr V6Api::getSwaps() const
    {
        if (_swaps == nullptr)
        {
            throw jsonrpc_exception(ApiError::NoSwapsError);
        }

        assertWalletThread();
        return _swaps;
    }

    IShadersManager::Ptr V6Api::getContracts() const
    {
        if (_contracts == nullptr)
        {
            throw jsonrpc_exception(ApiError::NotSupported);
        }

        assertWalletThread();
        return _contracts;
    }

    void V6Api::assertWalletThread() const
    {
        if (_wallet == nullptr)
        {
            throw jsonrpc_exception(ApiError::NotOpenedError, "Wallet is nullptr");
        }
        _wallet->assertThread();
    }

    Height V6Api::get_CurrentHeight() const
    {
       return getWallet()->get_CurrentHeight();
    }

    void V6Api::checkCAEnabled() const
    {
        TxFailureReason res = wallet::CheckAssetsEnabled(get_CurrentHeight());
        if (TxFailureReason::Count != res)
        {
            throw jsonrpc_exception(ApiError::NotSupported, GetFailureMessage(res));
        }
    }

    bool V6Api::getCAEnabled() const
    {
        TxFailureReason res = wallet::CheckAssetsEnabled(get_CurrentHeight());
        return res == TxFailureReason::Count;
    }
}
