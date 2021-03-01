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
#include "swap_eth_client.h"

using namespace beam;

namespace
{
    const unsigned int kBalanceUpdateInterval = 10 * 1000; // 10 seconds
    const unsigned int kPriceGasUpdateInterval = 60 * 1000; // 1 minute
}

SwapEthClient::SwapEthClient(
    ethereum::IBridgeHolder::Ptr bridgeHolder,
    std::unique_ptr<beam::ethereum::SettingsProvider> settingsProvider,
    io::Reactor& reactor)
    : ethereum::Client(bridgeHolder, std::move(settingsProvider), reactor)
    , _timer(io::Timer::create(reactor))
    , _feeTimer(io::Timer::create(reactor))
    , _status(Status::Unknown)
{
    requestBalance();
    requestRecommendedFeeRate();
    _timer->start(kBalanceUpdateInterval, true, [this]()
    {
        requestBalance();
    });

    _feeTimer->start(kPriceGasUpdateInterval, true, [this]()
    {
        requestRecommendedFeeRate();
    });
}

Amount SwapEthClient::GetAvailable(beam::wallet::AtomicSwapCoin swapCoin) const
{
    auto iter = _balances.find(swapCoin);
    if (iter != _balances.end())
    {
        return iter->second;
    }
    return 0;
}

Amount SwapEthClient::GetRecommendedFeeRate() const
{
    return _recommendedFeeRate;
}

bool SwapEthClient::IsConnected() const
{
    return _status == Status::Connected;
}

void SwapEthClient::requestBalance()
{
    if (GetSettings().IsActivated())
    {
        // update balance
        GetAsync()->GetBalance(beam::wallet::AtomicSwapCoin::Ethereum);

        for (auto token : beam::wallet::kEthTokens)
        {
            GetAsync()->GetBalance(token);
        }
    }
}

void SwapEthClient::requestRecommendedFeeRate()
{
    if (GetSettings().IsActivated())
    {
        // update recommended fee rate
        GetAsync()->EstimateGasPrice();
    }
}

void SwapEthClient::OnStatus(Status status)
{
    _status = status;
}

void SwapEthClient::OnBalance(beam::wallet::AtomicSwapCoin swapCoin, beam::Amount balance)
{
    _balances[swapCoin] = balance;
}

void SwapEthClient::OnEstimatedGasPrice(Amount feeRate)
{
    _recommendedFeeRate = feeRate;
}

void SwapEthClient::OnCanModifySettingsChanged(bool canModify)
{}

void SwapEthClient::OnChangedSettings()
{}

void SwapEthClient::OnConnectionError(beam::ethereum::IBridge::ErrorType error)
{}