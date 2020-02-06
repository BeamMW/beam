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

#define LOG_VERBOSE_ENABLED 1
#include "utility/logger.h"

#include "service.h"

#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <map>
#include <queue>

#include "utility/cli/options.h"
#include "utility/helpers.h"
#include "utility/io/timer.h"
#include "utility/io/json_serializer.h"
#include "utility/string_helpers.h"
#include "utility/log_rotation.h"

#include "p2p/line_protocol.h"

#include "wallet/core/wallet_db.h"
#include "wallet/core/wallet_network.h"
#include "wallet/core/simple_transaction.h"
#include "keykeeper/local_private_key_keeper.h"

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/uuid/uuid_generators.hpp>

#include "nlohmann/json.hpp"
#include "version.h"

#include "keykeeper/wasm_key_keeper.h"

using json = nlohmann::json;

static const unsigned LOG_ROTATION_PERIOD = 3 * 60 * 60 * 1000; // 3 hours
static const size_t PACKER_FRAGMENTS_SIZE = 4096;

using namespace beam;
using namespace beam::wallet;
using tcp = boost::asio::ip::tcp;               // from <boost/asio/ip/tcp.hpp>

namespace websocket = boost::beast::websocket;  // from <boost/beast/websocket.hpp>

namespace beam::wallet
{
    io::Address node_addr;

    // !TODO: temporary solution, just to reuse already opened wallet DB
    // DB shouldn't be locked in normal case
    static std::map<std::string, IWalletDB::Ptr> WalletsMap;

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

    // uint64_t readSessionParameter(const JsonRpcId& id, const nlohmann::json& params)
    // {
    //     uint64_t session = 0;

    //     if (params["session"] > 0)
    //     {
    //         session = params["session"];
    //     }
    //     else throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "Invalid 'session' parameter.", id };

    //     return session;
    
    // }

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
        // else if (existsJsonParam(params, "session"))
        // {
        //     send.session = readSessionParameter(id, params);
        // }

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

    // void WalletApi::onLockMessage(const JsonRpcId& id, const nlohmann::json& params)
    // {
    //     checkJsonParam(params, "coins", id);
    //     checkJsonParam(params, "session", id);

    //     Lock lock;

    //     lock.session = readSessionParameter(id, params);
    //     lock.coins = readCoinsParameter(id, params);

    //     _handler.onMessage(id, lock);
    // }

    // void WalletApi::onUnlockMessage(const JsonRpcId& id, const nlohmann::json& params)
    // {
    //     checkJsonParam(params, "session", id);

    //     Unlock unlock;

    //     unlock.session = readSessionParameter(id, params);

    //     _handler.onMessage(id, unlock);
    // }

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

    void WalletApi::onCreateWalletMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        CreateWallet createWallet;

        if (existsJsonParam(params, "pass"))
        {
            createWallet.pass = params["pass"];
        }
        else throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "'pass' parameter must be specified.", id };

        if (existsJsonParam(params, "ownerkey"))
        {
            createWallet.ownerKey = params["ownerkey"];
        }
        else throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "'ownerkey' parameter must be specified.", id };

        _handler.onMessage(id, createWallet);
    }

    void WalletApi::onOpenWalletMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        OpenWallet openWallet;

        if (existsJsonParam(params, "pass"))
        {
            openWallet.pass = params["pass"];
        }
        else throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "'pass' parameter must be specified.", id };

        if (existsJsonParam(params, "id"))
        {
            openWallet.id = params["id"];
        }
        else throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "'id' parameter must be specified.", id };

        _handler.onMessage(id, openWallet);
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

    void WalletApi::getResponse(const JsonRpcId& id, const CreateWallet::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHrd, JsonRpcVerHrd},
            {"id", id},
            {"result", res.id}
        };
    }

    void WalletApi::getResponse(const JsonRpcId& id, const OpenWallet::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHrd, JsonRpcVerHrd},
            {"id", id},
            {"result", res.session}
        };
    }

    // void WalletApi::getResponse(const JsonRpcId& id, const Lock::Response& res, json& msg)
    // {
    //     msg = json
    //     {
    //         {JsonRpcHrd, JsonRpcVerHrd},
    //         {"id", id},
    //         {"result", res.result}
    //     };        
    // }

    // void WalletApi::getResponse(const JsonRpcId& id, const Unlock::Response& res, json& msg)
    // {
    //     msg = json
    //     {
    //         {JsonRpcHrd, JsonRpcVerHrd},
    //         {"id", id},
    //         {"result", res.result}
    //     };
    // }

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

namespace
{
    std::string getMinimumFeeError(Amount minimumFee)
    {
        std::stringstream ss;
        ss << "Failed to initiate the send operation. The minimum fee is " << minimumFee << " GROTH.";
        return ss.str();
    }

    void fail(boost::system::error_code ec, char const* what)
    {
        LOG_ERROR() << what << ": " << ec.message();
    }

    class WalletApiServer
    {
        boost::asio::io_context _ioc;
        io::Timer::Ptr _iocTimer;

    public:
        WalletApiServer(io::Reactor::Ptr reactor, uint16_t port)
            : _ioc{1}
            , _reactor(reactor)
        {
            start(port);
        }

        ~WalletApiServer()
        {
            stop();
        }

    protected:

        void start(uint16_t port)
        {
            std::make_shared<listener>(_ioc, tcp::endpoint{boost::asio::ip::make_address("0.0.0.0"), port}, _reactor)->run();

            // !TODO: an attempt to run asio io_context near the libuv reactor
            // maybe, we could find a better solution here
            _iocTimer = io::Timer::create(*_reactor);
            _iocTimer->start(1, true, [this](){_ioc.poll();});
        }

