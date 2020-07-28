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

#ifdef BEAM_ATOMIC_SWAP_SUPPORT
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
#endif // BEAM_ATOMIC_SWAP_SUPPORT

namespace beam::wallet
{
#ifdef BEAM_ATOMIC_SWAP_SUPPORT
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

    {
        auto btcSettingsProvider = std::make_shared<bitcoin::SettingsProvider>(walletDB);
        btcSettingsProvider->Initialize();

        // btcSettingsProvider stored in bitcoinBridgeCreator
        auto bitcoinBridgeCreator = [settingsProvider = btcSettingsProvider]() -> bitcoin::IBridge::Ptr
        {
            if (settingsProvider->GetSettings().IsElectrumActivated())
                return std::make_shared<bitcoin::Electrum>(io::Reactor::get_Current(), *settingsProvider);

            if (settingsProvider->GetSettings().IsCoreActivated())
            return std::make_shared<bitcoin::BitcoinCore017>(io::Reactor::get_Current(), *settingsProvider);

            return bitcoin::IBridge::Ptr();
        };

        auto btcSecondSideFactory = wallet::MakeSecondSideFactory<BitcoinSide, bitcoin::Electrum, bitcoin::ISettingsProvider>(bitcoinBridgeCreator, *btcSettingsProvider);
        swapTransactionCreator->RegisterFactory(AtomicSwapCoin::Bitcoin, btcSecondSideFactory);
    }

    {
        auto ltcSettingsProvider = std::make_shared<litecoin::SettingsProvider>(walletDB);
        ltcSettingsProvider->Initialize();

        // ltcSettingsProvider stored in litecoinBridgeCreator
        auto litecoinBridgeCreator = [settingsProvider = ltcSettingsProvider]() -> bitcoin::IBridge::Ptr
        {
            if (settingsProvider->GetSettings().IsElectrumActivated())
                return std::make_shared<litecoin::Electrum>(io::Reactor::get_Current(), *settingsProvider);

            if (settingsProvider->GetSettings().IsCoreActivated())
            return std::make_shared<litecoin::LitecoinCore017>(io::Reactor::get_Current(), *settingsProvider);

            return bitcoin::IBridge::Ptr();
        };

        auto ltcSecondSideFactory = wallet::MakeSecondSideFactory<LitecoinSide, litecoin::Electrum, litecoin::ISettingsProvider>(litecoinBridgeCreator, *ltcSettingsProvider);
        swapTransactionCreator->RegisterFactory(AtomicSwapCoin::Litecoin, ltcSecondSideFactory);
    }

    {
        auto qtumSettingsProvider = std::make_shared<qtum::SettingsProvider>(walletDB);
        qtumSettingsProvider->Initialize();

        // qtumSettingsProvider stored in qtumBridgeCreator
        auto qtumBridgeCreator = [settingsProvider = qtumSettingsProvider]() -> bitcoin::IBridge::Ptr
        {
            if (settingsProvider->GetSettings().IsElectrumActivated())
                return std::make_shared<qtum::Electrum>(io::Reactor::get_Current(), *settingsProvider);

            if (settingsProvider->GetSettings().IsCoreActivated())
            return std::make_shared<qtum::QtumCore017>(io::Reactor::get_Current(), *settingsProvider);

            return bitcoin::IBridge::Ptr();
        };

        auto qtumSecondSideFactory = wallet::MakeSecondSideFactory<QtumSide, qtum::Electrum, qtum::ISettingsProvider>(qtumBridgeCreator, *qtumSettingsProvider);
        swapTransactionCreator->RegisterFactory(AtomicSwapCoin::Qtum, qtumSecondSideFactory);
    }

