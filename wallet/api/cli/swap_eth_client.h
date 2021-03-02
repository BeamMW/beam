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
#include "wallet/transactions/swaps/bridges/ethereum/client.h"

class SwapEthClient : public beam::ethereum::Client
{
public:
    using Ptr = std::shared_ptr<SwapEthClient>;

    SwapEthClient(
        beam::ethereum::IBridgeHolder::Ptr bridgeHolder,
        std::unique_ptr<beam::ethereum::SettingsProvider> settingsProvider,
        beam::io::Reactor& reactor
    );

    beam::Amount GetAvailable(beam::wallet::AtomicSwapCoin swapCoin) const;
    beam::Amount GetRecommendedFeeRate() const;
    bool IsConnected() const;

private:
    void requestBalance();
    void requestRecommendedFeeRate();

    void OnStatus(Status status) override;
    void OnBalance(beam::wallet::AtomicSwapCoin swapCoin, beam::Amount balance) override;
    void OnEstimatedGasPrice(beam::Amount feeRate) override;
    void OnCanModifySettingsChanged(bool canModify) override;
    void OnChangedSettings() override;
    void OnConnectionError(beam::ethereum::IBridge::ErrorType error) override;

private:
    beam::io::Timer::Ptr _timer;
    beam::io::Timer::Ptr _feeTimer;
    std::map<beam::wallet::AtomicSwapCoin, beam::Amount> _balances;
    beam::Amount _recommendedFeeRate = 0;
    Status _status;
};