        void stop()
        {

        }

    private:

        struct ApiConnectionHandler
        {
            virtual void serializeMsg(const json& msg) = 0;
        };

        class session;

        class WasmKeyKeeperProxy 
            : public PrivateKeyKeeper_AsyncNotify
            , public std::enable_shared_from_this<WasmKeyKeeperProxy>
        {
            session& _s;
            io::Reactor::Ptr _reactor;
        public:
            WasmKeyKeeperProxy(session& s, io::Reactor::Ptr reactor)
            : _s(s)
            , _reactor(reactor)
            {}
            virtual ~WasmKeyKeeperProxy(){}

            void InvokeAsync(Method::get_Kdf& x, const Handler::Ptr& h) override
            {
                json msg =
                {
                    {JsonRpcHrd, JsonRpcVerHrd},
                    {"id", 0},
                    {"method", "get_kdf"},
                    {"params",
                        {
                            {"root", x.m_Root},
                            {"child_key_num", x.m_iChild}
                        }
                    }
                };

                _s.call_keykeeper_method_async(msg, [this, &x, h](const json& msg)
                    {
                        Status::Type s = msg["status"];
                        if (s == Status::Success)
                        {
                            ByteBuffer buf = from_base64<ByteBuffer>(msg["kdf_pub"]);
                            ECC::HKdfPub::Packed* packed = reinterpret_cast<ECC::HKdfPub::Packed*>(&buf[0]);
                        
                            auto pubKdf = std::make_shared<ECC::HKdfPub>();
                            pubKdf->Import(*packed);
                            x.m_pPKdf = pubKdf;
                        }
                        PushOut(s, h);
                    });
            }

            void InvokeAsync(Method::get_NumSlots& x, const Handler::Ptr& h) override
            {
                json msg =
                {
                    {JsonRpcHrd, JsonRpcVerHrd},
                    {"id", 0},
                    {"method", "get_slots"}
                };

                _s.call_keykeeper_method_async(msg, [this, &x, h](const json& msg)
                {
                    Status::Type s = msg["status"];
                    if (s == Status::Success)
                    {
                        x.m_Count = msg["count"];
                    }
                    PushOut(s, h);
                });
            }

            void InvokeAsync(Method::CreateOutput& x, const Handler::Ptr& h) override
            {
                json msg =
                {
                    {JsonRpcHrd, JsonRpcVerHrd},
                    {"id", 0},
                    {"method", "create_output"},
                    {"params",
                        {
                            {"scheme", x.m_hScheme},
                            {"id", to_base64(x.m_Cid)}
                        }
                    }
                };

                _s.call_keykeeper_method_async(msg, [this, &x, h](const json& msg)
                    {
                        Status::Type s = msg["status"];
                        if (s == Status::Success)
                        {
                            x.m_pResult = from_base64<Output::Ptr>(msg["result"]);
                        }
                        PushOut(s, h);
                    });
            }

            void InvokeAsync(Method::SignReceiver& x, const Handler::Ptr& h) override
            {
                json msg =
                {
                    {JsonRpcHrd, JsonRpcVerHrd},
                    {"id", 0},
                    {"method", "sign_receiver"},
                    {"params",
                        {
                            {"inputs",    to_base64(x.m_vInputs)},
                            {"outputs",   to_base64(x.m_vOutputs)},
                            {"kernel",    to_base64(x.m_pKernel)},
                            {"non_conv",  x.m_NonConventional},
                            {"peer_id",   to_base64(x.m_Peer)},
                            {"my_id_key", to_base64(x.m_MyIDKey)}
                        }
                    }
                };

                _s.call_keykeeper_method_async(msg, [this, &x, h](const json& msg)
                    {
                        Status::Type s = msg["status"];
                        if (s == Status::Success)
                        {
                            x.m_kOffset = from_base64<ECC::Scalar>(msg["offset"]);
                            x.m_PaymentProofSignature = from_base64<ECC::Signature>(msg["payment_proof_sig"]);
                            x.m_pKernel->m_Signature = from_base64<ECC::Signature>(msg["sig"]);
                        }
                        PushOut(s, h);
                    });
            }

            void InvokeAsync(Method::SignSender& x, const Handler::Ptr& h) override
            {
                json msg =
                {
                    {JsonRpcHrd, JsonRpcVerHrd},
                    {"id", 0},
                    {"method", "sign_sender"},
                    {"params",
                        {
                            {"inputs",    to_base64(x.m_vInputs)},
                            {"outputs",   to_base64(x.m_vOutputs)},
                            {"kernel",    to_base64(x.m_pKernel)},
                            {"non_conv",  x.m_NonConventional},
                            {"peer_id",   to_base64(x.m_Peer)},
                            {"my_id_key", to_base64(x.m_MyIDKey)},
                            {"slot",      x.m_Slot},
                            {"agreement", to_base64(x.m_UserAgreement)},
                            {"my_id",     to_base64(x.m_MyID)}
                        }
                    }
                };

                _s.call_keykeeper_method_async(msg, [this, &x, h](const json& msg)
                    {
                        Status::Type s = msg["status"];
                        if (s == Status::Success)
                        {
                            x.m_pKernel->m_Signature = from_base64<ECC::Signature>(msg["sig"]);
                            if (x.m_UserAgreement == Zero)
                            {
                                x.m_UserAgreement = from_base64<ECC::Hash::Value>(msg["agreement"]);
                            }
                            else
                            {
                                x.m_kOffset = from_base64<ECC::Scalar>(msg["offset"]);
                                x.m_PaymentProofSignature = from_base64<ECC::Signature>(msg["payment_proof_sig"]);
                            }
                        }
                        PushOut(s, h);
                    });
            }

            void InvokeAsync(Method::SignSplit& x, const Handler::Ptr& h) override
            {
                json msg =
                {
                    {JsonRpcHrd, JsonRpcVerHrd},
                    {"id", 0},
                    {"method", "sign_split"},
                    {"params",
                        {
                            {"inputs",   to_base64(x.m_vInputs)},
                            {"outputs",  to_base64(x.m_vOutputs)},
                            {"kernel",   to_base64(x.m_pKernel)},
                            {"non_conv", x.m_NonConventional}
                        }
                    }
                };

                _s.call_keykeeper_method_async(msg, [this, &x, h](const json& msg)
                    {
                        Status::Type s = msg["status"];
                        if (s == Status::Success)
                        {
                            x.m_kOffset = from_base64<ECC::Scalar>(msg["offset"]);
                            x.m_pKernel->m_Signature = from_base64<ECC::Signature>(msg["sig"]);
                        }
                        PushOut(s, h);
                    });
            }

         //   void subscribe(Handler::Ptr handler) override
         //   {
         //       assert(!"not implemented.");
         //   }

        private:
            mutable Key::IKdf::Ptr _sbbsKdf;
        };
        
