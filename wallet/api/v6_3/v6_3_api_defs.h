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

#include <vector>
#include <memory>
#include <string>
#include "core/ecc_native.h"
#include "utility/common.h"

#include "wallet/api/v6_1/v6_1_api_defs.h"

namespace beam::wallet
{
    #define ETH_API_METHODS(macro) \
        macro(ChainID,                  "eth_chainId",                  API_READ_ACCESS,    API_SYNC,  APPS_BLOCKED) \
        macro(NetVersion,               "net_version",                  API_READ_ACCESS,    API_SYNC,  APPS_BLOCKED) \
        macro(BlockNumber,              "eth_blockNumber",              API_READ_ACCESS,    API_SYNC,  APPS_BLOCKED) \
        macro(Balance,                  "eth_getBalance",               API_READ_ACCESS,    API_SYNC,  APPS_BLOCKED) \
        macro(BlockByNumber,            "eth_getBlockByNumber",         API_READ_ACCESS,    API_SYNC,  APPS_BLOCKED) \
        macro(GasPrice,                 "eth_gasPrice",                 API_READ_ACCESS,    API_SYNC,  APPS_BLOCKED) \
        macro(EstimateGas,              "eth_estimateGas",              API_READ_ACCESS,    API_SYNC,  APPS_BLOCKED) \
        macro(GetCode,                  "eth_getCode",                  API_READ_ACCESS,    API_SYNC,  APPS_BLOCKED) \
        macro(GetTransactionCount,      "eth_getTransactionCount",      API_READ_ACCESS,    API_SYNC,  APPS_BLOCKED) \
        macro(SendRawTransaction,       "eth_sendRawTransaction",       API_WRITE_ACCESS,   API_ASYNC, APPS_BLOCKED) \
        macro(GetTransactionReceipt,    "eth_getTransactionReceipt",    API_READ_ACCESS,    API_SYNC,  APPS_BLOCKED) \
        macro(GetBlockByHash,           "eth_getBlockByHash",           API_READ_ACCESS,    API_SYNC,  APPS_BLOCKED) \
        macro(Call,                     "eth_call",                     API_WRITE_ACCESS,   API_ASYNC, APPS_BLOCKED) \



    #define V6_3_API_METHODS(macro) \
        macro(IPFSAdd,          "ipfs_add",             API_WRITE_ACCESS, API_ASYNC, APPS_ALLOWED) \
        macro(IPFSHash,         "ipfs_hash",            API_READ_ACCESS,  API_ASYNC, APPS_ALLOWED) \
        macro(IPFSGet,          "ipfs_get",             API_WRITE_ACCESS, API_ASYNC, APPS_ALLOWED) \
        macro(IPFSPin,          "ipfs_pin",             API_WRITE_ACCESS, API_ASYNC, APPS_ALLOWED) \
        macro(IPFSUnpin,        "ipfs_unpin",           API_WRITE_ACCESS, API_ASYNC, APPS_ALLOWED) \
        macro(IPFSGc,           "ipfs_gc",              API_WRITE_ACCESS, API_ASYNC, APPS_ALLOWED) \
        macro(SignMessage,      "sign_message",         API_READ_ACCESS,  API_SYNC,  APPS_ALLOWED) \
        macro(VerifySignature,  "verify_signature",     API_READ_ACCESS,  API_SYNC,  APPS_ALLOWED) \
        ETH_API_METHODS(macro)
        // TODO:IPFS add ipfs_caps/ev_ipfs_state methods that returns all available capabilities and ipfs state


    struct ChainID
    {
        struct Response
        {
        };
    };

    struct NetVersion
    {
        struct Response {};
    };

    struct BlockNumber
    {
        struct Response
        {
            uint64_t height;
        };
    };

    struct Balance
    {
        std::string address;
        std::string tag;
        std::string block;

        struct Response
        {
            uint64_t balanceHi = 0;
            uint64_t balanceLo = 0;
        };
    };

    struct BlockByNumber
    {
        std::string tag;
        bool fullTxInfo = false;

        struct Response
        {
            uint64_t number;
        };
    };

    struct GasPrice
    {
        struct Response {};
    };

    struct EstimateGas
    {
        struct Response {};
    };

    struct GetCode
    {
        struct Response {};
    };

    struct GetTransactionCount
    {
        struct Response
        {

        };
    };

    struct SendRawTransaction
    {
        InvokeContractV61 subCall;
        struct Response
        {
            std::string txHash;
        };
    };

    struct GetTransactionReceipt
    {
        struct Response
        {

        };
    };
    

    struct GetBlockByHash
    {

        struct Response
        {
            uint64_t number;
        };
    };

    struct Call
    {
        InvokeContractV61 subCall;
        struct Response
        {
            InvokeContractV61::Response response;
        };
    };
    

    ////////////////////

    struct IPFSAdd
    {
        std::vector<uint8_t> data;
        bool pin = true;
        uint32_t timeout = 0;

        struct Response
        {
            std::string hash;
            bool pinned = false;
        };
    };

    struct IPFSHash
    {
        std::vector<uint8_t> data;
        uint32_t timeout = 0;
        struct Response
        {
            std::string hash;
        };
    };

    struct IPFSGet
    {
        std::string hash;
        uint32_t timeout = 0;

        struct Response
        {
            std::string hash;
            std::vector<uint8_t> data;
        };
    };

    struct IPFSPin
    {
        std::string hash;
        uint32_t timeout = 0;

        struct Response
        {
            std::string hash;
        };
    };

    struct IPFSUnpin
    {
        std::string hash;
        struct Response
        {
            std::string hash;
        };
    };

    struct IPFSGc
    {
        uint32_t timeout = 0;
        struct Response
        {
        };
    };

    struct SignMessage
    {
        std::vector<uint8_t> keyMaterial;
        std::string message;
        struct Response
        {
            std::string signature;
        };
    };

    struct VerifySignature
    {
        ECC::Point::Native publicKey;
        std::string message;
        std::vector<uint8_t> signature;
        struct Response
        {
            bool result;
        };
    };
}
