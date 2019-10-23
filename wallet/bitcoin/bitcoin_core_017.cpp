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

#include "bitcoin_core_017.h"

#include "bitcoin/bitcoin.hpp"
#include "nlohmann/json.hpp"
#include "utility/logger.h"

using json = nlohmann::json;

namespace beam::bitcoin
{
    BitcoinCore017::BitcoinCore017(io::Reactor& reactor, IBitcoinCoreSettingsProvider& settingsProvider)
        : BitcoinCore016(reactor, settingsProvider)
    {
    }

    void BitcoinCore017::signRawTransaction(const std::string& rawTx, std::function<void(const IBridge::Error&, const std::string&, bool)> callback)
    {
        LOG_DEBUG() << "Send signrawtransactionwithwallet command";

        sendRequest("signrawtransactionwithwallet", "\"" + rawTx + "\"", [callback](IBridge::Error error, const json& result) {
            std::string hex;
            bool isComplete = false;

            if (error.m_type == IBridge::None)
            {
                try
                {
                    hex = result["hex"].get<std::string>();
                    isComplete = result["complete"].get<bool>();
                }
                catch (const std::exception& ex)
                {
                    error.m_type = IBridge::InvalidResultFormat;
                    error.m_message = ex.what();
                }
            }

            callback(error, hex, isComplete);
        });
    }

    void BitcoinCore017::createRawTransaction(
        const std::string& withdrawAddress,
        const std::string& contractTxId,
        Amount amount,
        int outputIndex,
        Timestamp locktime,
        std::function<void(const IBridge::Error&, const std::string&)> callback)
    {
        LOG_DEBUG() << "Send createRawTransaction command";

        std::string args("[{\"txid\": \"" + contractTxId + "\", \"vout\":" + std::to_string(outputIndex) + ", \"Sequence\": " + std::to_string(libbitcoin::max_input_sequence - 1) + " }]");

        args += ",[{\"" + withdrawAddress + "\": " + std::to_string(double(amount) / libbitcoin::satoshi_per_bitcoin) + "}]";
        if (locktime)
        {
            args += "," + std::to_string(locktime);
        }
        sendRequest("createrawtransaction", args, [callback](IBridge::Error error, const json& result) {
            std::string tx;

            if (error.m_type == IBridge::None)
            {
                try
                {
                    tx = result.get<std::string>();
                }
                catch (const std::exception& ex)
                {
                    error.m_type = IBridge::InvalidResultFormat;
                    error.m_message = ex.what();
                }
            }

            callback(error, tx);
        });
    }
} // namespace beam::bitcoin