        class ApiConnection : IWalletApiHandler, IWalletDbObserver
        {
            ApiConnectionHandler* _handler;
            io::Reactor::Ptr _reactor;
        public:
            ApiConnection(ApiConnectionHandler* handler, io::Reactor::Ptr reactor, session& s) 
                : _handler(handler)
                , _reactor(reactor)
                , _api(*this)
            {
                _keyKeeper = std::make_shared<WasmKeyKeeperProxy>(s, _reactor);
            }

            void reactorRunOnce()
            {
                _reactor->run_once();
            }

            virtual ~ApiConnection()
            {
                if(_walletDB)
                    _walletDB->Unsubscribe(this);
            }

            //virtual void serializeMsg(const json& msg) = 0;

            void serializeMsg(const json& msg)
            {
                _handler->serializeMsg(msg);
                //serialize_json_msg(_body, _packer, msg);
            }

            template<typename T>
            void doResponse(const JsonRpcId& id, const T& response)
            {
                json msg;
                _api.getResponse(id, response, msg);
                serializeMsg(msg);
            }

            void doError(const JsonRpcId& id, ApiError code, const std::string& data = "")
            {
                json msg
                {
                    {"jsonrpc", "2.0"},
                    {"id", id},
                    {"error",
                        {
                            {"code", code},
                            {"message", WalletApi::getErrorMessage(code)}
                        }
                    }
                };

                if (!data.empty())
                {
                    msg["error"]["data"] = data;
                }

                serializeMsg(msg);
            }

            void onInvalidJsonRpc(const json& msg) override
            {
                LOG_DEBUG() << "onInvalidJsonRpc: " << msg;

                serializeMsg(msg);
            }

            void FillAddressData(const AddressData& data, WalletAddress& address)
            {
                if (data.comment)
                {
                    address.setLabel(*data.comment);
                }

                if (data.expiration)
                {
                    switch (*data.expiration)
                    {
                    case EditAddress::OneDay:
                        address.setExpiration(WalletAddress::ExpirationStatus::OneDay);
                        break;
                    case EditAddress::Expired:
                        address.setExpiration(WalletAddress::ExpirationStatus::Expired);
                        break;
                    case EditAddress::Never:
                        address.setExpiration(WalletAddress::ExpirationStatus::Never);
                        break;
                    }
                }
            }

            void onMessage(const JsonRpcId& id, const CreateAddress& data) override 
            {
                LOG_DEBUG() << "CreateAddress(id = " << id << ")";

                WalletAddress address;
                _walletDB->createAddress(address);
                FillAddressData(data, address);

                _walletDB->saveAddress(address);

                doResponse(id, CreateAddress::Response{ address.m_walletID });
            }

            void onMessage(const JsonRpcId& id, const DeleteAddress& data) override
            {
                LOG_DEBUG() << "DeleteAddress(id = " << id << " address = " << std::to_string(data.address) << ")";

                auto addr = _walletDB->getAddress(data.address);

                if (addr)
                {
                    _walletDB->deleteAddress(data.address);

                    doResponse(id, DeleteAddress::Response{});
                }
                else
                {
                    doError(id, ApiError::InvalidAddress, "Provided address doesn't exist.");
                }
            }

            void onMessage(const JsonRpcId& id, const EditAddress& data) override
            {
                LOG_DEBUG() << "EditAddress(id = " << id << " address = " << std::to_string(data.address) << ")";

                auto addr = _walletDB->getAddress(data.address);

                if (addr)
                {
                    if (addr->m_OwnID)
                    {
                        FillAddressData(data, *addr);
                        _walletDB->saveAddress(*addr);

                        doResponse(id, EditAddress::Response{});
                    }
                    else
                    {
                        doError(id, ApiError::InvalidAddress, "You can edit only own address.");
                    }
                }
                else
                {
                    doError(id, ApiError::InvalidAddress, "Provided address doesn't exist.");
                }
            }

            void onMessage(const JsonRpcId& id, const AddrList& data) override
            {
                LOG_DEBUG() << "AddrList(id = " << id << ")";

                doResponse(id, AddrList::Response{ _walletDB->getAddresses(data.own) });
            }

