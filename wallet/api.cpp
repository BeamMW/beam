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

namespace beam::wallet
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

    WalletApi::WalletApi(IWalletApiHandler& handler, ACL acl)
        : _handler(handler)
        , _acl(acl)
    {
#define REG_FUNC(api, name, writeAccess) \
        _methods[name] = {BIND_THIS_MEMFN(on##api##Message), writeAccess};

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

    void FillAddressData(int id, const nlohmann::json& params, AddressData& data)
    {
        if (existsJsonParam(params, "comment"))
        {
            std::string comment = params["comment"];

            data.comment = comment;
        }

        if (existsJsonParam(params, "expiration"))
        {
            std::string expiration = params["expiration"];

            static std::map<std::string, AddressData::Expiration> Items =
            {
                {"expired", AddressData::Expired},
                {"24h",  AddressData::OneDay},
                {"never", AddressData::Never},
            };

            if(Items.count(expiration) == 0) throwInvalidJsonRpc(id);

            data.expiration = Items[expiration];
        }
    }

    void WalletApi::onCreateAddressMessage(int id, const nlohmann::json& params)
    {
        CreateAddress createAddress;
        FillAddressData(id, params, createAddress);

        _handler.onMessage(id, createAddress);
    }

    void WalletApi::onDeleteAddressMessage(int id, const nlohmann::json& params)
    {
        checkJsonParam(params, "address", id);

        DeleteAddress deleteAddress;
        deleteAddress.address.FromHex(params["address"]);

        _handler.onMessage(id, deleteAddress);
    }

    void WalletApi::onEditAddressMessage(int id, const nlohmann::json& params)
    {
        checkJsonParam(params, "address", id);

        if (!existsJsonParam(params, "comment") && !existsJsonParam(params, "expiration"))
            throwInvalidJsonRpc(id);

        EditAddress editAddress;
        editAddress.address.FromHex(params["address"]);

        FillAddressData(id, params, editAddress);


        _handler.onMessage(id, editAddress);
    }

    void WalletApi::onAddrListMessage(int id, const nlohmann::json& params)
    {
        checkJsonParam(params, "own", id);

        AddrList addrList;

        addrList.own = params["own"];

        _handler.onMessage(id, addrList);
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

    static CoinIDList readCoinsParameter(int id, const nlohmann::json& params)
    {
        CoinIDList coins;

        if (!params["coins"].is_array() || params["coins"].size() <= 0)
            throw jsonrpc_exception{ INVALID_PARAMS_JSON_RPC , "Invalid 'coins' parameter.", id };

        for (const auto& cid : params["coins"])
        {
            bool done = false;

            if (cid.is_string())
            {
                auto coinId = Coin::FromString(cid);

                if (coinId)
                {
                    coins.push_back(*coinId);
                    done = true;
                }
            }

            if (!done)
                throw jsonrpc_exception{ INVALID_PARAMS_JSON_RPC , "Invalid 'coin ID' parameter.", id };
        }

        return coins;
    }

    static uint64_t readSessionParameter(int id, const nlohmann::json& params)
    {
        uint64_t session = 0;

        if (params["session"] > 0)
        {
            session = params["session"];
        }
        else throw jsonrpc_exception{ INVALID_JSON_RPC , "Invalid 'session' parameter.", id };

        return session;
    }

    void WalletApi::onSendMessage(int id, const nlohmann::json& params)
    {
        checkJsonParam(params, "value", id);
        checkJsonParam(params, "address", id);

        if (params["value"] <= 0)
            throwInvalidJsonRpc(id);

        if (params["address"].empty())
            throwInvalidJsonRpc(id);

        Send send;
        send.value = params["value"];

        if (existsJsonParam(params, "coins"))
        {
            send.coins = readCoinsParameter(id, params);
        }
        else if (existsJsonParam(params, "session"))
        {
            send.session = readSessionParameter(id, params);
        }

        if (!send.address.FromHex(params["address"]))
        {
            throw jsonrpc_exception{ INVALID_PARAMS_JSON_RPC , "Invalid receiver address.", id };
        }

        if (existsJsonParam(params, "from"))
        {
            WalletID from(Zero);
            if (from.FromHex(params["from"]))
            {
                send.from = from;
            }
            else
            {
                throw jsonrpc_exception{ INVALID_PARAMS_JSON_RPC , "Invalid sender address.", id };
            }
        }

        if (existsJsonParam(params, "fee"))
        {
            if(params["fee"] < 0)
                throwInvalidJsonRpc(id);

            send.fee = params["fee"];
        }

        if (existsJsonParam(params, "comment"))
        {
            send.comment = params["comment"];
        }

        _handler.onMessage(id, send);
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
        checkJsonParam(params, "coins", id);

        if (!params["coins"].is_array() || params["coins"].size() <= 0)
            throwInvalidJsonRpc(id);

        Split split;

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

    void WalletApi::onTxCancelMessage(int id, const nlohmann::json& params)
    {
        checkJsonParam(params, "txId", id);
        auto txId = from_hex(params["txId"]);

        TxCancel txCancel;

        if (txId.size() != txCancel.txId.size())
            throwInvalidJsonRpc(id);

        std::copy_n(txId.begin(), txCancel.txId.size(), txCancel.txId.begin());

        _handler.onMessage(id, txCancel);
    }

    void WalletApi::onTxDeleteMessage(int id, const nlohmann::json& params)
    {
        checkJsonParam(params, "txId", id);
        auto txId = from_hex(params["txId"]);

        TxDelete txDelete;

        if (txId.size() != txDelete.txId.size())
            throwInvalidJsonRpc(id);

        std::copy_n(txId.begin(), txDelete.txId.size(), txDelete.txId.begin());

        _handler.onMessage(id, txDelete);
    }

    void WalletApi::onGetUtxoMessage(int id, const nlohmann::json& params)
    {
        GetUtxo getUtxo;

        if (existsJsonParam(params, "count"))
        {
            if (params["count"] > 0)
            {
                getUtxo.count = params["count"];
            }
            else throw jsonrpc_exception{ INVALID_JSON_RPC , "Invalid 'count' parameter.", id };
        }

        if (existsJsonParam(params, "skip"))
        {
            if (params["skip"] >= 0)
            {
                getUtxo.skip = params["skip"];
            }
            else throw jsonrpc_exception{ INVALID_JSON_RPC , "Invalid 'skip' parameter.", id };
        }

        _handler.onMessage(id, getUtxo);
    }

    void WalletApi::onLockMessage(int id, const nlohmann::json& params)
    {
        checkJsonParam(params, "coins", id);
        checkJsonParam(params, "session", id);

        Lock lock;

        lock.session = readSessionParameter(id, params);
        lock.coins = readCoinsParameter(id, params);

        _handler.onMessage(id, lock);
    }

    void WalletApi::onUnlockMessage(int id, const nlohmann::json& params)
    {
        checkJsonParam(params, "session", id);

        Unlock unlock;

        unlock.session = readSessionParameter(id, params);

        _handler.onMessage(id, unlock);
    }

    void WalletApi::onTxListMessage(int id, const nlohmann::json& params)
    {
        TxList txList;

        if (existsJsonParam(params, "filter"))
        {
            if (existsJsonParam(params["filter"], "status")
                && params["filter"]["status"].is_number_unsigned())
            {
                txList.filter.status = (TxStatus)params["filter"]["status"];
            }

            if (existsJsonParam(params["filter"], "height")
                && params["filter"]["height"].is_number_unsigned())
            {
                txList.filter.height = (Height)params["filter"]["height"];
            }
        }

        if (existsJsonParam(params, "count"))
        {
            if (params["count"] > 0)
            {
                txList.count = params["count"];
            }
            else throw jsonrpc_exception{ INVALID_JSON_RPC , "Invalid 'count' parameter.", id };
        }

        if (existsJsonParam(params, "skip"))
        {
            if (params["skip"] >= 0)
            {
                txList.skip = params["skip"];
            }
            else throw jsonrpc_exception{ INVALID_JSON_RPC , "Invalid 'skip' parameter.", id };
        }

        _handler.onMessage(id, txList);
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

    void WalletApi::getResponse(int id, const DeleteAddress::Response& res, json& msg)
    {
        msg = json
        {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"result", "done"}
        };
    }

    void WalletApi::getResponse(int id, const EditAddress::Response& res, json& msg)
    {
        msg = json
        {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"result", "done"}
        };
    }

    void WalletApi::getResponse(int id, const AddrList::Response& res, json& msg)
    {
        msg = json
        {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"result", json::array()}
        };

        for (auto& addr : res.list)
        {
            msg["result"].push_back(
            {
                {"address", std::to_string(addr.m_walletID)},
                {"comment", addr.m_label},
                {"category", addr.m_category},
                {"create_time", addr.getCreateTime()},
                {"duration", addr.m_duration},
                {"expired", addr.isExpired()},
                {"own", addr.m_OwnID != 0}
            });
        }
    }

    void WalletApi::getResponse(int id, const ValidateAddress::Response& res, json& msg)
    {
        msg = json
        {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"result", 
                {
                    {"is_valid",  res.isValid},
                    {"is_mine",  res.isMine},
                }
            }
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
                {"id", utxo.toStringID()},
                {"amount", utxo.m_ID.m_Value},
                {"type", (const char*)FourCC::Text(utxo.m_ID.m_Type)},
                {"maturity", utxo.get_Maturity()},
                {"createTxId", createTxId},
                {"spentTxId", spentTxId},
                {"status", utxo.m_status},
                {"status_string", utxo.getStatusString()},
                {"session", utxo.m_sessionId}
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

    static void getStatusResponseJson(const TxDescription& tx, json& msg, Height kernelProofHeight, Height systemHeight)
    {
        msg = json
        {
            {"txId", txIDToString(tx.m_txId)},
            {"status", tx.m_status},
            {"status_string", tx.getStatusString()},
            {"sender", std::to_string(tx.m_sender ? tx.m_myId : tx.m_peerId)},
            {"receiver", std::to_string(tx.m_sender ? tx.m_peerId : tx.m_myId)},
            {"fee", tx.m_fee},
            {"value", tx.m_amount},
            {"comment", std::string{ tx.m_message.begin(), tx.m_message.end() }},
            {"create_time", tx.m_createTime},            
            {"income", !tx.m_sender}
        };

        if (kernelProofHeight > 0)
        {
            msg["height"] = kernelProofHeight;

            if (systemHeight >= kernelProofHeight)
            {
                msg["confirmations"] = systemHeight - kernelProofHeight;
            }
        }

        if (tx.m_status == TxStatus::Failed)
        {
            msg["failure_reason"] = wallet::GetFailureMessage(tx.m_failureReason);
        }
        else if (tx.m_status != TxStatus::Cancelled)
        {
            msg["kernel"] = to_hex(tx.m_kernelID.m_pData, tx.m_kernelID.nBytes);
        }
    }

    void WalletApi::getResponse(int id, const Status::Response& res, json& msg)
    {
        msg = json
        {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"result", {}}
        };

        getStatusResponseJson(res.tx, msg["result"], res.kernelProofHeight, res.systemHeight);
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

    void WalletApi::getResponse(int id, const TxCancel::Response& res, json& msg)
    {
        msg = json
        {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"result", res.result}
        };
    }

    void WalletApi::getResponse(int id, const TxDelete::Response& res, json& msg)
    {
        msg = json
        {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"result", res.result}
        };
    }

    void WalletApi::getResponse(int id, const TxList::Response& res, json& msg)
    {
        msg = json
        {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"result", json::array()}
        };

        for (const auto& resItem : res.resultList)
        {
            json item = {};
            getStatusResponseJson(resItem.tx, item, resItem.kernelProofHeight, resItem.systemHeight);
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
                    {"current_state_hash", to_hex(res.currentStateHash.m_pData, res.currentStateHash.nBytes)},
                    {"prev_state_hash", to_hex(res.prevStateHash.m_pData, res.prevStateHash.nBytes)},
                    {"available", res.available},
                    {"receiving", res.receiving},
                    {"sending", res.sending},
                    {"maturing", res.maturing},
                    {"difficulty", res.difficulty},
                }
            }
        };
    }

    void WalletApi::getResponse(int id, const Lock::Response& res, json& msg)
    {
        msg = json
        {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"result", res.result}
        };        
    }

    void WalletApi::getResponse(int id, const Unlock::Response& res, json& msg)
    {
        msg = json
        {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"result", res.result}
        };
    }

    bool WalletApi::parse(const char* data, size_t size)
    {
        if (size == 0)
        {
            json msg
            {
                {"jsonrpc", "2.0"},
                {"error",
                    {
                        {"code", INVALID_JSON_RPC},
                        {"message", "Empty JSON request."},
                    }
                }
            };

            _handler.onInvalidJsonRpc(msg);
            return false;
        }

        try
        {
            json msg = json::parse(data, data + size);

            if (msg["jsonrpc"] != "2.0") throwInvalidJsonRpc();
            if (msg["id"] <= 0) throwInvalidJsonRpc();

            if (_acl)
            {
                if (msg["key"] == nullptr) throw jsonrpc_exception{ INVALID_PARAMS_JSON_RPC , "API key not specified.", msg["id"] };
                if (_acl->count(msg["key"]) == 0) throw jsonrpc_exception{ UNKNOWN_API_KEY , "Unknown API key.", msg["id"] };
            }

            if (msg["method"] == nullptr) throwInvalidJsonRpc();
            if (_methods.find(msg["method"]) == _methods.end()) throwUnknownJsonRpc(msg["id"]);

            try
            {
                auto& info = _methods[msg["method"]];

                if(_acl && info.writeAccess && _acl.get()[msg["key"]] == false)
                {
                    throw jsonrpc_exception{ INVALID_PARAMS_JSON_RPC , "User doesn't have permissions to call this method.", msg["id"] };
                }

                info.func(msg["id"], msg["params"] == nullptr ? json::object() : msg["params"]);
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
            json msg
            {
                {"jsonrpc", "2.0"},
                {"error",
                    {
                        {"code", INTERNAL_JSON_RPC_ERROR},
                        {"message", e.what()},
                    }
                }
            };

            _handler.onInvalidJsonRpc(msg);
        }

        return true;
    }
}
