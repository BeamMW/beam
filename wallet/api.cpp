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

#include "api.h"

#include "nlohmann/json.hpp"

using json = nlohmann::json;

namespace beam
{
    struct jsonrpc_exception
    {
        int code;
        std::string message;
        int id;
    };

    void throwInvalidJsonRpc(int id = 0)
    {
        throw jsonrpc_exception{ INVALID_JSON_RPC , "Invalid JSON-RPC.", id };
    }

    void throwUnknownJsonRpc(int id)
    {
        throw jsonrpc_exception{ NOTFOUND_JSON_RPC , "Procedure not found.", id};
    }

    std::string getJsonString(const char* data, size_t size)
    {
        return std::string(data, data + (size > 1024 ? 1024 : size));
    }

    WalletApi::WalletApi(IWalletApiHandler& handler)
        : _handler(handler)
    {
#define REG_FUNC(api, name) \
        _methods[name] = BIND_THIS_MEMFN(on##api##Message);

        WALLET_API_METHODS(REG_FUNC)

#undef REG_FUNC
    };

    void checkJsonParam(const nlohmann::json& params, const std::string& name, int id)
    {
        if (params.find(name) == params.end()) 
            throwInvalidJsonRpc(id);
    }

    void WalletApi::onCreateAddressMessage(int id, const nlohmann::json& params)
    {
        checkJsonParam(params, "lifetime", id);
        checkJsonParam(params, "metadata", id);

        CreateAddress createAddress;
        createAddress.metadata = params["metadata"];
        createAddress.lifetime = params["lifetime"];

        if (params["lifetime"] < 0)
            throwInvalidJsonRpc(id);

        _handler.onMessage(id, createAddress);
    }

    void WalletApi::onSendMessage(int id, const nlohmann::json& params)
    {
        Send send;
        _handler.onMessage(id, send);
    }

    void WalletApi::onReplaceMessage(int id, const nlohmann::json& params)
    {
        Replace replace;
        _handler.onMessage(id, replace);
    }

    void WalletApi::onStatusMessage(int id, const nlohmann::json& params)
    {
        Status status;
        _handler.onMessage(id, status);
    }

    void WalletApi::onSplitMessage(int id, const nlohmann::json& params)
    {
        Split split;
        _handler.onMessage(id, split);
    }

    void WalletApi::onBalanceMessage(int id, const nlohmann::json& params)
    {
        Balance balance;
        _handler.onMessage(id, balance);
    }

    void WalletApi::onGetUtxoMessage(int id, const nlohmann::json& params)
    {
        GetUtxo getUtxo;
        _handler.onMessage(id, getUtxo);
    }

    void WalletApi::onLockMessage(int id, const nlohmann::json& params)
    {
        Lock lock;
        _handler.onMessage(id, lock);
    }

    void WalletApi::onUnlockMessage(int id, const nlohmann::json& params)
    {
        Unlock unlock;
        _handler.onMessage(id, unlock);
    }

    void WalletApi::onCreateUtxoMessage(int id, const nlohmann::json& params)
    {
        CreateUtxo createUtxo;
        _handler.onMessage(id, createUtxo);
    }

    void WalletApi::onPollMessage(int id, const nlohmann::json& params)
    {
        Poll poll;
        _handler.onMessage(id, poll);
    }

    void WalletApi::getResponse(int id, const Balance::Response& res, json& msg)
    {
        msg = json
        {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"result", 
                {
                    {"available", res.available},
                    {"in_progress", res.in_progress},
                    {"locked", res.locked},
                }
            }
        };
    }

    void WalletApi::getResponse(int id, const CreateAddress::Response& res, json& msg)
    {
        msg = json
        {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"result", std::to_string(res.address)}
        };
    }

    void WalletApi::getResponse(int id, const GetUtxo::Response& res, json& msg)
    {
        msg = json
        {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"result", json::array()}
        };

        for (auto& utxo : res.utxos)
        {
            msg["result"].push_back(
            { 
                {"id", utxo.m_ID.m_Idx},
                {"amount", utxo.m_ID.m_Value},
                {"type", (const char*)FourCC::Text(utxo.m_ID.m_Type)},
                {"height", utxo.m_createHeight},
                {"maturity", utxo.m_maturity},
            });
        }
    }

    bool WalletApi::parse(const char* data, size_t size)
    {
        if (size == 0) return false;

        try
        {
            json msg = json::parse(data, data + size);

            if (msg["jsonrpc"] != "2.0") throwInvalidJsonRpc();
            if (msg["id"] <= 0) throwInvalidJsonRpc();
            if (msg["method"] == nullptr) throwInvalidJsonRpc();
            if (msg["params"] == nullptr) throwInvalidJsonRpc();
            if (_methods.find(msg["method"]) == _methods.end()) throwUnknownJsonRpc(msg["id"]);

            try
            {
                _methods[msg["method"]](msg["id"], msg["params"]);
            }
            catch (const nlohmann::detail::exception& e)
            {
                LOG_ERROR() << "json parse: " << e.what() << "\n" << getJsonString(data, size);

                throwInvalidJsonRpc(msg["id"]);
            }
        }
        catch (const jsonrpc_exception& e)
        {
            json msg
            {
                {"jsonrpc", "2.0"},
                {"error",
                    {
                        {"code", e.code},
                        {"message", e.message},
                    }
                }
            };

            if (e.id) msg["id"] = e.id;
            else msg["id"] = nullptr;

            _handler.onInvalidJsonRpc(msg);
        }
        catch (const std::exception& e)
        {
            LOG_ERROR() << "json parse: " << e.what() << "\n" << getJsonString(data, size);
            return false;
        }

        return true;
    }
}
