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

#include "utility/io/address.h"
#include "utility/common.h"
#include "wallet/common.h"

namespace beam
{
    struct BitcoinOptions
    {
        std::string m_userName;
        std::string m_pass;
        io::Address m_address;
        Amount m_feeRate;
        uint16_t m_confirmations = 6;
        wallet::SwapSecondSideChainType m_chainType = wallet::SwapSecondSideChainType::Mainnet;
        uint32_t m_lockTimeInBlocks = 2 * 24 * 6;
    };
}