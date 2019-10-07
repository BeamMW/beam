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
    static const char JsonRpcHrd[] = "jsonrpc";
    static const char JsonRpcVerHrd[] = "2.0";

    namespace
    {
        std::string txIDToString(const TxID& txId)
        {
            return to_hex(txId.data(), txId.size());
        }
    }

    struct jsonrpc_exception
    {
        ApiError code;
        std::string data;
        JsonRpcId id;
    };

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

    void checkJsonParam(const nlohmann::json& params, const std::string& name, const JsonRpcId& id)
    {
        if (!existsJsonParam(params, name))
            throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "Parameter '" + name + "' doesn't exist.", id };
    }

    void FillAddressData(const JsonRpcId& id, const nlohmann::json& params, AddressData& data)
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

            if(Items.count(expiration) == 0)
                throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "Unknown value for the 'expiration' parameter.", id };

            data.expiration = Items[expiration];
        }
    }

    void WalletApi::onCreateAddressMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        CreateAddress createAddress;
        FillAddressData(id, params, createAddress);

        _handler.onMessage(id, createAddress);
    }

    void WalletApi::onDeleteAddressMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        checkJsonParam(params, "address", id);

        DeleteAddress deleteAddress;
        deleteAddress.address.FromHex(params["address"]);

        _handler.onMessage(id, deleteAddress);
    }

    void WalletApi::onEditAddressMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        checkJsonParam(params, "address", id);

        if (!existsJsonParam(params, "comment") && !existsJsonParam(params, "expiration"))
            throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "Comment or Expiration parameter must be specified.", id };

        EditAddress editAddress;
        editAddress.address.FromHex(params["address"]);

        FillAddressData(id, params, editAddress);


        _handler.onMessage(id, editAddress);
    }

    void WalletApi::onAddrListMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        checkJsonParam(params, "own", id);

        AddrList addrList;

        addrList.own = params["own"];

        _handler.onMessage(id, addrList);
    }

    void WalletApi::onValidateAddressMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        checkJsonParam(params, "address", id);

        if (params["address"].empty())
            throw jsonrpc_exception{ ApiError::InvalidAddress, "Address is empty.", id };

        ValidateAddress validateAddress;
        validateAddress.address.FromHex(params["address"]);

        _handler.onMessage(id, validateAddress);
    }

    static CoinIDList readCoinsParameter(const JsonRpcId& id, const nlohmann::json& params)
    {
        CoinIDList coins;

        if (!params["coins"].is_array() || params["coins"].size() <= 0)
            throw jsonrpc_exception{ ApiError::InvalidParamsJsonRpc , "Invalid 'coins' parameter.", id };

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
                throw jsonrpc_exception{ ApiError::InvalidParamsJsonRpc , "Invalid 'coin ID' parameter.", id };
        }

        return coins;
    }

    uint64_t readSessionParameter(const JsonRpcId& id, const nlohmann::json& params)
    {
        uint64_t session = 0;

        if (params["session"] > 0)
        {
            session = params["session"];
        }
        else throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "Invalid 'session' parameter.", id };

        return session;
    
    }

    void checkTxId(const ByteBuffer& txId, const JsonRpcId& id)
    {
        if (txId.size() != TxID().size())
            throw jsonrpc_exception{ ApiError::InvalidTxId, "Transaction ID has wrong format.", id };
    }

    boost::optional<TxID> readTxIdParameter(const JsonRpcId& id, const nlohmann::json& params)
    {
        boost::optional<TxID> txId;

        if (existsJsonParam(params, "txId"))
        {
            TxID txIdDst;
            auto txIdSrc = from_hex(params["txId"]);

            checkTxId(txIdSrc, id);

            std::copy_n(txIdSrc.begin(), TxID().size(), txIdDst.begin());
            txId = txIdDst;
        }

        return txId;
    }

    void WalletApi::onSendMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        checkJsonParam(params, "value", id);
        checkJsonParam(params, "address", id);

        if (!params["value"].is_number_unsigned() || params["value"] == 0)
            throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "Value must be non zero 64bit unsigned integer.", id };

        if (params["address"].empty())
            throw jsonrpc_exception{ ApiError::InvalidAddress, "Address is empty.", id };

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
            throw jsonrpc_exception{ ApiError::InvalidAddress , "Invalid receiver address.", id };
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
                throw jsonrpc_exception{ ApiError::InvalidAddress, "Invalid sender address.", id };
            }
        }

        if (existsJsonParam(params, "fee"))
        {
            if(!params["fee"].is_number_unsigned() || params["fee"] == 0)
                throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "Invalid fee.", id };

            send.fee = params["fee"];
        }

        if (existsJsonParam(params, "comment"))
        {
            send.comment = params["comment"];
        }

        send.txId = readTxIdParameter(id, params);

        _handler.onMessage(id, send);
    }

    void WalletApi::onStatusMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        checkJsonParam(params, "txId", id);

        Status status;

        auto txId = from_hex(params["txId"]);

        checkTxId(txId, id);

        std::copy_n(txId.begin(), status.txId.size(), status.txId.begin());

        _handler.onMessage(id, status);
    }

    void WalletApi::onSplitMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        checkJsonParam(params, "coins", id);

        if (!params["coins"].is_array() || params["coins"].size() <= 0)
            throw jsonrpc_exception{ ApiError::InvalidParamsJsonRpc, "Coins parameter must be a nonempty array.", id };

        Split split;

        for (const auto& amount : params["coins"])
        {
            if(!amount.is_number_unsigned() || amount == 0)
                throw jsonrpc_exception{ ApiError::InvalidParamsJsonRpc, "Coin amount must be non zero 64bit unsigned integer.", id };

            split.coins.push_back(amount);
        }

        if (existsJsonParam(params, "fee"))
        {
            if (!params["fee"].is_number_unsigned() || params["fee"] == 0)
                throw jsonrpc_exception{ ApiError::InvalidParamsJsonRpc, "Invalid fee.", id };

            split.fee = params["fee"];
        }
        else
        {
            split.fee = std::max(wallet::GetMinimumFee(split.coins.size() + 1), DefaultFee); // +1 extra output for change
        }

        split.txId = readTxIdParameter(id, params);

        _handler.onMessage(id, split);
    }

    void WalletApi::onTxCancelMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        checkJsonParam(params, "txId", id);
        auto txId = from_hex(params["txId"]);

        TxCancel txCancel;

        checkTxId(txId, id);

        std::copy_n(txId.begin(), txCancel.txId.size(), txCancel.txId.begin());

        _handler.onMessage(id, txCancel);
    }

    void WalletApi::onTxDeleteMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        checkJsonParam(params, "txId", id);
        auto txId = from_hex(params["txId"]);

        TxDelete txDelete;

        checkTxId(txId, id);

        std::copy_n(txId.begin(), txDelete.txId.size(), txDelete.txId.begin());

        _handler.onMessage(id, txDelete);
    }

    void WalletApi::onGetUtxoMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        GetUtxo getUtxo;

        if (existsJsonParam(params, "count"))
        {
            if (params["count"] > 0)
            {
                getUtxo.count = params["count"];
            }
            else throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "Invalid 'count' parameter.", id };
        }

        if (existsJsonParam(params, "skip"))
        {
            if (params["skip"] >= 0)
            {
                getUtxo.skip = params["skip"];
            }
            else throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "Invalid 'skip' parameter.", id };
        }

        _handler.onMessage(id, getUtxo);
    }

    void WalletApi::onLockMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        checkJsonParam(params, "coins", id);
        checkJsonParam(params, "session", id);

        Lock lock;

        lock.session = readSessionParameter(id, params);
        lock.coins = readCoinsParameter(id, params);

        _handler.onMessage(id, lock);
    }

    void WalletApi::onUnlockMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        checkJsonParam(params, "session", id);

        Unlock unlock;

        unlock.session = readSessionParameter(id, params);

        _handler.onMessage(id, unlock);
    }

    void WalletApi::onTxListMessage(const JsonRpcId& id, const nlohmann::json& params)
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
            else throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "Invalid 'count' parameter.", id };
        }

        if (existsJsonParam(params, "skip"))
        {
            if (params["skip"] >= 0)
            {
                txList.skip = params["skip"];
            }
            else throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "Invalid 'skip' parameter.", id };
        }

        _handler.onMessage(id, txList);
    }

    void WalletApi::onWalletStatusMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        WalletStatus walletStatus;
        _handler.onMessage(id, walletStatus);
    }

    void WalletApi::onGenerateTxIdMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        GenerateTxId generateTxId;
        _handler.onMessage(id, generateTxId);
    }

    void WalletApi::getResponse(const JsonRpcId& id, const CreateAddress::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHrd, JsonRpcVerHrd},
            {"id", id},
            {"result", std::to_string(res.address)}
        };
    }

    void WalletApi::getResponse(const JsonRpcId& id, const DeleteAddress::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHrd, JsonRpcVerHrd},
            {"id", id},
            {"result", "done"}
        };
    }

    void WalletApi::getResponse(const JsonRpcId& id, const EditAddress::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHrd, JsonRpcVerHrd},
            {"id", id},
            {"result", "done"}
        };
    }

    void WalletApi::getResponse(const JsonRpcId& id, const AddrList::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHrd, JsonRpcVerHrd},
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

    void WalletApi::getResponse(const JsonRpcId& id, const ValidateAddress::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHrd, JsonRpcVerHrd},
            {"id", id},
            {"result", 
                {
                    {"is_valid",  res.isValid},
                    {"is_mine",  res.isMine},
                }
            }
        };
    }

    void WalletApi::getResponse(const JsonRpcId& id, const GetUtxo::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHrd, JsonRpcVerHrd},
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

    void WalletApi::getResponse(const JsonRpcId& id, const Send::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHrd, JsonRpcVerHrd},
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
        else if (tx.m_status != TxStatus::Canceled)
        {
            msg["kernel"] = to_hex(tx.m_kernelID.m_pData, tx.m_kernelID.nBytes);
        }
    }

    void WalletApi::getResponse(const JsonRpcId& id, const Status::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHrd, JsonRpcVerHrd},
            {"id", id},
            {"result", {}}
        };

        getStatusResponseJson(res.tx, msg["result"], res.kernelProofHeight, res.systemHeight);
    }

    void WalletApi::getResponse(const JsonRpcId& id, const Split::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHrd, JsonRpcVerHrd},
            {"id", id},
            {"result",
                {
                    {"txId", to_hex(res.txId.data(), res.txId.size())}
                }
            }
        };
    }

    void WalletApi::getResponse(const JsonRpcId& id, const TxCancel::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHrd, JsonRpcVerHrd},
            {"id", id},
            {"result", res.result}
        };
    }

    void WalletApi::getResponse(const JsonRpcId& id, const TxDelete::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHrd, JsonRpcVerHrd},
            {"id", id},
            {"result", res.result}
        };
    }

    void WalletApi::getResponse(const JsonRpcId& id, const TxList::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHrd, JsonRpcVerHrd},
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

    void WalletApi::getResponse(const JsonRpcId& id, const WalletStatus::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHrd, JsonRpcVerHrd},
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

    void WalletApi::getResponse(const JsonRpcId& id, const GenerateTxId::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHrd, JsonRpcVerHrd},
            {"id", id},
            {"result", txIDToString(res.txId)}
        };
    }

    void WalletApi::getResponse(const JsonRpcId& id, const Lock::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHrd, JsonRpcVerHrd},
            {"id", id},
            {"result", res.result}
        };        
    }

    void WalletApi::getResponse(const JsonRpcId& id, const Unlock::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHrd, JsonRpcVerHrd},
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
                {JsonRpcHrd, JsonRpcVerHrd},
                {"error",
                    {
                        {"code", ApiError::InvalidJsonRpc},
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

            if(!msg["id"].is_number_integer() 
                && !msg["id"].is_string())
                throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "ID can be integer or string only." };

            JsonRpcId id = msg["id"];

            if (msg[JsonRpcHrd] != JsonRpcVerHrd) 
                throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "Invalid JSON-RPC 2.0 header.", id };

            if (_acl)
            {
                if (msg["key"] == nullptr) 
                    throw jsonrpc_exception{ ApiError::InvalidParamsJsonRpc , "API key not specified.", id };

                if (_acl->count(msg["key"]) == 0) 
                    throw jsonrpc_exception{ ApiError::UnknownApiKey , msg["key"], id };
            }

            checkJsonParam(msg, "method", id);

            JsonRpcId method = msg["method"];

            if (_methods.find(method) == _methods.end())
            {
                throw jsonrpc_exception{ ApiError::NotFoundJsonRpc, method, id };
            }

            try
            {
                auto& info = _methods[method];

                if(_acl && info.writeAccess && _acl.get()[msg["key"]] == false)
                {
                    throw jsonrpc_exception{ ApiError::InvalidParamsJsonRpc , "User doesn't have permissions to call this method.", id };
                }

                info.func(id, msg["params"] == nullptr ? json::object() : msg["params"]);
            }
            catch (const nlohmann::detail::exception& e)
            {
                LOG_ERROR() << "json parse: " << e.what() << "\n" << getJsonString(data, size);

                throw jsonrpc_exception{ ApiError::InvalidJsonRpc , e.what(), id };
            }
        }
        catch (const jsonrpc_exception& e)
        {
            json msg
            {
                {JsonRpcHrd, JsonRpcVerHrd},
                {"error",
                    {
                        {"code", e.code},
                        {"message", getErrorMessage(e.code)},
                    }
                }
            };

            if (!e.data.empty())
            {
                msg["error"]["data"] = e.data;
            }

            if (e.id.is_number_integer() || e.id.is_string()) msg["id"] = e.id;
            else msg.erase("id");

            _handler.onInvalidJsonRpc(msg);
        }
        catch (const std::exception& e)
        {
            json msg
            {
                {JsonRpcHrd, JsonRpcVerHrd},
                {"error",
                    {
                        {"code", ApiError::InternalErrorJsonRpc},
                        {"message", e.what()},
                    }
                }
            };

            _handler.onInvalidJsonRpc(msg);
        }

        return true;
    }

    const char* WalletApi::getErrorMessage(ApiError code)
    {
#define ERROR_ITEM(_, item, info) case item: return info;
        switch (code) { JSON_RPC_ERRORS(ERROR_ITEM) }
#undef ERROR_ITEM

        assert(false);

        return "unknown error.";
    }
}
