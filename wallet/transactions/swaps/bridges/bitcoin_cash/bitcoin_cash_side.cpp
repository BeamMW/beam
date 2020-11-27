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

#include "bitcoin_cash_side.h"
#include "common.h"

#include "bitcoin/bitcoin.hpp"

namespace
{
    constexpr uint32_t kBCHWithdrawTxAverageSize = 360;
}

namespace beam::wallet
{
    BitcoinCashSide::BitcoinCashSide(BaseTransaction& tx, bitcoin::IBridge::Ptr bitcoinBridge, bitcoin_cash::ISettingsProvider& settingsProvider, bool isBeamSide)
        : BitcoinSide(tx, bitcoinBridge, settingsProvider, isBeamSide)
    {
    }

    bool BitcoinCashSide::CheckAmount(Amount amount, Amount feeRate)
    {
        Amount fee = CalcTotalFee(feeRate);
        return amount > fee && (amount - fee) >= bitcoin::kDustThreshold;
    }

    Amount BitcoinCashSide::CalcTotalFee(Amount feeRate)
    {
        return static_cast<Amount>(std::round(double(kBCHWithdrawTxAverageSize * feeRate) / 1000));
    }

    uint8_t BitcoinCashSide::GetSighashAlgorithm() const
    {
        return libbitcoin::machine::sighash_algorithm::all | 0x40;
    }

    bool BitcoinCashSide::NeedSignValue() const
    {
        return true;
    }

    bool BitcoinCashSide::IsSegwitSupported() const
    {
        return false;
    }
}