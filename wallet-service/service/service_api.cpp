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
#include "service_api.h"

namespace beam::wallet {
    WalletServiceApi::WalletServiceApi(IWalletData& walletData)
        // assets are forcibly disabled in wallet service
        // no ACL in wallet service
        : WalletApi(*this, false, boost::none)
        , WalletApiHandler(walletData, boost::none, false)
    {
        #define REG_FUNC(api, name, writeAccess) \
        _methods[name] = {BIND_THIS_MEMFN(on##api##Message), writeAccess};
        WALLET_SERVICE_API_METHODS(REG_FUNC)
        #undef REG_FUNC
    };

    void WalletServiceApi::onCreateWalletMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        // TODO: throttling
        // if (_createCnt++ >= _createLimit) {
        //    throw jsonrpc_exception { ApiError::ThrottleError, "" , id };
        // }

        CreateWallet createWallet;

        if (existsJsonParam(params, "pass"))
        {
            createWallet.pass = params["pass"].get<std::string>();
        }
        else throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "'pass' parameter must be specified.", id };

        if (existsJsonParam(params, "ownerkey"))
        {
            createWallet.ownerKey = params["ownerkey"].get<std::string>();
        }
        else throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "'ownerkey' parameter must be specified.", id };

        onWalletApiMessage(id, createWallet);
    }

    void WalletServiceApi::onOpenWalletMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        OpenWallet openWallet;

        const char* jsonId = "id";
        const char* jsonPass = "pass";
        const char* jsonKeeper = "fresh_keeper";

        if (existsJsonParam(params, jsonPass))
        {
            openWallet.pass = params[jsonPass].get<std::string>();
        }
        else throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "'pass' parameter must be specified.", id };

        if (existsJsonParam(params, jsonId))
        {
            openWallet.id = params[jsonId].get<std::string>();
        }
        else throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "'id' parameter must be specified.", id };

        if (existsJsonParam(params, jsonKeeper))
        {
            openWallet.freshKeeper = params[jsonKeeper].get<bool>();
        }

        onWalletApiMessage(id, openWallet);
    }

    void WalletServiceApi::onPingMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        onWalletApiMessage(id, Ping{});
    }

    void WalletServiceApi::onReleaseMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        onWalletApiMessage(id, Release{});
    }

    void WalletServiceApi::onCalcChangeMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        ParameterReader reader(id, params);
        CalcChange message {reader.readAmount("amount")};
        onWalletApiMessage(id, message);
    }

    void WalletServiceApi::onChangePasswordMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        checkJsonParam(params, "new_pass", id);
        ChangePassword message;
        message.newPassword = params["new_pass"].get<std::string>();
        onWalletApiMessage(id, message);
    }

    void WalletServiceApi::getWalletApiResponse(const JsonRpcId& id, const CreateWallet::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHrd, JsonRpcVerHrd},
            {"id", id},
            {"result", res.id}
        };
    }

    void WalletServiceApi::getWalletApiResponse(const JsonRpcId& id, const OpenWallet::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHrd, JsonRpcVerHrd},
            {"id", id},
            {"result", res.session}
        };
    }

    void WalletServiceApi::getWalletApiResponse(const JsonRpcId& id, const Ping::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHrd, JsonRpcVerHrd},
            {"id", id},
            {"result", "pong"}
        };
    }

    void WalletServiceApi::getWalletApiResponse(const JsonRpcId& id, const Release::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHrd, JsonRpcVerHrd},
            {"id", id},
            {"result", "done"}
        };
    }

    void WalletServiceApi::getWalletApiResponse(const JsonRpcId& id, const CalcChange::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHrd, JsonRpcVerHrd},
            {"id", id},
            {"result",
                {
                    {"change", res.change},
                    {"change_str", std::to_string(res.change)} // string representation
                }
            }
        };
    }

    void WalletServiceApi::getWalletApiResponse(const JsonRpcId& id, const ChangePassword::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHrd, JsonRpcVerHrd},
            {"id", id},
            {"result", "done"}
        };
    }
}