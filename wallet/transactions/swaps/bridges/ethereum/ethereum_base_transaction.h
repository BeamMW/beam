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

#pragma once
#include <bitcoin/bitcoin.hpp>

#include "core/ecc.h"


namespace beam::ethereum
{

struct EthBaseTransaction
{
    libbitcoin::short_hash m_from;
    libbitcoin::short_hash m_receiveAddress;
    ECC::uintBig m_value = ECC::Zero;
    beam::ByteBuffer m_data;
    ECC::uintBig m_nonce = ECC::Zero;
    ECC::uintBig m_gas = ECC::Zero;
    ECC::uintBig m_gasPrice = ECC::Zero;

    beam::ByteBuffer GetRawSigned(const libbitcoin::ec_secret& secret);
    bool Sign(libbitcoin::recoverable_signature& out, const libbitcoin::ec_secret& secret);
};

} // namespace beam::ethereum