    {
        auto bchSettingsProvider = std::make_shared<bitcoin_cash::SettingsProvider>(walletDB);
        bchSettingsProvider->Initialize();

        // bchSettingsProvider stored in bchBridgeCreator
        auto bchBridgeCreator = [settingsProvider = bchSettingsProvider]() -> bitcoin::IBridge::Ptr
        {
            if (settingsProvider->GetSettings().IsElectrumActivated())
                return std::make_shared<bitcoin_cash::Electrum>(io::Reactor::get_Current(), *settingsProvider);

            if (settingsProvider->GetSettings().IsCoreActivated())
                return std::make_shared<bitcoin_cash::BitcoinCashCore>(io::Reactor::get_Current(), *settingsProvider);

            return bitcoin::IBridge::Ptr();
        };

        auto bchSecondSideFactory = wallet::MakeSecondSideFactory<BitcoinCashSide, bitcoin_cash::Electrum, bitcoin_cash::ISettingsProvider>(bchBridgeCreator, *bchSettingsProvider);
        swapTransactionCreator->RegisterFactory(AtomicSwapCoin::Bitcoin_Cash, bchSecondSideFactory);
    }

    {
        auto bsvSettingsProvider = std::make_shared<bitcoin_sv::SettingsProvider>(walletDB);
        bsvSettingsProvider->Initialize();

        // bsvSettingsProvider stored in bsvBridgeCreator
        auto bsvBridgeCreator = [settingsProvider = bsvSettingsProvider]() -> bitcoin::IBridge::Ptr
        {
            if (settingsProvider->GetSettings().IsElectrumActivated())
                return std::make_shared<bitcoin_sv::Electrum>(io::Reactor::get_Current(), *settingsProvider);

            if (settingsProvider->GetSettings().IsCoreActivated())
                return std::make_shared<bitcoin_sv::BitcoinSVCore>(io::Reactor::get_Current(), *settingsProvider);

            return bitcoin::IBridge::Ptr();
        };

        auto bsvSecondSideFactory = wallet::MakeSecondSideFactory<BitcoinSVSide, bitcoin_sv::Electrum, bitcoin_sv::ISettingsProvider>(bsvBridgeCreator, *bsvSettingsProvider);
        swapTransactionCreator->RegisterFactory(AtomicSwapCoin::Bitcoin_SV, bsvSecondSideFactory);
    }

    {
        auto dogecoinSettingsProvider = std::make_shared<dogecoin::SettingsProvider>(walletDB);
        dogecoinSettingsProvider->Initialize();

        // qtumSettingsProvider stored in qtumBridgeCreator
        auto dogecoinBridgeCreator = [settingsProvider = dogecoinSettingsProvider]() -> bitcoin::IBridge::Ptr
        {
            if (settingsProvider->GetSettings().IsElectrumActivated())
                return std::make_shared<dogecoin::Electrum>(io::Reactor::get_Current(), *settingsProvider);

            if (settingsProvider->GetSettings().IsCoreActivated())
                return std::make_shared<dogecoin::DogecoinCore014>(io::Reactor::get_Current(), *settingsProvider);

            return bitcoin::IBridge::Ptr();
        };

        auto dogecoinSecondSideFactory = wallet::MakeSecondSideFactory<DogecoinSide, dogecoin::Electrum, dogecoin::ISettingsProvider>(dogecoinBridgeCreator, *dogecoinSettingsProvider);
        swapTransactionCreator->RegisterFactory(AtomicSwapCoin::Dogecoin, dogecoinSecondSideFactory);
    }

    {
        auto dashSettingsProvider = std::make_shared<dash::SettingsProvider>(walletDB);
        dashSettingsProvider->Initialize();

        // dashSettingsProvider stored in dashBridgeCreator
        auto dashBridgeCreator = [settingsProvider = dashSettingsProvider]() -> bitcoin::IBridge::Ptr
        {
            if (settingsProvider->GetSettings().IsElectrumActivated())
                return std::make_shared<dash::Electrum>(io::Reactor::get_Current(), *settingsProvider);

            if (settingsProvider->GetSettings().IsCoreActivated())
                return std::make_shared<dash::DashCore014>(io::Reactor::get_Current(), *settingsProvider);

            return bitcoin::IBridge::Ptr();
        };

        auto dashSecondSideFactory = wallet::MakeSecondSideFactory<DashSide, dash::Electrum, dash::ISettingsProvider>(dashBridgeCreator, *dashSettingsProvider);
        swapTransactionCreator->RegisterFactory(AtomicSwapCoin::Dash, dashSecondSideFactory);
    }
}