            void onMessage(const JsonRpcId& id, const ValidateAddress& data) override
            {
                LOG_DEBUG() << "ValidateAddress( address = " << std::to_string(data.address) << ")";

                auto addr = _walletDB->getAddress(data.address);
                bool isMine = addr ? addr->m_OwnID != 0 : false;
                doResponse(id, ValidateAddress::Response{ data.address.IsValid() && (isMine ? !addr->isExpired() : true), isMine});
            }

            void doTxAlreadyExistsError(const JsonRpcId& id)
            {
                doError(id, ApiError::InvalidTxId, "Provided transaction ID already exists in the wallet.");
            }

            void onMessage(const JsonRpcId& id, const Send& data) override
            {
                LOG_DEBUG() << "Send(id = " << id << " amount = " << data.value << " fee = " << data.fee <<  " address = " << std::to_string(data.address) << ")";

                try
                {
                    WalletID from(Zero);

                    if(data.from)
                    {
                        if(!data.from->IsValid())
                        {
                            doError(id, ApiError::InvalidAddress, "Invalid sender address.");
                            return;
                        }

                        auto addr = _walletDB->getAddress(*data.from);
                        bool isMine = addr ? addr->m_OwnID != 0 : false;

                        if(!isMine)
                        {
                            doError(id, ApiError::InvalidAddress, "It's not your own address.");
                            return;
                        }

                        if (addr->isExpired())
                        {
                            doError(id, ApiError::InvalidAddress, "Sender address is expired.");
                            return;
                        }

                        from = *data.from;
                    }
                    else
                    {
                        WalletAddress senderAddress;
                        _walletDB->createAddress(senderAddress);
                        _walletDB->saveAddress(senderAddress);

                        from = senderAddress.m_walletID;     
                    }

                    ByteBuffer message(data.comment.begin(), data.comment.end());

                    CoinIDList coins;

                    // if (data.session)
                    // {
                    //     coins = _walletDB->getLockedCoins(*data.session);

                    //     if (coins.empty())
                    //     {
                    //         doError(id, ApiError::InternalErrorJsonRpc, "Requested session is empty.");
                    //         return;
                    //     }
                    // }
                    // else
                    {
                        coins = data.coins ? *data.coins : CoinIDList();
                    }

                    auto minimumFee = std::max(wallet::GetMinimumFee(2), DefaultFee); // receivers's output + change
                    if (data.fee < minimumFee)
                    {
                        doError(id, ApiError::InternalErrorJsonRpc, getMinimumFeeError(minimumFee));
                        return;
                    }

                    if (data.txId && _walletDB->getTx(*data.txId))
                    {
                        doTxAlreadyExistsError(id);
                        return;
                    }

                    auto txId = _wallet->StartTransaction(CreateSimpleTransactionParameters(data.txId)
                        .SetParameter(TxParameterID::MyID, from)
                        .SetParameter(TxParameterID::PeerID, data.address)
                        .SetParameter(TxParameterID::Amount, data.value)
                        .SetParameter(TxParameterID::Fee, data.fee)
                        .SetParameter(TxParameterID::PreselectedCoins, coins)
                        .SetParameter(TxParameterID::Message, message));

                    doResponse(id, Send::Response{ txId });
                }
                catch(...)
                {
                    doError(id, ApiError::InternalErrorJsonRpc, "Transaction could not be created. Please look at logs.");
                }
            }

            void onMessage(const JsonRpcId& id, const Status& data) override
            {
                LOG_DEBUG() << "Status(txId = " << to_hex(data.txId.data(), data.txId.size()) << ")";

                auto tx = _walletDB->getTx(data.txId);

                if (tx)
                {
                    Block::SystemState::ID stateID = {};
                    _walletDB->getSystemStateID(stateID);

                    Status::Response result;
                    result.tx = *tx;
                    result.kernelProofHeight = 0;
                    result.systemHeight = stateID.m_Height;
                    result.confirmations = 0;

                    storage::getTxParameter(*_walletDB, tx->m_txId, TxParameterID::KernelProofHeight, result.kernelProofHeight);

                    doResponse(id, result);
                }
                else
                {
                    doError(id, ApiError::InvalidParamsJsonRpc, "Unknown transaction ID.");
                }
            }

            void onMessage(const JsonRpcId& id, const Split& data) override
            {
                LOG_DEBUG() << "Split(id = " << id << " coins = [";
                for (auto& coin : data.coins) LOG_DEBUG() << coin << ",";
                LOG_DEBUG() << "], fee = " << data.fee;
                try
                {
                    WalletAddress senderAddress;
                    _walletDB->createAddress(senderAddress);
                    _walletDB->saveAddress(senderAddress);

                    auto minimumFee = std::max(wallet::GetMinimumFee(data.coins.size() + 1), DefaultFee); // +1 extra output for change 
                    if (data.fee < minimumFee)
                    {
                        doError(id, ApiError::InternalErrorJsonRpc, getMinimumFeeError(minimumFee));
                        return;
                    }

                    if (data.txId && _walletDB->getTx(*data.txId))
                    {
                        doTxAlreadyExistsError(id);
                        return;
                    }

                    //auto txId = _wallet.StartTransaction(CreateSplitTransactionParameters(senderAddress.m_walletID, data.coins, data.txId)
                    //    .SetParameter(TxParameterID::Fee, data.fee));

                    //doResponse(id, Send::Response{ txId });
                    doError(id, ApiError::InternalErrorJsonRpc, "Temporary disabled!!!");
                }
                catch(...)
                {
                    doError(id, ApiError::InternalErrorJsonRpc, "Transaction could not be created. Please look at logs.");
                }
            }

