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

#include "wallet/transactions/swaps/common.h"
#include "bitcoin/bitcoin.hpp"

namespace beam::wallet
{
AtomicSwapCoin from_string(const std::string& value)
{
    if (value == "btc")
        return AtomicSwapCoin::Bitcoin;
    else if (value == "ltc")
        return AtomicSwapCoin::Litecoin;
    else if (value == "qtum")
        return AtomicSwapCoin::Qtum;
    else if (value == "bch")
        return AtomicSwapCoin::Bitcoin_Cash;
    else if (value == "bsv")
        return AtomicSwapCoin::Bitcoin_SV;
    else if (value == "doge")
        return AtomicSwapCoin::Dogecoin;
    else if (value == "dash")
        return AtomicSwapCoin::Dash;

    return AtomicSwapCoin::Unknown;
}

uint64_t UnitsPerCoin(AtomicSwapCoin swapCoin) noexcept
{
    switch (swapCoin)
    {
    case AtomicSwapCoin::Bitcoin:
    case AtomicSwapCoin::Litecoin:
    case AtomicSwapCoin::Qtum:
    case AtomicSwapCoin::Bitcoin_Cash:
    case AtomicSwapCoin::Bitcoin_SV:
    case AtomicSwapCoin::Dogecoin:
    case AtomicSwapCoin::Dash:
        return libbitcoin::satoshi_per_bitcoin;
    default:
    {
        assert("Unsupported swapCoin type.");
        return 0;
    }
    }
}
}  // namespace beam::wallet

namespace std
{
string to_string(beam::wallet::SwapOfferStatus status)
{
    switch (status)
    {
    case beam::wallet::SwapOfferStatus::Pending:
        return "Pending";
    case beam::wallet::SwapOfferStatus::InProgress:
        return "InProgress";
    case beam::wallet::SwapOfferStatus::Completed:
        return "Completed";
    case beam::wallet::SwapOfferStatus::Canceled:
        return "Canceled";
    case beam::wallet::SwapOfferStatus::Expired:
        return "Expired";
    case beam::wallet::SwapOfferStatus::Failed:
        return "Failed";

    default:
        return "";
    }
}

string to_string(beam::wallet::AtomicSwapCoin value)
{
    switch (value)
    {
    case beam::wallet::AtomicSwapCoin::Bitcoin:
        return "BTC";
    case beam::wallet::AtomicSwapCoin::Litecoin:
        return "LTC";
    case beam::wallet::AtomicSwapCoin::Qtum:
        return "QTUM";
    case beam::wallet::AtomicSwapCoin::Bitcoin_Cash:
        return "BCH";
    case beam::wallet::AtomicSwapCoin::Bitcoin_SV:
        return "BSV";
    case beam::wallet::AtomicSwapCoin::Dogecoin:
        return "DOGE";
    case beam::wallet::AtomicSwapCoin::Dash:
        return "DASH";
    default:
        return "";
    }
}
}  // namespace std 
