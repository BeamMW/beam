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

namespace beam::ethereum
{
class IBridge
{
public:
    virtual ~IBridge() {};

    virtual void getBalance(std::function<void(ECC::uintBig)> callback) = 0;
    virtual void getBlockNumber(std::function<void(Amount)> callback) = 0;
    virtual void getTransactionCount(std::function<void(Amount)> callback) = 0;
    virtual void sendRawTransaction(const std::string& rawTx, std::function<void(std::string)> callback) = 0;
    virtual void getTransactionReceipt(const std::string& txHash, std::function<void()> callback) = 0;
    virtual void call(const std::string& to, const std::string& data, std::function<void()> callback) = 0;
    virtual std::string generateEthAddress() const = 0;
};
} // namespace beam::ethereum