            void onMessage(const JsonRpcId& id, const TxCancel& data) override
            {
                LOG_DEBUG() << "TxCancel(txId = " << to_hex(data.txId.data(), data.txId.size()) << ")";

                auto tx = _walletDB->getTx(data.txId);

                if (tx)
                {
                    if (tx->canCancel())
                    {                        
                        //_wallet.CancelTransaction(tx->m_txId);
                        TxCancel::Response result{ true };
                        doResponse(id, result);
                    }
                    else
                    {
                        doError(id, ApiError::InvalidTxStatus, "Transaction could not be cancelled. Invalid transaction status.");
                    }
                }
                else
                {
                    doError(id, ApiError::InvalidParamsJsonRpc, "Unknown transaction ID.");
                }
            }

            void onMessage(const JsonRpcId& id, const TxDelete& data) override
            {
                LOG_DEBUG() << "TxDelete(txId = " << to_hex(data.txId.data(), data.txId.size()) << ")";

                auto tx = _walletDB->getTx(data.txId);

                if (tx)
                {
                    if (tx->canDelete())
                    {
                        _walletDB->deleteTx(data.txId);

                        if (_walletDB->getTx(data.txId))
                        {
                            doError(id, ApiError::InternalErrorJsonRpc, "Transaction not deleted.");
                        }
                        else
                        {
                            doResponse(id, TxDelete::Response{true});
                        }
                    }
                    else
                    {
                        doError(id, ApiError::InternalErrorJsonRpc, "Transaction can't be deleted.");
                    }
                }
                else
                {
                    doError(id, ApiError::InvalidParamsJsonRpc, "Unknown transaction ID.");
                }
            }

            template<typename T>
            static void doPagination(size_t skip, size_t count, std::vector<T>& res)
            {
                if (count > 0)
                {
                    size_t start = skip;
                    size_t end = start + count;
                    size_t size = res.size();

                    if (start < size)
                    {
                        if (end > size) end = size;

                        res = std::vector<T>(res.begin() + start, res.begin() + end);
                    }
                    else res = {};
                }
            }

            void onMessage(const JsonRpcId& id, const GetUtxo& data) override 
            {
                LOG_DEBUG() << "GetUtxo(id = " << id << ")";

                GetUtxo::Response response;
                _walletDB->visitCoins([&response](const Coin& c)->bool
                {
                    response.utxos.push_back(c);
                    return true;
                });

                doPagination(data.skip, data.count, response.utxos);

                doResponse(id, response);
            }

            void onMessage(const JsonRpcId& id, const WalletStatus& data) override
            {
                LOG_DEBUG() << "WalletStatus(id = " << id << ")";

                WalletStatus::Response response;

                {
                    Block::SystemState::ID stateID = {};
                    _walletDB->getSystemStateID(stateID);

                    response.currentHeight = stateID.m_Height;
                    response.currentStateHash = stateID.m_Hash;
                }

                {
                    Block::SystemState::Full state;
                    _walletDB->get_History().get_Tip(state);
                    response.prevStateHash = state.m_Prev;
                    response.difficulty = state.m_PoW.m_Difficulty.ToFloat();
                }

                storage::Totals allTotals(*_walletDB);
                const auto& totals = allTotals.GetTotals(Zero);

                response.available = totals.Avail;
                response.receiving = totals.Incoming;
                response.sending = totals.Outgoing;
                response.maturing = totals.Maturing;

                doResponse(id, response);
            }

            void onMessage(const JsonRpcId& id, const GenerateTxId& data) override
            {
                LOG_DEBUG() << "GenerateTxId(id = " << id << ")";

                doResponse(id, GenerateTxId::Response{ wallet::GenerateTxID() });
            }

            static std::string generateUid()
            {
                std::array<uint8_t, 16> buf{};
                {
                    boost::uuids::uuid uid = boost::uuids::random_generator()();
                    std::copy(uid.begin(), uid.end(), buf.begin());
                }

                return to_hex(buf.data(), buf.size());
            }

            void onMessage(const JsonRpcId& id, const CreateWallet& data) override
            {
                LOG_DEBUG() << "CreateWallet(id = " << id << ")";

                beam::KeyString ks;

                ks.SetPassword(data.pass);
                ks.m_sRes = data.ownerKey;

                std::shared_ptr<ECC::HKdfPub> ownerKdf = std::make_shared<ECC::HKdfPub>();

                if(ks.Import(*ownerKdf))
                {
                    auto dbName = generateUid();
                    IWalletDB::Ptr walletDB = WalletDB::init(dbName + ".db", SecString(data.pass), _keyKeeper);

                    if(walletDB)
                    {
                        walletDB->Subscribe(this);

                        // generate default address
                        WalletAddress address;
                        walletDB->createAddress(address);
                        address.m_label = "default";
                        walletDB->saveAddress(address);

                        doResponse(id, CreateWallet::Response{dbName});
                        return;
                    }
                }

                doError(id, ApiError::InternalErrorJsonRpc, "Wallet not created.");
            }

