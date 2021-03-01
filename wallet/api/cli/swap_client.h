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
#include "wallet/transactions/swaps/bridges/bitcoin/client.h"

class SwapClient : public beam::bitcoin::Client
{
public:
    using Ptr = std::shared_ptr<SwapClient>;

    SwapClient(
        beam::bitcoin::IBridgeHolder::Ptr bridgeHolder,
        std::unique_ptr<beam::bitcoin::SettingsProvider> settingsProvider,
        beam::io::Reactor& reactor);

    beam::Amount GetAvailable() const;
    beam::Amount GetRecommendedFeeRate() const;
    bool IsConnected() const;
    
private:
    void requestBalance();
    void requestRecommendedFeeRate();

    void OnStatus(Status status) override;
    void OnBalance(const Balance& balance) override;
    void OnEstimatedFeeRate(beam::Amount feeRate) override;
    void OnCanModifySettingsChanged(bool canModify) override;
    void OnChangedSettings() override;
    void OnConnectionError(beam::bitcoin::IBridge::ErrorType error) override;

private:
    beam::io::Timer::Ptr _timer;
    beam::io::Timer::Ptr _feeTimer;
    Balance _balance;
    beam::Amount _recommendedFeeRate = 0;
    Status _status;
};
