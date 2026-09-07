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
#include <boost/optional.hpp>
#include <chrono>
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

    // Balance of an arbitrary ERC-20 contract, in wallet units for the given
    // decimals. Returns boost::none until the first refresh cycle has answered
    // for this contract; the contract is registered for polling as a side
    // effect and dropped again once nobody has asked about it for a while.
    boost::optional<beam::Amount> GetTokenAvailable(const std::string& tokenContract, uint8_t decimals);

private:
    void requestBalance();
    void requestRecommendedFeeRate();

    void OnStatus(Status status) override;
    void OnBalance(beam::wallet::AtomicSwapCoin swapCoin, beam::Amount balance) override;
    void OnTokenBalance(const std::string& tokenContract, beam::Amount balance) override;
    void OnEstimatedGasPrice(beam::Amount feeRate) override;
    void OnCanModifySettingsChanged(bool canModify) override;
    void OnChangedSettings() override;
    void OnConnectionError(beam::ethereum::IBridge::ErrorType error) override;

private:
    beam::io::Timer::Ptr _timer;
    beam::io::Timer::Ptr _feeTimer;
    std::map<beam::wallet::AtomicSwapCoin, beam::Amount> _balances;
    struct WatchedToken
    {
        uint8_t m_decimals;
        std::chrono::steady_clock::time_point m_lastUse;
    };
    std::map<std::string, WatchedToken> _watchedTokens;
    std::map<std::string, beam::Amount> _tokenBalances;
    beam::Amount _recommendedFeeRate = 0;
    Status _status;
};
