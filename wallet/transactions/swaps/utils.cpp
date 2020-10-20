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

#include "wallet/transactions/swaps/utils.h"

#include "wallet/core/wallet.h"
#include "wallet/core/wallet_db.h"
#include "wallet/transactions/swaps/bridges/bitcoin/bitcoin.h"
#include "wallet/transactions/swaps/bridges/litecoin/electrum.h"
#include "wallet/transactions/swaps/bridges/qtum/electrum.h"
#include "wallet/transactions/swaps/bridges/litecoin/litecoin.h"
#include "wallet/transactions/swaps/bridges/qtum/qtum.h"
#include "wallet/transactions/swaps/bridges/dash/dash.h"
#include "wallet/transactions/swaps/bridges/bitcoin_cash/bitcoin_cash.h"
#include "wallet/transactions/swaps/bridges/bitcoin_sv/bitcoin_sv.h"
#include "wallet/transactions/swaps/bridges/dogecoin/dogecoin.h"
#include "wallet/transactions/swaps/bridges/ethereum/ethereum.h"

namespace beam::wallet
{

namespace
{
template<typename SettingsProvider, typename Electrum, typename Core, typename SecondSide, typename ISettingsProvider>
ISecondSideFactory::Ptr CreateFactory(IWalletDB::Ptr walletDB)
{
    auto settingsProvider = std::make_shared<SettingsProvider>(walletDB);
    settingsProvider->Initialize();

    // btcSettingsProvider stored in bitcoinBridgeCreator
    auto bridgeCreator = [provider = settingsProvider]() -> bitcoin::IBridge::Ptr
    {
        if (provider->GetSettings().IsElectrumActivated())
            return std::make_shared<Electrum>(io::Reactor::get_Current(), *provider);

        if (provider->GetSettings().IsCoreActivated())
            return std::make_shared<Core>(io::Reactor::get_Current(), *provider);

        return bitcoin::IBridge::Ptr();
    };

    return wallet::MakeSecondSideFactory<SecondSide, Electrum, ISettingsProvider>(bridgeCreator, *settingsProvider);
}

template<typename SettingsProvider, typename Bridge, typename SecondSide, typename ISettingsProvider>
ISecondSideFactory::Ptr CreateFactory(IWalletDB::Ptr walletDB)
{
    auto settingsProvider = std::make_shared<SettingsProvider>(walletDB);
    settingsProvider->Initialize();

    auto bridgeCreator = [provider = settingsProvider]() -> Bridge::Ptr
    {
        if (provider->GetSettings().IsActivated())
            return std::make_shared<Bridge>(io::Reactor::get_Current(), *provider);

        return Bridge::Ptr();
    };

    return wallet::MakeSecondSideFactory<SecondSide, Bridge, ISettingsProvider>(bridgeCreator, *settingsProvider);
}
} // namespace

const char* getSwapTxStatus(AtomicSwapTransaction::State state)
{
    static const char* Initial = "waiting for peer";
    static const char* BuildingBeamLockTX = "building Beam LockTX";
    static const char* BuildingBeamRefundTX = "building Beam RefundTX";
    static const char* BuildingBeamRedeemTX = "building Beam RedeemTX";
    static const char* HandlingContractTX = "handling LockTX";
    static const char* SendingRefundTX = "sending RefundTX";
    static const char* SendingRedeemTX = "sending RedeemTX";
    static const char* SendingBeamLockTX = "sending Beam LockTX";
    static const char* SendingBeamRefundTX = "sending Beam RefundTX";
    static const char* SendingBeamRedeemTX = "sending Beam RedeemTX";
    static const char* Completed = "completed";
    static const char* Canceled = "cancelled";
    static const char* Aborted = "aborted";
    static const char* Failed = "failed";

    switch (state)
    {
    case wallet::AtomicSwapTransaction::State::Initial:
        return Initial;
    case wallet::AtomicSwapTransaction::State::BuildingBeamLockTX:
        return BuildingBeamLockTX;
    case wallet::AtomicSwapTransaction::State::BuildingBeamRefundTX:
        return BuildingBeamRefundTX;
    case wallet::AtomicSwapTransaction::State::BuildingBeamRedeemTX:
        return BuildingBeamRedeemTX;
    case wallet::AtomicSwapTransaction::State::HandlingContractTX:
        return HandlingContractTX;
    case wallet::AtomicSwapTransaction::State::SendingRefundTX:
        return SendingRefundTX;
    case wallet::AtomicSwapTransaction::State::SendingRedeemTX:
        return SendingRedeemTX;
    case wallet::AtomicSwapTransaction::State::SendingBeamLockTX:
        return SendingBeamLockTX;
    case wallet::AtomicSwapTransaction::State::SendingBeamRefundTX:
        return SendingBeamRefundTX;
    case wallet::AtomicSwapTransaction::State::SendingBeamRedeemTX:
        return SendingBeamRedeemTX;
    case wallet::AtomicSwapTransaction::State::CompleteSwap:
        return Completed;
    case wallet::AtomicSwapTransaction::State::Canceled:
        return Canceled;
    case wallet::AtomicSwapTransaction::State::Refunded:
        return Aborted;
    case wallet::AtomicSwapTransaction::State::Failed:
        return Failed;
    default:
        assert(false && "Unexpected status");
    }

    return "";
}

/// Swap Parameters 
TxParameters InitNewSwap(
    const WalletID& myID, Height minHeight, Amount amount,
    Amount fee, AtomicSwapCoin swapCoin, Amount swapAmount, Amount swapFee,
    bool isBeamSide/* = true*/, Height lifetime/*= kDefaultTxLifetime*/,
    Height responseTime/* = kDefaultTxResponseTime*/)
{
    auto swapTxParameters = CreateSwapTransactionParameters();
    FillSwapTxParams(&swapTxParameters,
                     myID,
                     minHeight,
                     amount,
                     fee,
                     swapCoin,
                     swapAmount,
                     swapFee,
                     isBeamSide);
    return swapTxParameters;
}

void RegisterSwapTxCreators(Wallet::Ptr wallet, IWalletDB::Ptr walletDB)
{
    auto swapTransactionCreator = std::make_shared<AtomicSwapTransaction::Creator>(walletDB);
    wallet->RegisterTransactionType(TxType::AtomicSwap, std::static_pointer_cast<BaseTransaction::Creator>(swapTransactionCreator));

    swapTransactionCreator->RegisterFactory(
        AtomicSwapCoin::Bitcoin, 
        CreateFactory
            <bitcoin::SettingsProvider, 
             bitcoin::Electrum, 
             bitcoin::BitcoinCore017,
             BitcoinSide, 
             bitcoin::ISettingsProvider>(walletDB));

    swapTransactionCreator->RegisterFactory(
        AtomicSwapCoin::Litecoin,
        CreateFactory
            <litecoin::SettingsProvider,
             litecoin::Electrum, 
             litecoin::LitecoinCore017,
             LitecoinSide, 
             litecoin::ISettingsProvider>(walletDB));

    swapTransactionCreator->RegisterFactory(
        AtomicSwapCoin::Qtum,
        CreateFactory
            <qtum::SettingsProvider,
             qtum::Electrum, 
             qtum::QtumCore017,
             QtumSide, 
             qtum::ISettingsProvider>(walletDB));

    swapTransactionCreator->RegisterFactory(
        AtomicSwapCoin::Bitcoin_Cash,
        CreateFactory
            <bitcoin_cash::SettingsProvider,
             bitcoin_cash::Electrum, 
             bitcoin_cash::BitcoinCashCore,
             BitcoinCashSide, 
             bitcoin_cash::ISettingsProvider>(walletDB));

    swapTransactionCreator->RegisterFactory(
        AtomicSwapCoin::Bitcoin_SV,
        CreateFactory
            <bitcoin_sv::SettingsProvider,
             bitcoin_sv::Electrum, 
             bitcoin_sv::BitcoinSVCore,
             BitcoinSVSide, 
             bitcoin_sv::ISettingsProvider>(walletDB));

    swapTransactionCreator->RegisterFactory(
        AtomicSwapCoin::Dogecoin,
        CreateFactory
            <dogecoin::SettingsProvider,
             dogecoin::Electrum, 
             dogecoin::DogecoinCore014,
             DogecoinSide, 
             dogecoin::ISettingsProvider>(walletDB));

    swapTransactionCreator->RegisterFactory(
        AtomicSwapCoin::Dash,
        CreateFactory
            <dash::SettingsProvider,
             dash::Electrum, 
             dash::DashCore014,
             DashSide, 
             dash::ISettingsProvider>(walletDB));

    swapTransactionCreator->RegisterFactory(
        AtomicSwapCoin::Ethereum,
        CreateFactory
            <ethereum::SettingsProvider,
             ethereum::EthereumBridge,
             EthereumSide, 
             ethereum::ISettingsProvider>(walletDB));
}

bool IsSwapAmountValid(
    AtomicSwapCoin swapCoin, Amount swapAmount, Amount swapFeeRate)
{
    switch (swapCoin)
    {
    case AtomicSwapCoin::Bitcoin:
        return BitcoinSide::CheckAmount(swapAmount, swapFeeRate);
    case AtomicSwapCoin::Litecoin:
        return LitecoinSide::CheckAmount(swapAmount, swapFeeRate);
    case AtomicSwapCoin::Qtum:
        return QtumSide::CheckAmount(swapAmount, swapFeeRate);
    case AtomicSwapCoin::Bitcoin_Cash:
        return BitcoinCashSide::CheckAmount(swapAmount, swapFeeRate);
    case AtomicSwapCoin::Bitcoin_SV:
        return BitcoinSVSide::CheckAmount(swapAmount, swapFeeRate);
    case AtomicSwapCoin::Dogecoin:
        return DogecoinSide::CheckAmount(swapAmount, swapFeeRate);
    case AtomicSwapCoin::Dash:
        return DashSide::CheckAmount(swapAmount, swapFeeRate);
    case AtomicSwapCoin::Ethereum:
        // TODO roman.strilets
        return true;
    default:
        throw std::runtime_error("Unsupported coin for swap");
    }
}
} // namespace beam::wallet
