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

#define INVALID_JSON_RPC -32600
#define NOTFOUND_JSON_RPC -32601
#define INVALID_PARAMS_JSON_RPC -32602
#define INVALID_TX_STATUS -32001

namespace beam
{
    using json = nlohmann::json;

#define WALLET_API_METHODS(macro) \
    macro(CreateAddress,    "create_address") \
    macro(ValidateAddress,  "validate_address") \
    macro(Send,             "tx_send") \
    macro(Replace,          "replace") \
    macro(Status,           "tx_status") \
    macro(Split,            "tx_split") \
    macro(TxCancel,         "tx_cancel") \
    macro(GetUtxo,          "get_utxo") \
    macro(Lock,             "lock") \
    macro(Unlock,           "unlock") \
    macro(TxList,           "tx_list") \
    macro(WalletStatus,     "wallet_status")

    struct CreateAddress
    {
        std::string metadata;
        int lifetime;

        struct Response
        {
            WalletID address;
        };
    };

    struct ValidateAddress
    {
        WalletID address;

        struct Response
        {
            bool isValid;
        };
    };

    struct Send
    {
        //int session;
        Amount value;
        Amount fee;
        WalletID address;
        std::string comment;

        struct Response
        {
            TxID txId;
        };
    };

    struct Replace
    {
        struct Response
        {

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
        //int session;
        Amount fee;
        AmountList coins;

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

    struct GetUtxo
    {
        struct Response
        {
            std::vector<beam::Coin> utxos;
        };
    };

    struct Lock
    {
        struct Response
        {

        };
    };

    struct Unlock
    {
        struct Response
        {

        };
    };

    struct TxList
    {
        struct
        {
            boost::optional<TxStatus> status;
            boost::optional<Height> height;
        } filter;

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
            std::string currentStateHash;
            Amount available = 0;
            Amount receiving = 0;
            Amount sending = 0;
            Amount maturing = 0;
            Amount locked = 0;
        };
    };

    class IWalletApiHandler
    {
    public:
        virtual void onInvalidJsonRpc(const json& msg) = 0;

#define MESSAGE_FUNC(api, name) \
        virtual void onMessage(int id, const api& data) = 0;

        WALLET_API_METHODS(MESSAGE_FUNC)

#undef MESSAGE_FUNC
    };

    class WalletApi
    {
    public:
        WalletApi(IWalletApiHandler& handler);

#define RESPONSE_FUNC(api, name) \
        void getResponse(int id, const api::Response& data, json& msg);

        WALLET_API_METHODS(RESPONSE_FUNC)

#undef RESPONSE_FUNC

        bool parse(const char* data, size_t size);

    private:

#define MESSAGE_FUNC(api, name) \
        void on##api##Message(int id, const json& msg);

        WALLET_API_METHODS(MESSAGE_FUNC)

#undef MESSAGE_FUNC

    private:
        IWalletApiHandler& _handler;
        std::map<std::string, std::function<void(int id, const json& msg)>> _methods;
    };
}
