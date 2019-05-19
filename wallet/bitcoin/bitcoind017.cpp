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

#include "bitcoind017.h"

#include "bitcoin/bitcoin.hpp"
#include "nlohmann/json.hpp"

using json = nlohmann::json;

namespace beam
{
    Bitcoind017::Bitcoind017(io::Reactor& reactor, const std::string& userName, const std::string& pass, const io::Address& address, Amount feeRate, bool mainnet)
        : Bitcoind016(reactor, userName, pass, address, feeRate, mainnet)
    {
    }

    void Bitcoind017::signRawTransaction(const std::string& rawTx, std::function<void(const std::string&, const std::string&, bool)> callback)
    {
        sendRequest("signrawtransactionwithwallet", "\"" + rawTx + "\"", [callback](const std::string& response) {
            json reply = json::parse(response);
            std::string error = reply["error"].empty() ? "" : reply["error"]["message"].get<std::string>();
            const auto& result = reply["result"];
            std::string hex;
            bool isComplete = false;
            if (!result.empty())
            {
                hex = result["hex"].get<std::string>();
                isComplete = result["complete"].get<bool>();
            }

            callback(error, hex, isComplete);
        });
    }

    void Bitcoind017::createRawTransaction(
        const std::string& withdrawAddress,
        const std::string& contractTxId,
        uint64_t amount,
        int outputIndex,
        Timestamp locktime,
        std::function<void(const std::string&, const std::string&)> callback)
    {
        std::string args("[{\"txid\": \"" + contractTxId + "\", \"vout\":" + std::to_string(outputIndex) + ", \"Sequence\": " + std::to_string(libbitcoin::max_input_sequence - 1) + " }]");

        args += ",[{\"" + withdrawAddress + "\": " + std::to_string(double(amount) / libbitcoin::satoshi_per_bitcoin) + "}]";
        if (locktime)
        {
            args += "," + std::to_string(locktime);
        }
        sendRequest("createrawtransaction", args, [callback](const std::string& response) {
            json reply = json::parse(response);
            std::string error = reply["error"].empty() ? "" : reply["error"]["message"].get<std::string>();
            std::string result = reply["result"].empty() ? "" : reply["result"].get<std::string>();

            callback(error, result);
        });
    }
}