            void onMessage(const JsonRpcId& id, const OpenWallet& data) override
            {
                LOG_DEBUG() << "OpenWallet(id = " << id << ")";

                _walletDB = WalletsMap.count(data.id)
                    ? WalletsMap[data.id]
                    : WalletDB::open(data.id + ".db", SecString(data.pass));

                if(!_walletDB)
                {
                    doError(id, ApiError::InternalErrorJsonRpc, "Wallet not opened.");
                    return;
                }

                if(!WalletsMap.count(data.id))
                    WalletsMap[data.id] = _walletDB;

                LOG_INFO() << "wallet sucessfully opened...";

                _walletDB->Subscribe(this);

                _wallet = std::make_unique<Wallet>( _walletDB );
                _wallet->ResumeAllTransactions();

                auto nnet = std::make_shared<proto::FlyClient::NetworkStd>(*_wallet);
                nnet->m_Cfg.m_PollPeriod_ms = 0;//options.pollPeriod_ms.value;
            
                if (nnet->m_Cfg.m_PollPeriod_ms)
                {
                    LOG_INFO() << "Node poll period = " << nnet->m_Cfg.m_PollPeriod_ms << " ms";
                    uint32_t timeout_ms = std::max(Rules::get().DA.Target_s * 1000, nnet->m_Cfg.m_PollPeriod_ms);
                    if (timeout_ms != nnet->m_Cfg.m_PollPeriod_ms)
                    {
                        LOG_INFO() << "Node poll period has been automatically rounded up to block rate: " << timeout_ms << " ms";
                    }
                }
                uint32_t responceTime_s = Rules::get().DA.Target_s * wallet::kDefaultTxResponseTime;
                if (nnet->m_Cfg.m_PollPeriod_ms >= responceTime_s * 1000)
                {
                    LOG_WARNING() << "The \"--node_poll_period\" parameter set to more than " << uint32_t(responceTime_s / 3600) << " hours may cause transaction problems.";
                }
                nnet->m_Cfg.m_vNodes.push_back(node_addr);
                nnet->Connect();

                auto wnet = std::make_shared<WalletNetworkViaBbs>(*_wallet, nnet, _walletDB);
                _wallet->AddMessageEndpoint(wnet);
                _wallet->SetNodeEndpoint(nnet);

                // !TODO: not sure, do we need this id in the future
                auto session = generateUid();

                doResponse(id, OpenWallet::Response{session});
            }

            // void onMessage(const JsonRpcId& id, const Lock& data) override
            // {
            //     LOG_DEBUG() << "Lock(id = " << id << ")";

            //     Lock::Response response;

            //     response.result = _walletDB->lockCoins(data.coins, data.session);

            //     doResponse(id, response);
            // }

            // void onMessage(const JsonRpcId& id, const Unlock& data) override
            // {
            //     LOG_DEBUG() << "Unlock(id = " << id << " session = " << data.session << ")";

            //     Unlock::Response response;

            //     response.result = _walletDB->unlockCoins(data.session);

            //     doResponse(id, response);
            // }

            void onMessage(const JsonRpcId& id, const TxList& data) override
            {
                LOG_DEBUG() << "List(filter.status = " << (data.filter.status ? std::to_string((uint32_t)*data.filter.status) : "nul") << ")";

                TxList::Response res;

                {
                    auto txList = _walletDB->getTxHistory();

                    Block::SystemState::ID stateID = {};
                    _walletDB->getSystemStateID(stateID);

                    for (const auto& tx : txList)
                    {
                        Status::Response item;
                        item.tx = tx;
                        item.kernelProofHeight = 0;
                        item.systemHeight = stateID.m_Height;
                        item.confirmations = 0;

                        storage::getTxParameter(*_walletDB, tx.m_txId, TxParameterID::KernelProofHeight, item.kernelProofHeight);
                        res.resultList.push_back(item);
                    }
                }

                using Result = decltype(res.resultList);

                // filter transactions by status if provided
                if (data.filter.status)
                {
                    Result filteredList;

                    for (const auto& it : res.resultList)
                        if (it.tx.m_status == *data.filter.status)
                            filteredList.push_back(it);

                    res.resultList = filteredList;
                }

                // filter transactions by height if provided
                if (data.filter.height)
                {
                    Result filteredList;

                    for (const auto& it : res.resultList)
                        if (it.kernelProofHeight == *data.filter.height)
                            filteredList.push_back(it);

                    res.resultList = filteredList;
                }

                doPagination(data.skip, data.count, res.resultList);

                doResponse(id, res);
            }

        protected:
            IWalletDB::Ptr _walletDB;
            std::unique_ptr<Wallet> _wallet;
            WalletApi _api;
            IPrivateKeyKeeper2::Ptr _keyKeeper;
        };

