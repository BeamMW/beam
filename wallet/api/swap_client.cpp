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

#include "swap_client.h"

//#include "wallet/transactions/swaps/bridges/bitcoin/bridge_holder.h"

using namespace beam;

SwapClient::SwapClient(
    bitcoin::IBridgeHolder::Ptr bridgeHolder,
    std::unique_ptr<beam::bitcoin::SettingsProvider> settingsProvider,
    io::Reactor& reactor)
    : bitcoin::Client(bridgeHolder, std::move(settingsProvider), reactor)
    , _timer(io::Timer::create(reactor))
    , _feeTimer(io::Timer::create(reactor))
    , _status(Status::Unknown)
{
    requestBalance();
    requestRecommendedFeeRate();
    _timer->start(1000, true, [this]()
    {
        requestBalance();
    });

    // TODO need name for the parameter
    _feeTimer->start(60 * 1000, true, [this]()
    {
        requestRecommendedFeeRate();
    });
}

Amount SwapClient::GetAvailable() const
{
    return _balance.m_available;
}

Amount SwapClient::GetRecommendedFeeRate() const
{
    return _recommendedFeeRate;
}

bool SwapClient::IsConnected() const
{
    return _status == Status::Connected;
}

void SwapClient::requestBalance()
{
    if (GetSettings().IsActivated())
    {
        // update balance
        GetAsync()->GetBalance();
    }
}

void SwapClient::requestRecommendedFeeRate()
{
    if (GetSettings().IsActivated())
    {
        // update recommended fee rate
        GetAsync()->EstimateFeeRate();
    }
}

void SwapClient::OnStatus(Status status)
{
    _status = status;
}

void SwapClient::OnBalance(const Balance& balance)
{
    _balance = balance;
}

void SwapClient::OnEstimatedFeeRate(Amount feeRate)
{
    _recommendedFeeRate = feeRate;
}

void SwapClient::OnCanModifySettingsChanged(bool canModify)
{}

void SwapClient::OnChangedSettings()
{}

void SwapClient::OnConnectionError(beam::bitcoin::IBridge::ErrorType error)
{}