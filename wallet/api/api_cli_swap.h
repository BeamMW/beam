// Copyright 2018 The Beam Team
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

#if defined(BEAM_ATOMIC_SWAP_SUPPORT)
#include "wallet/transactions/swaps/utils.h"
#include "wallet/client/extensions/broadcast_gateway/broadcast_router.h"
#include "wallet/transactions/swaps/bridges/bitcoin/client.h"
#include "wallet/transactions/swaps/bridges/bitcoin/bridge_holder.h"
#include "wallet/transactions/swaps/bridges/bitcoin/bitcoin.h"
#include "wallet/transactions/swaps/bridges/litecoin/litecoin.h"
#include "wallet/transactions/swaps/bridges/qtum/qtum.h"
#include "wallet/transactions/swaps/bridges/dogecoin/dogecoin.h"
#if defined(BITCOIN_CASH_SUPPORT)
#include "wallet/transactions/swaps/bridges/bitcoin_cash/bitcoin_cash.h"
#endif // BITCOIN_CASH_SUPPORT
#include "wallet/transactions/swaps/bridges/dash/dash.h"
#include "wallet/api/api_swaps_provider.h"
#include "wallet/client/extensions/offers_board/swap_offers_board.h"
#include "api_swaps_provider.h"
#endif  // BEAM_ATOMIC_SWAP_SUPPORT

using namespace beam;
using namespace beam::wallet;

using BaseSwapClient = beam::bitcoin::Client;
class SwapClient : public BaseSwapClient
{
public:
    using Ptr = std::shared_ptr<SwapClient>;
    SwapClient(
        beam::bitcoin::IBridgeHolder::Ptr bridgeHolder,
        std::unique_ptr<beam::bitcoin::SettingsProvider> settingsProvider,
        io::Reactor& reactor)
        : BaseSwapClient(bridgeHolder,
                         std::move(settingsProvider),
                         reactor)
        , _timer(beam::io::Timer::create(reactor))
        , _feeTimer(beam::io::Timer::create(reactor))
        , _status(Status::Unknown)
    {
        requestBalance();
        requestRecommendedFeeRate();
        _timer->start(1000, true, [this] ()
        {
            requestBalance();
        });

        // TODO need name for the parameter
        _feeTimer->start(60 * 1000, true, [this]()
        {
            requestRecommendedFeeRate();
        });
    }

    Amount GetAvailable() const
    {
        return _balance.m_available;
    }

    Amount GetRecommendedFeeRate() const
    {
        return _recommendedFeeRate;
    }

    bool IsConnected() const
    {
        return _status == Status::Connected;
    }

private:
    beam::io::Timer::Ptr _timer;
    beam::io::Timer::Ptr _feeTimer;
    Balance _balance;
    Amount _recommendedFeeRate = 0;
    Status _status;
    void requestBalance()
    {
        if (GetSettings().IsActivated())
        {
            // update balance
            GetAsync()->GetBalance();
        }
    }
    void requestRecommendedFeeRate()
    {
        if (GetSettings().IsActivated())
        {
            // update recommended fee rate
            GetAsync()->EstimateFeeRate();
        }
    }
    void OnStatus(Status status) override
    {
        _status = status;
    }
    void OnBalance(const Balance& balance) override
    {
        _balance = balance;
    }
    void OnEstimatedFeeRate(Amount feeRate) override
    {
        _recommendedFeeRate = feeRate;
    }
    void OnCanModifySettingsChanged(bool canModify) override {}
    void OnChangedSettings() override {}
    void OnConnectionError(beam::bitcoin::IBridge::ErrorType error) override {}
};

