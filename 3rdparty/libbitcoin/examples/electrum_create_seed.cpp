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

#include "utility/logger.h"

#include "bitcoin/bitcoin.hpp"
#include "mnemonic/mnemonic.h"

#include <iostream>

using namespace beam;
using namespace libbitcoin;
using namespace libbitcoin::wallet;

void testValidateMnemonic()
{
    LOG_INFO() << "testValidateMnemonic";

    LOG_INFO() << "valid seed: " << electrum::validate_mnemonic({ "sunny", "ridge", "neutral", "address", "fossil", "gospel", "common", "brush", "cactus", "poverty", "fitness", "duty" });

    LOG_INFO() << "invalid seed: " << electrum::validate_mnemonic({ "ridge", "sunny", "neutral", "address", "fossil", "gospel", "common", "brush", "cactus", "poverty", "fitness", "duty" });
}

void generateSeed()
{
    LOG_INFO() << "generateSeed";

    auto wordList = electrum::create_mnemonic(getEntropy());

    LOG_INFO() << wordList;
    LOG_INFO() << "is valid = " << electrum::validate_mnemonic(wordList);
}

int main() {
    int logLevel = LOG_LEVEL_DEBUG;
    auto logger = Logger::create(logLevel, logLevel);    

    testValidateMnemonic();
    generateSeed();
    return 0;
}