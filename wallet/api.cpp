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
    namespace
    {
        std::string txIDToString(const TxID& txId)
        {
            return to_hex(txId.data(), txId.size());
        }
    }

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

    bool existsJsonParam(const nlohmann::json& params, const std::string& name)
    {
        return params.find(name) != params.end();
    }

    void checkJsonParam(const nlohmann::json& params, const std::string& name, int id)
    {
        if (!existsJsonParam(params, name))
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

    void WalletApi::onValidateAddressMessage(int id, const nlohmann::json& params)
    {
        checkJsonParam(params, "address", id);

        if (params["address"].empty())
            throwInvalidJsonRpc(id);

        ValidateAddress validateAddress;
        validateAddress.address.FromHex(params["address"]);

        _handler.onMessage(id, validateAddress);
    }

    void WalletApi::onSendMessage(int id, const nlohmann::json& params)
    {
        checkJsonParam(params, "session", id);
        checkJsonParam(params, "value", id);
        checkJsonParam(params, "address", id);

        if (params["session"] < 0)
            throwInvalidJsonRpc(id);

        if (params["value"] <= 0)
            throwInvalidJsonRpc(id);

        if (params["address"].empty())
            throwInvalidJsonRpc(id);

        Send send;
        send.session = params["session"];
        send.value = params["value"];
        send.address.FromHex(params["address"]);

        if (existsJsonParam(params, "fee"))
        {
            if(params["fee"] < 0)
                throwInvalidJsonRpc(id);

            send.fee = params["fee"];
        }
        else send.fee = 0;

        if (existsJsonParam(params, "comment"))
        {
            send.comment = params["comment"];
        }

        _handler.onMessage(id, send);
    }

    void WalletApi::onReplaceMessage(int id, const nlohmann::json& params)
    {
        Replace replace;
        _handler.onMessage(id, replace);
    }

    void WalletApi::onStatusMessage(int id, const nlohmann::json& params)
    {
        checkJsonParam(params, "txId", id);

        Status status;

        auto txId = from_hex(params["txId"]);

        if (txId.size() != status.txId.size())
            throwInvalidJsonRpc(id);

        std::copy_n(txId.begin(), status.txId.size(), status.txId.begin());

        _handler.onMessage(id, status);
    }

    void WalletApi::onSplitMessage(int id, const nlohmann::json& params)
    {
        checkJsonParam(params, "session", id);
        checkJsonParam(params, "coins", id);

        if (params["session"] < 0)
            throwInvalidJsonRpc(id);

        if (!params["coins"].is_array() || params["coins"].size() <= 0)
            throwInvalidJsonRpc(id);

        Split split;
        split.session = params["session"];

        for (const auto& amount : params["coins"])
        {
            split.coins.push_back(amount);
        }

        if (existsJsonParam(params, "fee"))
        {
            if (params["fee"] < 0)
                throwInvalidJsonRpc(id);

            split.fee = params["fee"];
        }
        else split.fee = 0;

        _handler.onMessage(id, split);
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

    void WalletApi::onListMessage(int id, const nlohmann::json& params)
    {
        List list;

        if (existsJsonParam(params, "filter"))
        {
            if (existsJsonParam(params["filter"], "status")
                && params["filter"]["status"].is_number_unsigned())
            {
                list.filter.status = params["filter"]["status"];
            }

            if (existsJsonParam(params["filter"], "height")
                && params["filter"]["height"].is_number_unsigned())
            {
                list.filter.height = params["filter"]["height"];
            }
        }

        _handler.onMessage(id, list);
    }

    void WalletApi::onWalletStatusMessage(int id, const nlohmann::json& params)
    {
        WalletStatus walletStatus;
        _handler.onMessage(id, walletStatus);
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

    void WalletApi::getResponse(int id, const ValidateAddress::Response& res, json& msg)
    {
        msg = json
        {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"result", res.isValid}
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
            std::string createTxId = utxo.m_createTxId.is_initialized() ? txIDToString(*utxo.m_createTxId) : "";
            std::string spentTxId = utxo.m_spentTxId.is_initialized() ? txIDToString(*utxo.m_spentTxId) : "";

            msg["result"].push_back(
            { 
                {"id", utxo.m_ID.m_Idx},
                {"amount", utxo.m_ID.m_Value},
                {"type", (const char*)FourCC::Text(utxo.m_ID.m_Type)},
                {"height", utxo.m_createHeight},
                {"maturity", utxo.m_maturity},
                {"createTxId", createTxId},
                {"spentTxId", spentTxId},
                {"sessionId", utxo.m_sessionId},
            });
        }
    }

    void WalletApi::getResponse(int id, const Send::Response& res, json& msg)
    {
        msg = json
        {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"result", 
                {
                    {"txId", txIDToString(res.txId)}
                }
            }
        };
    }

    static void getStatusResponseJson(const TxDescription& tx, json& msg)
    {
        msg = json
        {
            {"status", tx.m_status},
            {"sender", std::to_string(tx.m_myId)},
            {"receiver", std::to_string(tx.m_peerId)},
            {"fee", tx.m_fee},
            {"value", tx.m_amount},
            {"comment", std::string{ tx.m_message.begin(), tx.m_message.end() }},
            {"kernel", to_hex(tx.m_kernelID.m_pData, tx.m_kernelID.nBytes)},
            {"height", tx.m_minHeight}
        };
    }

    void WalletApi::getResponse(int id, const Status::Response& res, json& msg)
    {
        msg = json
        {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"result", {}}
        };

        getStatusResponseJson(res.tx, msg["result"]);
    }

    void WalletApi::getResponse(int id, const Split::Response& res, json& msg)
    {
        msg = json
        {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"result",
                {
                    {"txId", to_hex(res.txId.data(), res.txId.size())}
                }
            }
        };
    }

    void WalletApi::getResponse(int id, const List::Response& res, json& msg)
    {
        msg = json
        {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"result", json::array()}
        };

        for (const auto& tx : res.list)
        {
            json item = {};
            getStatusResponseJson(tx, item);
            msg["result"].push_back(item);
        }
    }

    void WalletApi::getResponse(int id, const WalletStatus::Response& res, json& msg)
    {
        msg = json
        {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"result",
                {
                    {"current_height", res.currentHeight},
                    {"current_state_hash", res.currentStateHash},
                    {"available", res.available},
                    {"receiving", res.receiving},
                    {"sending", res.sending},
                    {"maturing", res.maturing},
                    {"locked", res.locked},
                }
            }
        };
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
            if (_methods.find(msg["method"]) == _methods.end()) throwUnknownJsonRpc(msg["id"]);

            try
            {
                _methods[msg["method"]](msg["id"], msg["params"] == nullptr ? json::object() : msg["params"]);
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
