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

#include <boost/optional.hpp>

#include "wallet/wallet.h"
#include "nlohmann/json.hpp"

namespace beam::wallet
{
    constexpr Amount DefaultFee = 100;

    using json = nlohmann::json;
    using JsonRpcId = json;

#define JSON_RPC_ERRORS(macro) \
    macro(-32600, InvalidJsonRpc,       "Invalid JSON-RPC.")        \
    macro(-32601, NotFoundJsonRpc,      "Procedure not found.")     \
    macro(-32602, InvalidParamsJsonRpc, "Invalid parameters.")      \
    macro(-32603, InternalErrorJsonRpc, "Internal JSON-RPC error.") \
    macro(-32001, InvalidTxStatus,      "Invalid TX status.")       \
    macro(-32002, UnknownApiKey,        "Unknown API key.")         \
    macro(-32003, InvalidAddress,       "Invalid address.")         \
    macro(-32004, InvalidTxId,          "Invalid transaction ID.")

    enum ApiError
    {
#define ERROR_ITEM(code, item, _) item = code,
        JSON_RPC_ERRORS(ERROR_ITEM)
#undef ERROR_ITEM
    };

#define API_WRITE_ACCESS true
#define API_READ_ACCESS false

#define WALLET_API_METHODS(macro) \
    macro(CreateAddress,    "create_address",   API_WRITE_ACCESS)   \
    macro(DeleteAddress,    "delete_address",   API_WRITE_ACCESS)   \
    macro(EditAddress,      "edit_address",     API_WRITE_ACCESS)   \
    macro(AddrList,         "addr_list",        API_READ_ACCESS)    \
    macro(ValidateAddress,  "validate_address", API_READ_ACCESS)    \
    macro(Send,             "tx_send",          API_WRITE_ACCESS)   \
    macro(Status,           "tx_status",        API_READ_ACCESS)    \
    macro(Split,            "tx_split",         API_WRITE_ACCESS)   \
    macro(TxCancel,         "tx_cancel",        API_WRITE_ACCESS)   \
    macro(TxDelete,         "tx_delete",        API_WRITE_ACCESS)   \
    macro(GetUtxo,          "get_utxo",         API_READ_ACCESS)    \
    macro(Lock,             "lock",             API_WRITE_ACCESS)   \
    macro(Unlock,           "unlock",           API_WRITE_ACCESS)   \
    macro(TxList,           "tx_list",          API_READ_ACCESS)    \
    macro(WalletStatus,     "wallet_status",    API_READ_ACCESS)    \
    macro(GenerateTxId,     "generate_tx_id",   API_READ_ACCESS)

    struct AddressData
    {
        boost::optional<std::string> comment;

        enum Expiration { Expired, Never, OneDay };
        boost::optional<Expiration> expiration;
    };

    struct CreateAddress : AddressData
    {
        struct Response
        {
            WalletID address;
        };
    };

    struct DeleteAddress
    {
        WalletID address;

        struct Response {};
    };

    struct EditAddress : AddressData
    {
        WalletID address;

        struct Response {};
    };

    struct AddrList
    {
        bool own;

        struct Response
        {
            std::vector<WalletAddress> list;
        };
    };

    struct ValidateAddress
    {
        WalletID address = Zero;

        struct Response
        {
            bool isValid;
            bool isMine;
        };
    };

    struct Send
    {
        Amount value;
        Amount fee = DefaultFee;
        boost::optional<CoinIDList> coins;
        boost::optional<WalletID> from;
        boost::optional<uint64_t> session;
        boost::optional<TxID> txId;
        WalletID address;
        std::string comment;

        struct Response
        {
            TxID txId;
        };
    };

    struct Status
    {
        TxID txId;

        struct Response
        {
            TxDescription tx;
            Height kernelProofHeight;
            Height systemHeight;
            uint64_t confirmations;
        };
    };

    struct Split
    {
        Amount fee = DefaultFee;
        AmountList coins;
        boost::optional<TxID> txId;

        struct Response
        {
            TxID txId;
        };
    };

    struct TxCancel
    {
        TxID txId;

        struct Response
        {
            bool result;
        };
    };


    struct TxDelete
    {
        TxID txId;

        struct Response
        {
            bool result;
        };
    };

    struct GetUtxo
    {
        int count = 0;
        int skip = 0;

        struct Response
        {
            std::vector<Coin> utxos;
        };
    };

    struct Lock
    {
        CoinIDList coins;
        uint64_t session;

        struct Response
        {
            bool result;
        };
    };

    struct Unlock
    {
        uint64_t session;

        struct Response
        {
            bool result;
        };
    };

    struct TxList
    {
        struct
        {
            boost::optional<TxStatus> status;
            boost::optional<Height> height;
        } filter;

        int count = 0;
        int skip = 0;

        struct Response
        {
            std::vector<Status::Response> resultList;
        };
    };

    struct WalletStatus
    {
        struct Response
        {
            beam::Height currentHeight = 0;
            Merkle::Hash currentStateHash;
            Merkle::Hash prevStateHash;
            Amount available = 0;
            Amount receiving = 0;
            Amount sending = 0;
            Amount maturing = 0;
            double difficulty = 0;
        };
    };

    struct GenerateTxId
    {
        struct Response
        {
            TxID txId;
        };
    };

    class IWalletApiHandler
    {
    public:
        virtual void onInvalidJsonRpc(const json& msg) = 0;

#define MESSAGE_FUNC(api, name, _) \
        virtual void onMessage(const JsonRpcId& id, const api& data) = 0;

        WALLET_API_METHODS(MESSAGE_FUNC)

#undef MESSAGE_FUNC
    };

    class WalletApi
    {
    public:

        // user api key and read/write access
        using ACL = boost::optional<std::map<std::string, bool>>;

        WalletApi(IWalletApiHandler& handler, ACL acl = boost::none);

#define RESPONSE_FUNC(api, name, _) \
        void getResponse(const JsonRpcId& id, const api::Response& data, json& msg);

        WALLET_API_METHODS(RESPONSE_FUNC)

#undef RESPONSE_FUNC

        bool parse(const char* data, size_t size);

        static const char* getErrorMessage(ApiError code);

    private:

#define MESSAGE_FUNC(api, name, _) \
        void on##api##Message(const JsonRpcId& id, const json& msg);

        WALLET_API_METHODS(MESSAGE_FUNC)

#undef MESSAGE_FUNC

    private:
        IWalletApiHandler& _handler;

        struct FuncInfo
        {
            std::function<void(const JsonRpcId& id, const json& msg)> func;
            bool writeAccess;
        };

        std::map<std::string, FuncInfo> _methods;
        ACL _acl;
    };
}
