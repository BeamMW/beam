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
#pragma once

#include "core/uintBig.h"
#include "dictionary.h"

#include <vector>
#include <string>

namespace beam
{
    class MnemonicException : public std::runtime_error {
    public:
        explicit MnemonicException(const std::string& msg)
            : std::runtime_error(msg.c_str())
        {
        }

        explicit MnemonicException(const char *msg)
            : std::runtime_error(msg)
        {
        }
    };

    typedef std::vector<std::string> WordList;

    const size_t WORD_COUNT = 12;

    std::vector<uint8_t> getEntropy();

    // implementation of bip39 for 12 words
    // TODO implement other version of bip39
    WordList createMnemonic(const std::vector<uint8_t>& entropy, const Dictionary& dict);

    std::vector<uint8_t> decodeMnemonic(const WordList& words);

    bool isAllowedWord(const std::string& word, const Dictionary& dict);
    bool isValidMnemonic(const WordList& words, const Dictionary& dict);
}