        class session : 
            public std::enable_shared_from_this<session>, 
            public ApiConnection, 
            public ApiConnectionHandler
        {
            websocket::stream<tcp::socket> ws_;
            boost::beast::multi_buffer buffer_;
            io::Timer::Ptr _readTimer;
            bool systemIsBusy = false;

        public:
            // Take ownership of the socket
            explicit
            session(tcp::socket socket, io::Reactor::Ptr reactor)
                : ApiConnection(this, reactor, *this)
                , ws_(std::move(socket))
                , _readTimer(io::Timer::create(*reactor))
            {
                
            }

            ~session()
            {
                LOG_DEBUG() << "session destroyed.";
            }

            // Start the asynchronous operation
            void
            run()
            {
                // Accept the websocket handshake
                ws_.async_accept(
                    boost::asio::bind_executor(
                        ws_.get_executor(),
                        std::bind(
                            &session::on_accept,
                            shared_from_this(),
                            std::placeholders::_1)));
            }

            void
            on_accept(boost::system::error_code ec)
            {
                if(ec)
                    return fail(ec, "accept");

                // Read a message
                // do_async_read();
                do_read();
            }

            void do_async_read()
            {
                //do_read();
                _readTimer->start(1, false, [this](){do_read();});
            }

            void
            do_read()
            {
                // Read a message into our buffer
                ws_.async_read(
                    buffer_,
                    boost::asio::bind_executor(
                        ws_.get_executor(),
                        std::bind(
                            &session::on_read,
                            shared_from_this(),
                            std::placeholders::_1,
                            std::placeholders::_2)));

                // while(!done)
                //     reactorRunOnce();
            }

            using KeyKeeperFunc = std::function<void(const json&)>;

            boost::optional<KeyKeeperFunc> _awaitingResponse;
            std::queue<KeyKeeperFunc> _keeperCallbacks;

            // void
            // do_keykeeper_read(KeyKeeperFunc func)
            // {
            //     // Read a message into our buffer
            //     ws_.async_read(
            //         buffer_,
            //         boost::asio::bind_executor(
            //             strand_,
            //             std::bind(
            //                 &session::on_keykeeper_read,
            //                 shared_from_this(),
            //                 std::placeholders::_1,
            //                 std::placeholders::_2, func)));
            // }

            void serializeMsg(const json& msg) override
            {
                //buffer_.consume(buffer_.size());
                std::string contents = msg.dump();
                //size_t n = boost::asio::buffer_copy(buffer_.prepare(contents.size()), boost::asio::buffer(contents));
                //buffer_.commit(n);

                bool done = false;

                ws_.text(ws_.got_text());
                ws_.async_write(
                    boost::asio::buffer(contents),//buffer_.data(),
                    boost::asio::bind_executor(
                        ws_.get_executor(),
                        std::bind(
                            &session::on_write,
                            shared_from_this(),
                            std::placeholders::_1,
                            std::placeholders::_2, [&done](){done=true;})));

                while(!done)
                    reactorRunOnce();
            }

            void call_keykeeper_method_async(const json& msg, KeyKeeperFunc func)
            {
                std::string contents = msg.dump();

                _keeperCallbacks.push(func);

                ws_.text(ws_.got_text());
                ws_.async_write(
                    boost::asio::buffer(contents),
                    boost::asio::bind_executor(
                        ws_.get_executor(),
                        std::bind(
                            &session::on_keykeeper_write,
                            shared_from_this(),
                            std::placeholders::_1,
                            std::placeholders::_2, []() {})));

            }

            void call_keykeeper_method(const json& msg, KeyKeeperFunc func)
            {
                systemIsBusy = true;

                //buffer_.consume(buffer_.size());
                std::string contents = msg.dump();
                // size_t n = boost::asio::buffer_copy(buffer_.prepare(contents.size()), boost::asio::buffer(contents));
                // buffer_.commit(n);

                bool done = false;

                _awaitingResponse = [&done, &func](const json& msg)
                {
                    done = true;
                    func(msg);
                };

                ws_.text(ws_.got_text());
                ws_.async_write(
                    boost::asio::buffer(contents),
                    boost::asio::bind_executor(
                        ws_.get_executor(),
                        std::bind(
                            &session::on_keykeeper_write,
                            shared_from_this(),
                            std::placeholders::_1,
                            std::placeholders::_2, [](){})));

                while(!done)
                    reactorRunOnce();

                systemIsBusy = false;
            }

            void
            on_read(
                boost::system::error_code ec,
                std::size_t bytes_transferred)
            {
                boost::ignore_unused(bytes_transferred);

                // This indicates that the session was closed
                if(ec == websocket::error::closed)
                    return;

                if(ec)
                    fail(ec, "read");

                {
                    std::ostringstream os;

# if (BOOST_VERSION/100 % 1000) >= 70
                    os << boost::beast::make_printable(buffer_.data());
# else
                    os << boost::beast::buffers(buffer_.data());
# endif

                    buffer_.consume(buffer_.size());
                    auto data = os.str();

                    if(data.size())
                    {
                        LOG_DEBUG() << "data from a client:" << data;
                        
                        do_async_read();

                        try
                        {
                            json msg = json::parse(data.c_str(), data.c_str() + data.size());

                            if(existsJsonParam(msg, "result"))
                            {
                                if (_keeperCallbacks.empty())
                                    return;

                                _keeperCallbacks.front()(msg);
                                _keeperCallbacks.pop();
                            }
                            else if(!systemIsBusy)
                            {
                                // !TODO: don't forget to cache this request
                                _api.parse(data.c_str(), data.size());
                            }
                        }
                        catch (const nlohmann::detail::exception& e)
                        {
                            LOG_ERROR() << "json parse: " << e.what() << "\n";

                        }
                    }
                }

                
            }

            // void
            // on_keykeeper_read(
            //     boost::system::error_code ec,
            //     std::size_t bytes_transferred, KeyKeeperFunc func)
            // {
            //     boost::ignore_unused(bytes_transferred);

            //     // This indicates that the session was closed
            //     if(ec == websocket::error::closed)
            //         return;

            //     if(ec)
            //         fail(ec, "read");

            //     {
            //         std::ostringstream os;
            //         os << boost::beast::buffers(buffer_.data());
            //         auto data = os.str();
            //         LOG_DEBUG() << "data from a keykeeper:" << data;

            //         json msg = json::parse(data.c_str(), data.c_str() + data.size());

            //         func(msg);
            //     }
            // }

            void
            on_write(
                boost::system::error_code ec,
                std::size_t bytes_transferred, std::function<void()> done)
            {
                boost::ignore_unused(bytes_transferred);

                if(ec)
                    return fail(ec, "write");

                // Clear the buffer
                //buffer_.consume(buffer_.size());

                done();

                // Do another read
                //do_read();
            }

            void
            on_keykeeper_write(
                boost::system::error_code ec,
                std::size_t bytes_transferred, std::function<void()> done)
            {
                boost::ignore_unused(bytes_transferred);

                if(ec)
                    return fail(ec, "write");

                // Clear the buffer
                //buffer_.consume(buffer_.size());

                done();

                // Do another read
                //do_keykeeper_read(func);
            }
        };