Amount GetSwapFeeRate(IWalletDB::Ptr walletDB, AtomicSwapCoin swapCoin)
{
    switch (swapCoin)
    {
    case AtomicSwapCoin::Bitcoin:
    {
        auto btcSettingsProvider = std::make_shared<bitcoin::SettingsProvider>(walletDB);
        btcSettingsProvider->Initialize();

        auto btcSettings = btcSettingsProvider->GetSettings();
        if (!btcSettings.IsInitialized())
        {
            throw std::runtime_error("BTC settings should be initialized.");
        }

        return btcSettings.GetFeeRate();
    }
    case AtomicSwapCoin::Litecoin:
    {
        auto ltcSettingsProvider = std::make_shared<litecoin::SettingsProvider>(walletDB);
        ltcSettingsProvider->Initialize();

        auto ltcSettings = ltcSettingsProvider->GetSettings();
        if (!ltcSettings.IsInitialized())
        {
            throw std::runtime_error("LTC settings should be initialized.");
        }

        return ltcSettings.GetFeeRate();
    }
    case AtomicSwapCoin::Qtum:
    {
        auto qtumSettingsProvider = std::make_shared<qtum::SettingsProvider>(walletDB);
        qtumSettingsProvider->Initialize();

        auto qtumSettings = qtumSettingsProvider->GetSettings();
        if (!qtumSettings.IsInitialized())
        {
            throw std::runtime_error("Qtum settings should be initialized.");
        }

        return qtumSettings.GetFeeRate();
    }
    case AtomicSwapCoin::Bitcoin_Cash:
    {
        auto bchSettingsProvider = std::make_shared<bitcoin_cash::SettingsProvider>(walletDB);
        bchSettingsProvider->Initialize();

        auto bchSettings = bchSettingsProvider->GetSettings();
        if (!bchSettings.IsInitialized())
        {
            throw std::runtime_error("Bitcoin Cash settings should be initialized.");
        }

        return bchSettings.GetFeeRate();
    }
    case AtomicSwapCoin::Bitcoin_SV:
    {
        auto bsvSettingsProvider = std::make_shared<bitcoin_sv::SettingsProvider>(walletDB);
        bsvSettingsProvider->Initialize();

        auto bsvSettings = bsvSettingsProvider->GetSettings();
        if (!bsvSettings.IsInitialized())
        {
            throw std::runtime_error("Bitcoin SV settings should be initialized.");
        }

        return bsvSettings.GetFeeRate();
    }
    case AtomicSwapCoin::Dogecoin:
    {
        auto dogecoinSettingsProvider = std::make_shared<dogecoin::SettingsProvider>(walletDB);
        dogecoinSettingsProvider->Initialize();

        auto dogecoinSettings = dogecoinSettingsProvider->GetSettings();
        if (!dogecoinSettings.IsInitialized())
        {
            throw std::runtime_error("Dogecoin settings should be initialized.");
        }

        return dogecoinSettings.GetFeeRate();
    }
    case AtomicSwapCoin::Dash:
    {
        auto dashSettingsProvider = std::make_shared<dash::SettingsProvider>(walletDB);
        dashSettingsProvider->Initialize();

        auto dashSettings = dashSettingsProvider->GetSettings();
        if (!dashSettings.IsInitialized())
        {
            throw std::runtime_error("Dash settings should be initialized.");
        }

        return dashSettings.GetFeeRate();
    }
    default:
    {
        throw std::runtime_error("Unsupported coin for swap");
    }
    }
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
    default:
        throw std::runtime_error("Unsupported coin for swap");
    }
}
#endif // BEAM_ATOMIC_SWAP_SUPPORT
} // namespace beam::wallet
