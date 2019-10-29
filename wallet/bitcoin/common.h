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

#include <stdint.h>
#include <vector>
#include <string>

namespace libbitcoin::wallet
{
    class ec_private;
    class hd_private;
}

namespace beam::bitcoin
{
    constexpr uint32_t kTransactionVersion = 2;
    constexpr uint64_t kDustThreshold = 546;
    constexpr uint32_t kBTCWithdrawTxAverageSize = 360;

    uint64_t btc_to_satoshi(double btc);
    uint8_t getAddressVersion();
    bool validateElectrumMnemonic(const std::vector<std::string>& words);
    std::vector<std::string> createElectrumMnemonic(const std::vector<uint8_t>& entropy);

    // the first key is receiving master private key
    // the second key is changing master private key
    std::pair<libbitcoin::wallet::hd_private, libbitcoin::wallet::hd_private> generateElectrumMasterPrivateKeys(const std::vector<std::string>& words);
    // return the indexth address for private key
    std::string getElectrumAddress(const libbitcoin::wallet::hd_private& privateKey, uint32_t index, uint8_t addressVersion);
    std::vector<std::string> generateReceivingAddresses(const std::vector<std::string>& words, uint32_t amount, uint8_t addressVersion);
    std::vector<std::string> generateChangeAddresses(const std::vector<std::string>& words, uint32_t amount, uint8_t addressVersion);
    bool isAllowedWord(const std::string& word);
} // namespace beam::bitcoin