        // Accepts incoming connections and launches the sessions
        class listener : public std::enable_shared_from_this<listener>
        {
            tcp::acceptor acceptor_;
            tcp::socket socket_;
            io::Reactor::Ptr _reactor;

        public:
            listener(boost::asio::io_context& ioc,
                tcp::endpoint endpoint, io::Reactor::Ptr reactor)
                : acceptor_(ioc)
                , socket_(ioc)
                , _reactor(reactor)
            {
                boost::system::error_code ec;

                // Open the acceptor
                acceptor_.open(endpoint.protocol(), ec);
                if(ec)
                {
                    fail(ec, "open");
                    return;
                }

                // Allow address reuse
                acceptor_.set_option(boost::asio::socket_base::reuse_address(true), ec);
                if(ec)
                {
                    fail(ec, "set_option");
                    return;
                }

                // Bind to the server address
                acceptor_.bind(endpoint, ec);
                if(ec)
                {
                    fail(ec, "bind");
                    return;
                }

                // Start listening for connections
                acceptor_.listen(
                    boost::asio::socket_base::max_listen_connections, ec);
                if(ec)
                {
                    fail(ec, "listen");
                    return;
                }
            }

            // Start accepting incoming connections
            void
            run()
            {
                if(! acceptor_.is_open())
                    return;
                do_accept();
            }

            void
            do_accept()
            {
                acceptor_.async_accept(
                    socket_,
                    std::bind(
                        &listener::on_accept,
                        shared_from_this(),
                        std::placeholders::_1));
            }

            void
            on_accept(boost::system::error_code ec)
            {
                if(ec)
                {
                    fail(ec, "accept");
                }
                else
                {
                    // Create the session and run it
                    auto s = std::make_shared<session>(std::move(socket_), _reactor);
                    _sessions.push_back(s);
                    s->run();
                }

                // Accept another connection
                do_accept();
            }

            std::vector<std::shared_ptr<session>> _sessions;
        };

        io::Reactor::Ptr _reactor;
    };
}

int main(int argc, char* argv[])
{
    using namespace beam;
    namespace po = boost::program_options;

    const auto path = boost::filesystem::system_complete("./logs");
    auto logger = beam::Logger::create(LOG_LEVEL_DEBUG, LOG_LEVEL_DEBUG, LOG_LEVEL_DEBUG, "api_", path.string());

    try
    {
        struct
        {
            uint16_t port;
            std::string nodeURI;
            Nonnegative<uint32_t> pollPeriod_ms;
            uint32_t logCleanupPeriod;

        } options;

        {
            po::options_description desc("Wallet API general options");
            desc.add_options()
                (cli::HELP_FULL, "list of all options")
                (cli::PORT_FULL, po::value(&options.port)->default_value(8080), "port to start server on")
                (cli::NODE_ADDR_FULL, po::value<std::string>(&options.nodeURI), "address of node")
                (cli::LOG_CLEANUP_DAYS, po::value<uint32_t>(&options.logCleanupPeriod)->default_value(5), "old logfiles cleanup period(days)")
                (cli::NODE_POLL_PERIOD, po::value<Nonnegative<uint32_t>>(&options.pollPeriod_ms)->default_value(Nonnegative<uint32_t>(0)), "Node poll period in milliseconds. Set to 0 to keep connection. Anyway poll period would be no less than the expected rate of blocks if it is less then it will be rounded up to block rate value.")
            ;

            desc.add(createRulesOptionsDescription());

            po::variables_map vm;

            po::store(po::command_line_parser(argc, argv)
                .options(desc)
                .style(po::command_line_style::default_style ^ po::command_line_style::allow_guessing)
                .run(), vm);

            if (vm.count(cli::HELP))
            {
                std::cout << desc << std::endl;
                return 0;
            }

            {
                std::ifstream cfg("wallet-api.cfg");

                if (cfg)
                {                    
                    po::store(po::parse_config_file(cfg, desc), vm);
                }
            }

            vm.notify();

            getRulesOptions(vm);

            Rules::get().UpdateChecksum();
            LOG_INFO() << "Beam Wallet API " << PROJECT_VERSION << " (" << BRANCH_NAME << ")";
            LOG_INFO() << "Rules signature: " << Rules::get().get_SignatureStr();
            
            if (vm.count(cli::NODE_ADDR) == 0)
            {
                LOG_ERROR() << "node address should be specified";
                return -1;
            }

            if (!node_addr.resolve(options.nodeURI.c_str()))
            {
                LOG_ERROR() << "unable to resolve node address: " << options.nodeURI;
                return -1;
            }
        }

        io::Reactor::Ptr reactor = io::Reactor::create();
        io::Reactor::Scope scope(*reactor);
        io::Reactor::GracefulIntHandler gih(*reactor);

        LogRotation logRotation(*reactor, LOG_ROTATION_PERIOD, 5);//options.logCleanupPeriod);

        WalletApiServer server(reactor, options.port);

        reactor->run();

        LOG_INFO() << "Done";
    }
    catch (const std::exception& e)
    {
        LOG_ERROR() << "EXCEPTION: " << e.what();
    }
    catch (...)
    {
        LOG_ERROR() << "NON_STD EXCEPTION";
    }

    return 0;
}
