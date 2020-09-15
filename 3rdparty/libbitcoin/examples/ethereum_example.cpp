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

#include <bitcoin/bitcoin.hpp>
#include <ethash/keccak.hpp>
#include <iostream>
#include <vector>

#include "utility\hex.h"

libbitcoin::wallet::hd_private ProcessHDPrivate(const libbitcoin::wallet::hd_private& privateKey, uint32_t index, bool hard = true)
{
    static constexpr auto first = libbitcoin::wallet::hd_first_hardened_key;
    const auto position = hard ? first + index : index;
    return privateKey.derive_private(position);
}

void GenerateAddress(uint32_t index)
{
    std::vector<std::string> words = { "grass", "happy", "napkin", "skill", "hazard", "isolate", "slot", "barely", "stamp", "dismiss", "there", "found"};
    //std::vector<std::string> words = { "radar", "blur", "cabbage", "chef", "fix", "engine", "embark", "joy", "scheme", "fiction", "master", "release" };
    auto seed = libbitcoin::wallet::decode_mnemonic(words);
    libbitcoin::data_chunk seed_chunk(libbitcoin::to_chunk(seed));

    const auto prefixes = libbitcoin::wallet::hd_private::to_prefixes(0, 0);
    libbitcoin::wallet::hd_private private_key(seed_chunk, prefixes);

    private_key = ProcessHDPrivate(private_key, 44);
    private_key = ProcessHDPrivate(private_key, 60);
    private_key = ProcessHDPrivate(private_key, 0);
    private_key = ProcessHDPrivate(private_key, 0, false);
    private_key = ProcessHDPrivate(private_key, index, false);

    std::cout << private_key.encoded() << std::endl;
    std::cout << libbitcoin::encode_base16(private_key.secret()) << std::endl;


    libbitcoin::ec_compressed point;

    libbitcoin::secret_to_public(point, private_key.secret());

    auto pk = libbitcoin::wallet::ec_public(point, false);
    auto rawPk = pk.encoded();

    std::cout << rawPk << std::endl;    

    auto tmp = beam::from_hex(std::string(rawPk.begin() + 2, rawPk.end()));

    auto hash = ethash::keccak256(&tmp[0], tmp.size());
    libbitcoin::data_chunk data;
    for (int i = 12; i < 32; i++)
    {
        data.push_back(hash.bytes[i]);
    }

    std::cout << libbitcoin::encode_base16(data);
}

int main()
{
    GenerateAddress(1);

    return 0;
}