class ApiCliSwap
    : public ISwapsProvider
    , ISwapOffersObserver
{
public:
    explicit ApiCliSwap(IWalletDB::Ptr wdb)
        : _walletDB(std::move(wdb))
    {
    }

    void initSwapFeature(proto::FlyClient::INetwork& nnet, IWalletMessageEndpoint& wnet)
    {
        _broadcastRouter = std::make_shared<BroadcastRouter>(nnet, wnet);
        _offerBoardProtocolHandler = std::make_shared<OfferBoardProtocolHandler>(_walletDB->get_SbbsKdf());
        _offersBulletinBoard = std::make_shared<SwapOffersBoard>(*_broadcastRouter, *_offerBoardProtocolHandler, _walletDB);
        _walletDbSubscriber = std::make_unique<WalletDbSubscriber>(static_cast<IWalletDbObserver*>(_offersBulletinBoard.get()), _walletDB);
        _swapOffersBoardSubscriber = std::make_unique<SwapOffersBoardSubscriber>(static_cast<ISwapOffersObserver*>(this), _offersBulletinBoard);

        initSwapClient<bitcoin::BitcoinCore017, bitcoin::Electrum, bitcoin::SettingsProvider>(AtomicSwapCoin::Bitcoin);
        initSwapClient<litecoin::LitecoinCore017, litecoin::Electrum, litecoin::SettingsProvider>(AtomicSwapCoin::Litecoin);
        initSwapClient<qtum::QtumCore017, qtum::Electrum, qtum::SettingsProvider>(AtomicSwapCoin::Qtum);
        initSwapClient<dash::DashCore014, dash::Electrum, dash::SettingsProvider>(AtomicSwapCoin::Dash);
#if defined(BITCOIN_CASH_SUPPORT)
        initSwapClient<bitcoin_cash::BitcoinCashCore, bitcoin_cash::Electrum, bitcoin_cash::SettingsProvider>(AtomicSwapCoin::Bitcoin_Cash);
#endif // BITCOIN_CASH_SUPPORT
        initSwapClient<dogecoin::DogecoinCore014, dogecoin::Electrum, dogecoin::SettingsProvider>(AtomicSwapCoin::Dogecoin);
    }

private:
    [[nodiscard]] beam::Amount getCoinAvailable(AtomicSwapCoin swapCoin) const override
    {
        auto swapClient = getSwapCoinClient(swapCoin);
        return swapClient ? swapClient->GetAvailable() : 0;
    }

    [[nodiscard]] beam::Amount getRecommendedFeeRate(AtomicSwapCoin swapCoin) const override
    {
        auto swapClient = getSwapCoinClient(swapCoin);
        return swapClient ? swapClient->GetRecommendedFeeRate() : 0;
    }

    [[nodiscard]] beam::Amount getMinFeeRate(AtomicSwapCoin swapCoin) const override
    {
        auto swapClient = getSwapCoinClient(swapCoin);
        return swapClient ? swapClient->GetSettings().GetMinFeeRate() : 0;
    }

    [[nodiscard]] const SwapOffersBoard& getSwapOffersBoard() const override
    {
        return *_offersBulletinBoard;
    }

    [[nodiscard]] bool isCoinClientConnected(AtomicSwapCoin swapCoin) const override
    {
        auto swapClient = getSwapCoinClient(swapCoin);
        return swapClient && swapClient->IsConnected();
    }

    [[nodiscard]] SwapClient::Ptr getSwapCoinClient(beam::wallet::AtomicSwapCoin swapCoin) const
    {
        auto it = _swapClients.find(swapCoin);
        if (it != _swapClients.end())
        {
            return it->second;
        }
        return nullptr;
    }

    void onSwapOffersChanged(ChangeAction action, const std::vector<SwapOffer>& offers) override
    {
    }

    using WalletDbSubscriber = ScopedSubscriber<wallet::IWalletDbObserver, wallet::IWalletDB>;
    using SwapOffersBoardSubscriber = ScopedSubscriber<wallet::ISwapOffersObserver, wallet::SwapOffersBoard>;

    template<typename CoreBridge, typename ElectrumBridge, typename SettingsProvider>
    void initSwapClient(beam::wallet::AtomicSwapCoin swapCoin)
    {
        auto bridgeHolder = std::make_shared<bitcoin::BridgeHolder<ElectrumBridge, CoreBridge>>();
        auto settingsProvider = std::make_unique<SettingsProvider>(_walletDB);
        settingsProvider->Initialize();
        auto client = std::make_shared<SwapClient>(bridgeHolder, std::move(settingsProvider), io::Reactor::get_Current());
        _swapClients.emplace(std::make_pair(swapCoin, client));
        _swapBridgeHolders.emplace(std::make_pair(swapCoin, bridgeHolder));
    }

private:
    std::map<beam::wallet::AtomicSwapCoin, SwapClient::Ptr> _swapClients;
    std::map<beam::wallet::AtomicSwapCoin, beam::bitcoin::IBridgeHolder::Ptr> _swapBridgeHolders;
    SwapOffersBoard::Ptr _offersBulletinBoard;
    std::shared_ptr<BroadcastRouter> _broadcastRouter;
    std::shared_ptr<OfferBoardProtocolHandler> _offerBoardProtocolHandler;
    std::unique_ptr<WalletDbSubscriber> _walletDbSubscriber;
    std::unique_ptr<SwapOffersBoardSubscriber> _swapOffersBoardSubscriber;
    WalletDB::Ptr _walletDB;
};
