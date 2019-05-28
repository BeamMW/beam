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

#include "bitcoind016.h"

namespace beam
{
    class Bitcoind017 : public Bitcoind016
    {
    public:
        Bitcoind017() = delete;
        Bitcoind017(io::Reactor& reactor, BitcoinOptions options);

        void signRawTransaction(const std::string& rawTx, std::function<void(const IBitcoinBridge::Error&, const std::string&, bool)> callback) override;
        void createRawTransaction(
            const std::string& withdrawAddress,
            const std::string& contractTxId,
            uint64_t amount,
            int outputIndex,
            Timestamp locktime,
            std::function<void(const IBitcoinBridge::Error&, const std::string&)> callback) override;
    };
}