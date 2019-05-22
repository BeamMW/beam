// Copyright 2019 The Beam Team
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
#include <memory>
#include <string>
#include <functional>

namespace beam
{
    class IBitcoinBridge
    {
    public:
        using Ptr = std::shared_ptr<IBitcoinBridge>;

        virtual ~IBitcoinBridge() {};

        // error, private key
        virtual void dumpPrivKey(const std::string& btcAddress, std::function<void(const std::string&, const std::string&)> callback) = 0;
        // error, transaction (hex), changepos
        virtual void fundRawTransaction(const std::string& rawTx, Amount feeRate, std::function<void(const std::string&, const std::string&, int)> callback) = 0;
        //error, transaction (hex), complete
        virtual void signRawTransaction(const std::string& rawTx, std::function<void(const std::string&, const std::string&, bool)> callback) = 0;
        // error, transaction ID
        virtual void sendRawTransaction(const std::string& rawTx, std::function<void(const std::string&, const std::string&)> callback) = 0;
        // error, address
        virtual void getRawChangeAddress(std::function<void(const std::string&, const std::string&)> callback) = 0;
        // error, transaction (hex)
        virtual void createRawTransaction(
            const std::string& withdrawAddress,
            const std::string& contractTxId,
            uint64_t amount,
            int outputIndex,
            Timestamp locktime,
            std::function<void(const std::string&, const std::string&)> callback) = 0;
        // error, value, script (hex), confirmations
        virtual void getTxOut(const std::string& txid, int outputIndex, std::function<void(const std::string&, const std::string&, double, uint16_t)> callback) = 0;
        // error, block count
        virtual void getBlockCount(std::function<void(const std::string&, uint64_t)> callback) = 0;

        virtual uint8_t getAddressVersion() = 0;
        virtual Amount getFeeRate() const = 0;
        virtual Amount getMaxConfirmations() const = 0;
    };
}