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

#include "common.h"

#include "bitcoin/bitcoin.hpp"

namespace beam::bitcoin
{
    uint8_t getAddressVersion()
    {
#ifdef BEAM_MAINNET
        return libbitcoin::wallet::ec_private::mainnet_p2kh;
#else

        return libbitcoin::wallet::ec_private::testnet_p2kh;
#endif
    }

    bool validateElectrumMnemonic(const std::vector<std::string>& words)
    {
        return libbitcoin::wallet::electrum::validate_mnemonic(words, libbitcoin::wallet::language::electrum::en);
    }

    std::vector<std::string> createElectrumMnemonic(const std::vector<uint8_t>& entropy)
    {
        return libbitcoin::wallet::electrum::create_mnemonic(entropy);
    }
} // namespace beam::bitcoin
