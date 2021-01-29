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

#include <nlohmann/json.hpp>
#include <stdexcept>

namespace beam::wallet
{
    using json = nlohmann::json;
    using JsonRpcId = json;

    #define JSON_RPC_ERRORS(macro) \
    macro(-32600, InvalidJsonRpc,            "Invalid JSON-RPC.")                \
    macro(-32601, NotFoundJsonRpc,           "Procedure not found.")             \
    macro(-32602, InvalidParamsJsonRpc,      "Invalid parameters.")              \
    macro(-32603, InternalErrorJsonRpc,      "Internal JSON-RPC error.")         \
    macro(-32001, InvalidTxStatus,           "Invalid TX status.")               \
    macro(-32002, UnknownApiKey,             "Unknown API key.")                 \
    macro(-32003, InvalidAddress,            "Invalid address.")                 \
    macro(-32004, InvalidTxId,               "Invalid transaction ID.")          \
    macro(-32005, NotSupported,              "Feature is not supported")         \
    macro(-32006, InvalidPaymentProof,       "Invalid payment proof provided")   \
    macro(-32007, PaymentProofExportError,   "Cannot export payment proof")      \
    macro(-32008, SwapFailToParseToken,      "Invalid swap token.")              \
    macro(-32009, SwapFailToAcceptOwnOffer,  "Can't accept own swap offer.")     \
    macro(-32010, SwapNotEnoughtBeams,       "Not enought beams.")               \
    macro(-32011, SwapFailToConnect,         "Doesn't have active connection.")  \
    macro(-32012, DatabaseError,             "Database error")                   \
    macro(-32013, DatabaseNotFound,          "Database not found")               \
    macro(-32014, ThrottleError,             "Requests limit exceeded")          \
    macro(-32015, NotOpenedError,            "Wallet not opened")

    enum ApiError
    {
        #define ERROR_ITEM(code, item, _) item = code,
        JSON_RPC_ERRORS(ERROR_ITEM)
        #undef ERROR_ITEM
    };

    class FailToParseToken: public std::runtime_error
    {
    public:
        FailToParseToken(): std::runtime_error("Parse Parameters from 'token' failed.") {}
    };

    class FailToAcceptOwnOffer: public std::runtime_error
    {
    public:
        FailToAcceptOwnOffer(): std::runtime_error("You can't accept own offer.") {}
    };

    class NotEnoughtBeams: public std::runtime_error
    {
    public:
        NotEnoughtBeams(): std::runtime_error("Not enought beams") {}
    };

    class FailToConnectSwap: public std::runtime_error
    {
    public:
        explicit FailToConnectSwap(const std::string& coin)
            :std::runtime_error(std::string("There is not connection with ") + coin + " wallet")
        {
        }
    };

    class jsonrpc_exception: public std::runtime_error
    {
    public:
        jsonrpc_exception(const ApiError ecode, JsonRpcId reqid, const std::string &msg)
            : runtime_error(msg),
            _ecode(ecode),
            _rpcid(std::move(reqid))
        {
        }

        [[nodiscard]] ApiError code() const
        {
            return _ecode;
        }

        [[nodiscard]] const JsonRpcId &rpcid() const
        {
            return _rpcid;
        }

        [[nodiscard]] std::string whatstr() const
        {
            return std::string(what());
        }

        [[nodiscard]] bool has_what() const
        {
            return what() && strlen(what());
        }

    private:
        ApiError _ecode;
        JsonRpcId _rpcid;
    };
}
