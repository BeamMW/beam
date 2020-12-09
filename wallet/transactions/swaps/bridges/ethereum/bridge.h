// Copyright 2020 The Beam Team
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

#include "utility/common.h"
#include "core/ecc.h"
#include <functional>
#include <string>
#include <vector>
#include "nlohmann/json.hpp"

namespace beam::ethereum
{
class IBridge
{
public:
    typedef std::array<uint8_t, 20> short_hash;
    typedef std::vector<uint8_t> data_chunk;
    typedef std::array<uint8_t, 32> ec_secret;

    enum ErrorType
    {
        None,
        InvalidResultFormat,
        IOError,
        EthError,
        EmptyResult
    };

    struct Error
    {
        ErrorType m_type;
        std::string m_message;
    };
    
    using Ptr = std::shared_ptr<IBridge>;
    virtual ~IBridge() {};

    virtual void getBalance(std::function<void(const Error&, const std::string&)> callback) = 0;
    virtual void getTokenBalance(
        const std::string& contractAddr, 
        std::function<void(const Error&, const std::string&)> callback) = 0;
    virtual void getBlockNumber(std::function<void(const Error&, uint64_t)> callback) = 0;
    virtual void getTransactionCount(std::function<void(const Error&, Amount)> callback) = 0;
    virtual void sendRawTransaction(const std::string& rawTx, std::function<void(const Error&, std::string)> callback) = 0;
    virtual void send(
        const short_hash& to,
        const data_chunk& data,
        const ECC::uintBig& value,
        const ECC::uintBig& gas,
        const ECC::uintBig& gasPrice,
        std::function<void(const Error&, std::string, uint64_t)> callback) = 0;
    virtual void getTransactionReceipt(const std::string& txHash, std::function<void(const Error&, const nlohmann::json&)> callback) = 0;
    virtual void getTxBlockNumber(const std::string& txHash, std::function<void(const Error&, uint64_t)> callback) = 0;
    virtual void getTxByHash(const std::string& txHash, std::function<void(const Error&, const nlohmann::json&)> callback) = 0;
    virtual void call(const short_hash& to, const std::string& data, std::function<void(const Error&, const nlohmann::json&)> callback) = 0;
    virtual short_hash generateEthAddress() const = 0;
    virtual void getGasPrice(std::function<void(const Error&, Amount)> callback) = 0;
};
} // namespace beam::ethereum