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

#include "mnemonic.h"

#include "core/ecc_native.h"
#include "pkcs5_pbkdf2.h"

#include <boost/algorithm/string.hpp>
#include <random>

namespace beam
{
    namespace
    {
        const size_t wordCount = 12;
        const size_t bitsPerWord = 11;
        const uint8_t byteBits = 8;
        const std::string passphrasePrefix = "mnemonic";
        const size_t hmacIterations = 2048;
        const size_t sizeHash = 512 >> 3;

        uint8_t shiftBits(size_t bit)
        {
            return (1 << (byteBits - (bit % byteBits) - 1));
        }
    }

    std::vector<uint8_t> getEntropy()
    {
        std::vector<uint8_t> res;
        res.resize(16);
        ECC::GenRandom(res.data(), static_cast<uint32_t>(res.size()));

        return res;
    }

    WordList createMnemonic(const std::vector<uint8_t>& entropy, const Dictionary& dict)
    {
        // entropy should be 16 bytes or 128 bits for 12 words
        assert(entropy.size() == 128 >> 3);

        ECC::Hash::Value value;
        Blob blob(entropy.data(), static_cast<uint32_t>(entropy.size()));
        ECC::Hash::Processor() << blob >> value;
        std::vector<uint8_t> data(entropy);
        data.push_back(value.m_pData[0]);
        
        std::vector<std::string> words;
        size_t bit = 0;

        for (size_t word = 0; word < wordCount; word++)
        {
            size_t position = 0;
            for (size_t loop = 0; loop < bitsPerWord; loop++)
            {
                bit = (word * bitsPerWord + loop);
                position <<= 1;

                const auto byte = bit / byteBits;

                if ((data[byte] & shiftBits(bit)) > 0)
                    position++;
            }

            words.push_back(dict[position]);
        }

        return words;
    }

    std::vector<uint8_t> decodeMnemonic(const WordList& words)
    {
        const std::string sentence = boost::join(words, " ");
        std::vector<uint8_t> passphrase(sentence.begin(), sentence.end());
        std::vector<uint8_t> salt(passphrasePrefix.begin(), passphrasePrefix.end());
        std::vector<uint8_t> hash(sizeHash);

        const auto result = pkcs5_pbkdf2(
            passphrase.data(),
            passphrase.size(),
            salt.data(),
            salt.size(),
            hash.data(), 
            hash.size(), 
            hmacIterations);

        if (result != 0)
            throw MnemonicException("pbkdf2 returned bad result");

        return hash;
    }
}