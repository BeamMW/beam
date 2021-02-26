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

#include "swaps.h"

#include "wallet/core/common_utils.h"
#include "wallet/core/strings_resources.h"

#include "wallet/transactions/swaps/bridges/bitcoin/bitcoin.h"
#include "wallet/transactions/swaps/bridges/litecoin/electrum.h"
#include "wallet/transactions/swaps/bridges/qtum/electrum.h"
#include "wallet/transactions/swaps/bridges/litecoin/litecoin.h"
#include "wallet/transactions/swaps/bridges/qtum/qtum.h"
#include "wallet/transactions/swaps/bridges/bitcoin/bridge_holder.h"
#if defined(BITCOIN_CASH_SUPPORT)
#include "wallet/transactions/swaps/bridges/bitcoin_cash/bitcoin_cash.h"
#endif // BITCOIN_CASH_SUPPORT
#include "wallet/transactions/swaps/bridges/dogecoin/dogecoin.h"
#include "wallet/transactions/swaps/bridges/dash/dash.h"
#include "wallet/transactions/swaps/bridges/ethereum/ethereum.h"
#include "wallet/transactions/swaps/bridges/ethereum/bridge_holder.h"
#include "wallet/transactions/swaps/common.h"
#include "wallet/transactions/swaps/utils.h"

#include "mnemonic/mnemonic.h"
#include "utility/cli/options.h"
#include "utility/string_helpers.h"

#include "utils.h"

#include <boost/algorithm/string/trim.hpp>
#include <boost/format.hpp>
#include <boost/program_options.hpp>
#include <boost/multiprecision/cpp_dec_float.hpp>
#include <regex>

using namespace std;
using namespace beam;
using namespace beam::wallet;
using namespace ECC;

namespace beam::wallet
{
namespace
{
const char kElectrumSeparateSymbol = ' ';

Amount ReadEthSwapAmount(const po::variables_map& vm, AtomicSwapCoin swapCoin)
{
    if (vm.count(cli::ETH_SWAP_AMOUNT) == 0)
    {
        throw std::runtime_error("eth_swap_amount should be specified");
    }

    const auto strAmount = vm[cli::ETH_SWAP_AMOUNT].as<std::string>();

    try
    {
        boost::multiprecision::cpp_dec_float_50 preciseAmount(strAmount);
         
        preciseAmount *= UnitsPerCoin(swapCoin);

        return preciseAmount.convert_to<Amount>();
    }
    catch (const std::runtime_error& /*err*/)
    {
        throw std::runtime_error((boost::format("the argument ('%1%') for option '--%2%' is invalid.") % strAmount % cli::ETH_SWAP_AMOUNT).str());
    }
}

Amount ReadGasPrice(const po::variables_map& vm)
{
    if (vm.count(cli::ETH_GAS_PRICE) == 0)
    {
        throw std::runtime_error("eth_gas_price should be specified");
    }

    return vm[cli::ETH_GAS_PRICE].as<Positive<Amount>>().value;
}

std::string PrintEth(beam::Amount value, AtomicSwapCoin swapCoin)
{
    const uint64_t unitsToPrint = 1'000'000u;
    boost::multiprecision::cpp_dec_float_50 preciseAmount(value);
    auto unitsPerCoin = UnitsPerCoin(swapCoin);

    if (unitsPerCoin > unitsToPrint)
    {
        preciseAmount /= UnitsPerCoin(swapCoin) / unitsToPrint;
    }

    preciseAmount = boost::multiprecision::round(preciseAmount);

    if (unitsPerCoin < unitsToPrint)
    {
        preciseAmount /= unitsPerCoin;
    }
    else
    {
        preciseAmount /= unitsToPrint;
    }

    return preciseAmount.str() + " " + std::to_string(swapCoin);
}

template<typename Settings>
bool ParseElectrumSettings(const po::variables_map& vm, Settings& settings)
{
    if (vm.count(cli::ELECTRUM_SEED) || vm.count(cli::ELECTRUM_ADDR) ||
        vm.count(cli::GENERATE_ELECTRUM_SEED) || vm.count(cli::SELECT_SERVER_AUTOMATICALLY) ||
        vm.count(cli::ADDRESSES_TO_RECEIVE) || vm.count(cli::ADDRESSES_FOR_CHANGE))
    {
        if (!settings.IsSupportedElectrum())
        {
            throw std::runtime_error("electrum is not supported");
        }

        auto electrumSettings = settings.GetElectrumConnectionOptions();

        if (!electrumSettings.IsInitialized())
        {
            if (!vm.count(cli::ELECTRUM_SEED) && !vm.count(cli::GENERATE_ELECTRUM_SEED))
            {
                throw std::runtime_error("electrum seed should be specified");
            }

            if (!vm.count(cli::ELECTRUM_ADDR) &&
                (!vm.count(cli::SELECT_SERVER_AUTOMATICALLY) || !vm[cli::SELECT_SERVER_AUTOMATICALLY].as<bool>()))
            {
                throw std::runtime_error("electrum address should be specified");
            }
        }

        if (vm.count(cli::ELECTRUM_ADDR))
        {
            electrumSettings.m_address = vm[cli::ELECTRUM_ADDR].as<string>();
            if (!io::Address().resolve(electrumSettings.m_address.c_str()))
            {
                throw std::runtime_error("unable to resolve electrum address: " + electrumSettings.m_address);
            }
        }

        if (vm.count(cli::SELECT_SERVER_AUTOMATICALLY))
        {
            electrumSettings.m_automaticChooseAddress = vm[cli::SELECT_SERVER_AUTOMATICALLY].as<bool>();
            if (!electrumSettings.m_automaticChooseAddress && electrumSettings.m_address.empty())
            {
                throw std::runtime_error("electrum address should be specified");
            }
        }

        if (vm.count(cli::ADDRESSES_TO_RECEIVE))
        {
            electrumSettings.m_receivingAddressAmount = vm[cli::ADDRESSES_TO_RECEIVE].as<Positive<uint32_t>>().value;
        }

        if (vm.count(cli::ADDRESSES_FOR_CHANGE))
        {
            electrumSettings.m_changeAddressAmount = vm[cli::ADDRESSES_FOR_CHANGE].as<Positive<uint32_t>>().value;
        }

        if (vm.count(cli::ELECTRUM_SEED))
        {
            auto tempPhrase = vm[cli::ELECTRUM_SEED].as<string>();
            boost::algorithm::trim_if(tempPhrase, [](char ch) { return ch == kElectrumSeparateSymbol; });
            electrumSettings.m_secretWords = string_helpers::split(tempPhrase, kElectrumSeparateSymbol);

            if (!electrum::validateMnemonic(electrumSettings.m_secretWords))
            {
                if (electrum::validateMnemonic(electrumSettings.m_secretWords, true))
                {
                    throw std::runtime_error("Segwit seed phrase is not supported yet.");
                }
                throw std::runtime_error("seed is not valid");
            }
        }
        else if (vm.count(cli::GENERATE_ELECTRUM_SEED))
        {
            electrumSettings.m_secretWords = electrum::createMnemonic(getEntropy());

            // TODO roman.strilets need to check words
            auto strSeed = std::accumulate(
                std::next(electrumSettings.m_secretWords.begin()), electrumSettings.m_secretWords.end(), *electrumSettings.m_secretWords.begin(),
                [](const std::string& a, const std::string& b)
            {
                return a + kElectrumSeparateSymbol + b;
            });

            LOG_INFO() << "seed = " << strSeed;
        }

        settings.SetElectrumConnectionOptions(electrumSettings);

        return true;
    }

    return false;
}

template<typename Settings>
bool ParseSwapSettings(const po::variables_map& vm, Settings& settings)
{
    if (vm.count(cli::SWAP_WALLET_ADDR) > 0 || vm.count(cli::SWAP_WALLET_USER) > 0 || vm.count(cli::SWAP_WALLET_PASS) > 0)
    {
        auto coreSettings = settings.GetConnectionOptions();
        if (!coreSettings.IsInitialized())
        {
            if (vm.count(cli::SWAP_WALLET_USER) == 0)
            {
                throw std::runtime_error(kErrorSwapWalletUserNameUnspecified);
            }

            if (vm.count(cli::SWAP_WALLET_ADDR) == 0)
            {
                throw std::runtime_error(kErrorSwapWalletAddrUnspecified);
            }

            if (vm.count(cli::SWAP_WALLET_PASS) == 0)
            {
                throw std::runtime_error(kErrorSwapWalletPwdNotProvided);
            }
        }

        if (vm.count(cli::SWAP_WALLET_USER))
        {
            coreSettings.m_userName = vm[cli::SWAP_WALLET_USER].as<string>();
        }

        if (vm.count(cli::SWAP_WALLET_ADDR))
        {
            string nodeUri = vm[cli::SWAP_WALLET_ADDR].as<string>();
            if (!coreSettings.m_address.resolve(nodeUri.c_str()))
            {
                throw std::runtime_error((boost::format(kErrorSwapWalletAddrNotResolved) % nodeUri).str());
            }
        }

        // TODO roman.strilets: use SecString instead of std::string
        if (vm.count(cli::SWAP_WALLET_PASS))
        {
            coreSettings.m_pass = vm[cli::SWAP_WALLET_PASS].as<string>();
        }

        settings.SetConnectionOptions(coreSettings);

        return true;
    }

    return false;
}

template<typename SettingsProvider, typename Settings, typename CoreSettings, typename ElectrumSettings>
int SetSwapSettings(const po::variables_map& vm, const IWalletDB::Ptr& walletDB)
{
    SettingsProvider settingsProvider{ walletDB };
    settingsProvider.Initialize();

    if (vm.count(cli::ALTCOIN_SETTINGS_RESET))
    {
        auto connectionType = bitcoin::from_string(vm[cli::ALTCOIN_SETTINGS_RESET].as<string>());

        if (connectionType)
        {
            if (*connectionType == bitcoin::Settings::ConnectionType::Core)
            {
                auto settings = settingsProvider.GetSettings();

                if (!settings.GetElectrumConnectionOptions().IsInitialized())
                {
                    settings = Settings{};
                }
                else
                {
                    settings.SetConnectionOptions(CoreSettings{});

                    if (settings.GetCurrentConnectionType() == bitcoin::Settings::ConnectionType::Core)
                    {
                        settings.ChangeConnectionType(bitcoin::Settings::ConnectionType::Electrum);
                    }
                }

                settingsProvider.SetSettings(settings);
                return 0;
            }

            if (*connectionType == bitcoin::Settings::ConnectionType::Electrum)
            {
                auto settings = settingsProvider.GetSettings();

                if (!settings.GetConnectionOptions().IsInitialized())
                {
                    settings = Settings{};
                }
                else
                {
                    settings.SetElectrumConnectionOptions(ElectrumSettings{});

                    if (settings.GetCurrentConnectionType() == bitcoin::Settings::ConnectionType::Electrum)
                    {
                        settings.ChangeConnectionType(bitcoin::Settings::ConnectionType::Core);
                    }
                }

                settingsProvider.SetSettings(settings);
                return 0;
            }
        }

        LOG_ERROR() << "unknown parameter";
        return -1;
    }

    auto settings = settingsProvider.GetSettings();
    bool isChanged = false;

    isChanged |= ParseSwapSettings(vm, settings);
    isChanged |= ParseElectrumSettings(vm, settings);

    if (!isChanged && !settings.IsInitialized())
    {
        LOG_INFO() << "settings should be specified.";
        return -1;
    }

    if (vm.count(cli::ACTIVE_CONNECTION))
    {
        auto typeConnection = bitcoin::from_string(vm[cli::ACTIVE_CONNECTION].as<string>());
        if (!typeConnection)
        {
            throw std::runtime_error("active_connection is wrong");
        }

        if ((*typeConnection == bitcoin::Settings::ConnectionType::Core && !settings.GetConnectionOptions().IsInitialized())
            || (*typeConnection == bitcoin::Settings::ConnectionType::Electrum && !settings.GetElectrumConnectionOptions().IsInitialized()))
        {
            throw std::runtime_error(vm[cli::ACTIVE_CONNECTION].as<string>() + " is not initialized");
        }

        settings.ChangeConnectionType(*typeConnection);
        isChanged = true;
    }

    if (isChanged)
    {
        settingsProvider.SetSettings(settings);
    }
    return 0;
}

template<typename SettingsProvider>
void ShowSwapSettings(const IWalletDB::Ptr& walletDB, AtomicSwapCoin swapCoin)
{
    SettingsProvider settingsProvider{ walletDB };

    settingsProvider.Initialize();
    auto settings = settingsProvider.GetSettings();

    if (settings.IsInitialized())
    {
        ostringstream stream;
        stream << "\n" << GetCoinName(swapCoin) << " settings" << '\n';
        if (settings.GetConnectionOptions().IsInitialized())
        {
            stream << "RPC user: " << settings.GetConnectionOptions().m_userName << '\n'
                << "RPC node: " << settings.GetConnectionOptions().m_address.str() << '\n';
        }

        if (settings.GetElectrumConnectionOptions().IsInitialized())
        {
            if (settings.GetElectrumConnectionOptions().m_automaticChooseAddress)
            {
                stream << "Automatic node selection mode is turned ON.\n";
            }
            else
            {
                stream << "Electrum node: " << settings.GetElectrumConnectionOptions().m_address << '\n';
            }

            stream << "Amount of receiving addresses: " << settings.GetElectrumConnectionOptions().m_receivingAddressAmount << '\n';
            stream << "Amount of change addresses: " << settings.GetElectrumConnectionOptions().m_changeAddressAmount << '\n';
        }
        stream << "Active connection: " << bitcoin::to_string(settings.GetCurrentConnectionType()) << '\n';

        LOG_INFO() << stream.str();
        return;
    }

    LOG_INFO() << GetCoinName(swapCoin) << " settings are not initialized.";
}

int SetEthSettings(const po::variables_map& vm, const IWalletDB::Ptr& walletDB, wallet::AtomicSwapCoin swapCoin)
{
    ethereum::SettingsProvider settingsProvider{ walletDB };
    settingsProvider.Initialize();

    if (vm.count(cli::ALTCOIN_SETTINGS_RESET))
    {
        settingsProvider.SetSettings(ethereum::Settings{});
        return 0;
    }

    auto settings = settingsProvider.GetSettings();
    bool isChanged = false;

    if (!settings.IsInitialized())
    {
        if (!vm.count(cli::ETHEREUM_SEED))
        {
            throw std::runtime_error("ethereum seed should be specified");
        }

        if (!vm.count(cli::INFURA_PROJECT_ID))
        {
            throw std::runtime_error("infura project id should be specified");
        }
    }

    if (vm.count(cli::INFURA_PROJECT_ID))
    {
        settings.m_projectID = vm[cli::INFURA_PROJECT_ID].as<string>();
        isChanged = true;
    }

    if (vm.count(cli::ETHEREUM_SEED))
    {
        auto tempPhrase = vm[cli::ETHEREUM_SEED].as<string>();
        boost::algorithm::trim_if(tempPhrase, [](char ch) { return ch == kElectrumSeparateSymbol; });
        settings.m_secretWords = string_helpers::split(tempPhrase, kElectrumSeparateSymbol);

        if (!isValidMnemonic(settings.m_secretWords))
        {
            throw std::runtime_error("seed is not valid");
        }

        isChanged = true;
    }

    if (!isChanged && !settings.IsInitialized())
    {
        LOG_INFO() << "settings should be specified.";
        return -1;
    }

    if (vm.count(cli::ACCOUNT_INDEX))
    {
        settings.m_accountIndex = vm[cli::ACCOUNT_INDEX].as<Nonnegative<uint32_t>>().value;
        isChanged = true;
    }

    if (vm.count(cli::SHOULD_CONNECT))
    {
        settings.m_shouldConnect = vm[cli::SHOULD_CONNECT].as<bool>();
        isChanged = true;
    }

    if (isChanged)
    {
        settingsProvider.SetSettings(settings);
    }
    return 0;
}

void ShowEthSettings(const IWalletDB::Ptr& walletDB)
{
    ethereum::SettingsProvider settingsProvider{ walletDB };
    settingsProvider.Initialize();

    auto settings = settingsProvider.GetSettings();

    if (settings.IsInitialized())
    {
        ostringstream stream;
        stream << "\n" << GetCoinName(AtomicSwapCoin::Ethereum) << " settings" << '\n';

        stream << "Infura project ID: " << settings.m_projectID << '\n';
        stream << "Account index: " << settings.m_accountIndex << '\n';
        stream << "Should connect: " << settings.m_shouldConnect << '\n';

        LOG_INFO() << stream.str();
        return;
    }

    LOG_INFO() << GetCoinName(AtomicSwapCoin::Ethereum) << " settings are not initialized.";
}

template<typename SettingsProvider, typename Bridge, typename BridgeHolder>
void RequestToSpecificBridge(IWalletDB::Ptr walletDB, AtomicSwapCoin swapCoin, std::function<void(typename Bridge::Ptr)> callback = nullptr)
{
    SettingsProvider settingsProvider{ walletDB };

    settingsProvider.Initialize();

    auto settings = settingsProvider.GetSettings();

    if (settings.IsActivated())
    {
        if (callback)
        {
            BridgeHolder bridgeHolder;
            callback(bridgeHolder.Get(io::Reactor::get_Current(), settingsProvider));
        }
        return;
    }

    throw std::runtime_error(GetCoinName(swapCoin) + " settings are not initialized.");
}

void RequestToBridge(const IWalletDB::Ptr& walletDB, AtomicSwapCoin swapCoin, std::function<void(beam::bitcoin::IBridge::Ptr)> callback = nullptr)
{
    switch (swapCoin)
    {
    case AtomicSwapCoin::Bitcoin:
    {
        RequestToSpecificBridge
            <bitcoin::SettingsProvider, 
            bitcoin::IBridge, 
            bitcoin::BridgeHolder<bitcoin::Electrum, bitcoin::BitcoinCore017>>
                (walletDB, swapCoin, callback);
        break;
    }
    case AtomicSwapCoin::Litecoin:
    {
        RequestToSpecificBridge
            <litecoin::SettingsProvider, 
            bitcoin::IBridge, 
            bitcoin::BridgeHolder<litecoin::Electrum, litecoin::LitecoinCore017>>
                (walletDB, swapCoin, callback);
        break;
    }
    case AtomicSwapCoin::Qtum:
    {
        RequestToSpecificBridge
            <qtum::SettingsProvider, 
            bitcoin::IBridge,
            bitcoin::BridgeHolder<qtum::Electrum, qtum::QtumCore017>>
                (walletDB, swapCoin, callback);
        break;
    }
    case AtomicSwapCoin::Dogecoin:
    {
        RequestToSpecificBridge
            <dogecoin::SettingsProvider, 
            bitcoin::IBridge,
            bitcoin::BridgeHolder<dogecoin::Electrum, dogecoin::DogecoinCore014>>
                (walletDB, swapCoin, callback);
        break;
    }
#if defined(BITCOIN_CASH_SUPPORT)
    case beam::wallet::AtomicSwapCoin::Bitcoin_Cash:
    {
        RequestToSpecificBridge
            <bitcoin_cash::SettingsProvider, 
            bitcoin::IBridge,
            bitcoin::BridgeHolder<bitcoin_cash::Electrum, bitcoin_cash::BitcoinCashCore>>
                (walletDB, swapCoin, callback);
        break;
    }
#endif // BITCOIN_CASH_SUPPORT
    case AtomicSwapCoin::Dash:
    {
        RequestToSpecificBridge
            <dash::SettingsProvider, 
            bitcoin::IBridge,
            bitcoin::BridgeHolder<dash::Electrum, dash::DashCore014>>
                (walletDB, swapCoin, callback);
        break;
    }
    default:
    {
        throw std::runtime_error("Unsupported coin for swap");
    }
    }
}

void RequestToEthBridge(const IWalletDB::Ptr& walletDB, std::function<void(beam::ethereum::IBridge::Ptr)> callback = nullptr)
{
    RequestToSpecificBridge
        <ethereum::SettingsProvider, ethereum::IBridge, ethereum::BridgeHolder>
            (walletDB, AtomicSwapCoin::Ethereum, callback);
}

template<typename SettingsProvider>
Amount GetMinSwapFeeRate(IWalletDB::Ptr walletDB)
{
    SettingsProvider settingsProvider{ walletDB };

    settingsProvider.Initialize();

    auto settings = settingsProvider.GetSettings();
    return settings.GetMinFeeRate();
}

template<typename SettingsProvider>
Amount GetMaxSwapFeeRate(IWalletDB::Ptr walletDB)
{
    SettingsProvider settingsProvider{ walletDB };

    settingsProvider.Initialize();

    auto settings = settingsProvider.GetSettings();
    return settings.GetMaxFeeRate();
}
} // namespace

bool HasActiveSwapTx(const IWalletDB::Ptr& walletDB, AtomicSwapCoin swapCoin)
{
    auto txHistory = walletDB->getTxHistory(wallet::TxType::AtomicSwap);

    for (const auto& tx : txHistory)
    {
        if (tx.m_status != TxStatus::Canceled && tx.m_status != TxStatus::Completed && tx.m_status != TxStatus::Failed)
        {
            AtomicSwapCoin txSwapCoin = AtomicSwapCoin::Unknown;
            storage::getTxParameter(*walletDB, tx.m_txId, wallet::kDefaultSubTxID, wallet::TxParameterID::AtomicSwapCoin, txSwapCoin);
            if (txSwapCoin == swapCoin) return true;
        }
    }

    return false;
}


Amount EstimateSwapFeerate(AtomicSwapCoin swapCoin, IWalletDB::Ptr walletDB)
{
    Amount result = 0;

    if (ethereum::IsEthereumBased(swapCoin))
    {
        auto callback = [&result](ethereum::IBridge::Ptr bridge)
        {
            bridge->getGasPrice([&result](const ethereum::IBridge::Error& error, Amount gasPrice)
            {
                // convert from wei to gwei
                result = gasPrice / ethereum::GetCoinUnitsMultiplier(AtomicSwapCoin::Ethereum);
                io::Reactor::get_Current().stop();
            });

            io::Reactor::get_Current().run();
        };

        RequestToEthBridge(walletDB, callback);
    }
    else
    {
        auto callback = [&result](beam::bitcoin::IBridge::Ptr bridge)
        {
            bridge->estimateFee(1, [&result](const bitcoin::IBridge::Error& error, Amount feeRate)
            {
                // feeRate = 0 if it has not connection
                result = feeRate;
                io::Reactor::get_Current().stop();
            });

            io::Reactor::get_Current().run();
        };

        RequestToBridge(walletDB, swapCoin, callback);
    }

    return result;
}

Amount GetMinSwapFeeRate(AtomicSwapCoin swapCoin, IWalletDB::Ptr walletDB)
{
    switch (swapCoin)
    {
    case AtomicSwapCoin::Bitcoin:
    {
        return GetMinSwapFeeRate<bitcoin::SettingsProvider>(walletDB);
    }
    case AtomicSwapCoin::Litecoin:
    {
        return GetMinSwapFeeRate<litecoin::SettingsProvider>(walletDB);
    }
    case AtomicSwapCoin::Qtum:
    {
        return GetMinSwapFeeRate<qtum::SettingsProvider>(walletDB);
    }
    case AtomicSwapCoin::Dogecoin:
    {
        return GetMinSwapFeeRate<dogecoin::SettingsProvider>(walletDB);
    }
#if defined(BITCOIN_CASH_SUPPORT)
    case beam::wallet::AtomicSwapCoin::Bitcoin_Cash:
    {
        return GetMinSwapFeeRate<bitcoin_cash::SettingsProvider>(walletDB);
    }
#endif // BITCOIN_CASH_SUPPORT
    case AtomicSwapCoin::Dash:
    {
        return GetMinSwapFeeRate<dash::SettingsProvider>(walletDB);
    }
    case AtomicSwapCoin::Ethereum:
    case AtomicSwapCoin::Dai:
    case AtomicSwapCoin::Usdt:
    case AtomicSwapCoin::WBTC:
    {
        return GetMinSwapFeeRate<ethereum::SettingsProvider>(walletDB);
    }
    default:
    {
        throw std::runtime_error("Unsupported coin for swap");
        return 0;
    }
    }
}

Amount GetMaxSwapFeeRate(AtomicSwapCoin swapCoin, IWalletDB::Ptr walletDB)
{
    switch (swapCoin)
    {
    case AtomicSwapCoin::Bitcoin:
    {
        return GetMaxSwapFeeRate<bitcoin::SettingsProvider>(walletDB);
    }
    case AtomicSwapCoin::Litecoin:
    {
        return GetMaxSwapFeeRate<litecoin::SettingsProvider>(walletDB);
    }
    case AtomicSwapCoin::Qtum:
    {
        return GetMaxSwapFeeRate<qtum::SettingsProvider>(walletDB);
    }
    case AtomicSwapCoin::Dogecoin:
    {
        return GetMaxSwapFeeRate<dogecoin::SettingsProvider>(walletDB);
    }
#if defined(BITCOIN_CASH_SUPPORT)
    case beam::wallet::AtomicSwapCoin::Bitcoin_Cash:
    {
        return GetMaxSwapFeeRate<bitcoin_cash::SettingsProvider>(walletDB);
    }
#endif // BITCOIN_CASH_SUPPORT
    case AtomicSwapCoin::Dash:
    {
        return GetMaxSwapFeeRate<dash::SettingsProvider>(walletDB);
    }
    case AtomicSwapCoin::Ethereum:
    case AtomicSwapCoin::Dai:
    case AtomicSwapCoin::Usdt:
    case AtomicSwapCoin::WBTC:
    {
        return GetMaxSwapFeeRate<ethereum::SettingsProvider>(walletDB);
    }
    default:
    {
        throw std::runtime_error("Unsupported coin for swap");
        return 0;
    }
    }
}

Amount GetBalance(AtomicSwapCoin swapCoin, IWalletDB::Ptr walletDB)
{
    Amount result = 0;

    if (ethereum::IsEthereumBased(swapCoin))
    {
        auto callback = [&result, swapCoin, walletDB](beam::ethereum::IBridge::Ptr bridge)
        {
            auto balanceCallback = [&result, swapCoin](const ethereum::IBridge::Error& error, const std::string& balance)
            {
                if (error.m_type != ethereum::IBridge::ErrorType::None)
                {
                    throw std::runtime_error(error.m_message);
                }

                boost::multiprecision::uint256_t tmp(balance);

                tmp /= ethereum::GetCoinUnitsMultiplier(swapCoin);

                result = tmp.convert_to<Amount>();
                io::Reactor::get_Current().stop();
            };

            if (swapCoin == AtomicSwapCoin::Ethereum)
            {
                bridge->getBalance(balanceCallback);
            }
            else
            {
                ethereum::SettingsProvider settingsProvider(walletDB);

                settingsProvider.Initialize();

                const auto tokenContractAddressStr = settingsProvider.GetSettings().GetTokenContractAddress(swapCoin);
                if (tokenContractAddressStr.empty())
                {
                    throw std::runtime_error("Token contract is absent");
                    return;
                }

                bridge->getTokenBalance(tokenContractAddressStr, balanceCallback);
            }

            io::Reactor::get_Current().run();
        };

        RequestToEthBridge(walletDB, callback);
    }
    else
    {
        auto callback = [&result](beam::bitcoin::IBridge::Ptr bridge)
        {
            bridge->getDetailedBalance([&result](const bitcoin::IBridge::Error& error, Amount balance, Amount, Amount)
            {
                if (error.m_type != bitcoin::IBridge::ErrorType::None)
                {
                    throw std::runtime_error(error.m_message);
                }

                result = balance;
                io::Reactor::get_Current().stop();
            });

            io::Reactor::get_Current().run();
        };

        RequestToBridge(walletDB, swapCoin, callback);
    }

    return result;
}

boost::optional<TxID> InitSwap(const po::variables_map& vm, const IWalletDB::Ptr& walletDB, Wallet& wallet, bool checkFee)
{
    wallet::AtomicSwapCoin swapCoin = wallet::AtomicSwapCoin::Bitcoin;

    if (vm.count(cli::SWAP_COIN) > 0)
    {
        swapCoin = wallet::from_string(vm[cli::SWAP_COIN].as<string>());
    }

    Amount swapAmount = 0;
    Amount swapFeeRate = 0;

    if (ethereum::IsEthereumBased(swapCoin))
    {
        if (vm.count(cli::ETH_SWAP_AMOUNT) == 0)
        {
            throw std::runtime_error("eth_swap_amount should be specified");
        }

        if (vm.count(cli::ETH_GAS_PRICE) == 0)
        {
            throw std::runtime_error("eth_gas_price should be specified");
        }

        swapAmount = ReadEthSwapAmount(vm, swapCoin);
        swapFeeRate = ReadGasPrice(vm);

        if (!swapAmount)
        {
            throw std::runtime_error("eth_swap_amount is too small.");
        }

        // TODO need to unite with InitSwap
        Amount estimatedFeeRate = EstimateSwapFeerate(swapCoin, walletDB);

        if (estimatedFeeRate > 0 && estimatedFeeRate > swapFeeRate)
        {
            throw std::runtime_error("eth_gas_price must be greater than the etimate gas price.");
        }

        Amount minFeeRate = GetMinSwapFeeRate(swapCoin, walletDB);

        if (minFeeRate > 0 && minFeeRate > swapFeeRate)
        {
            throw std::runtime_error("eth_gas_price must be greater than the minimum gas price.");
        }

        Amount maxFeeRate = GetMaxSwapFeeRate(swapCoin, walletDB);

        if (maxFeeRate > 0 && maxFeeRate < swapFeeRate)
        {
            throw std::runtime_error("eth_gas_price must be less than the maximum gas price.");
        }
    }
    else
    {
        if (vm.count(cli::SWAP_AMOUNT) == 0)
        {
            throw std::runtime_error(kErrorSwapAmountMissing);
        }

        swapAmount = vm[cli::SWAP_AMOUNT].as<Positive<Amount>>().value;

        if (vm.count(cli::SWAP_FEERATE) == 0)
        {
            throw std::runtime_error("swap_feerate should be specified");
        }

        Amount estimatedFeeRate = EstimateSwapFeerate(swapCoin, walletDB);

        swapFeeRate = vm[cli::SWAP_FEERATE].as<Positive<Amount>>().value;

        if (estimatedFeeRate > 0 && estimatedFeeRate > swapFeeRate)
        {
            throw std::runtime_error("swap_feerate must be greater than the recommended fee rate.");
        }

        Amount minFeeRate = GetMinSwapFeeRate(swapCoin, walletDB);

        if (minFeeRate > 0 && minFeeRate > swapFeeRate)
        {
            throw std::runtime_error("swap_feerate must be greater than the minimum fee rate.");
        }

        Amount maxFeeRate = GetMaxSwapFeeRate(swapCoin, walletDB);

        if (maxFeeRate > 0 && maxFeeRate < swapFeeRate)
        {
            throw std::runtime_error("swap_feerate must be less than the maximum fee rate.");
        }

        bool isSwapAmountValid =
            IsLockTxAmountValid(swapCoin, swapAmount, swapFeeRate);
        if (!isSwapAmountValid)
            throw std::runtime_error("The swap amount must be greater than the redemption fee.");
    }

    bool isBeamSide = (vm.count(cli::SWAP_BEAM_SIDE) != 0);

    Asset::ID assetId = Asset::s_InvalidID;
    Amount amount = 0;
    Amount fee = 0;
    WalletID receiverWalletID(Zero);

    if (!LoadBaseParamsForTX(vm, assetId, amount, fee, receiverWalletID, checkFee, true))
    {
        return boost::none;
    }

    if (assetId)
    {
        throw std::runtime_error(kErrorCantSwapAsset);
    }

    if (amount <= kMinFeeInGroth)
    {
        throw std::runtime_error(kErrorSwapAmountTooLow);
    }

    Amount feeForShieldedInputs = 0;
    if (isBeamSide && !CheckFeeForShieldedInputs(amount, fee, Asset::s_BeamID, walletDB, false, feeForShieldedInputs))
        throw std::runtime_error("Fee to low");

    WalletAddress senderAddress = GenerateNewAddress(walletDB, "");

    // TODO:SWAP use async callbacks or IWalletObserver?

    Height minHeight = walletDB->getCurrentHeight();
    auto swapTxParameters = CreateSwapTransactionParameters();

    FillSwapTxParams(&swapTxParameters,
                     senderAddress.m_walletID,
                     minHeight,
                     amount,
                     !!feeForShieldedInputs ? fee - feeForShieldedInputs : fee,
                     swapCoin,
                     swapAmount,
                     swapFeeRate,
                     isBeamSide);

    boost::optional<TxID> currentTxID = wallet.StartTransaction(swapTxParameters);

    // print swap tx token
    {
        const auto& mirroredTxParams = MirrorSwapTxParams(swapTxParameters);
        const auto& readyForTokenizeTxParams =
            PrepareSwapTxParamsForTokenization(mirroredTxParams);
        auto swapTxToken = std::to_string(readyForTokenizeTxParams);
        LOG_INFO() << "Swap token: " << swapTxToken;
    }
    return currentTxID;
}

boost::optional<TxID> AcceptSwap(const po::variables_map& vm, const IWalletDB::Ptr& walletDB, Wallet& wallet, bool checkFee)
{
    if (vm.count(cli::SWAP_TOKEN) == 0)
    {
        throw std::runtime_error("swap transaction token should be specified");
    }

    auto swapTxToken = vm[cli::SWAP_TOKEN].as<std::string>();
    auto swapTxParameters = beam::wallet::ParseParameters(swapTxToken);

    // validate TxType and parameters
    auto transactionType = swapTxParameters->GetParameter<TxType>(TxParameterID::TransactionType);
    auto isBeamSide = swapTxParameters->GetParameter<bool>(TxParameterID::AtomicSwapIsBeamSide);
    auto swapCoin = swapTxParameters->GetParameter<AtomicSwapCoin>(TxParameterID::AtomicSwapCoin);
    auto beamAmount = swapTxParameters->GetParameter<Amount>(TxParameterID::Amount);
    auto swapAmount = swapTxParameters->GetParameter<Amount>(TxParameterID::AtomicSwapAmount);
    auto peerID = swapTxParameters->GetParameter<WalletID>(TxParameterID::PeerID);
    auto peerResponseTime = swapTxParameters->GetParameter<Height>(TxParameterID::PeerResponseTime);
    auto createTime = swapTxParameters->GetParameter<Height>(TxParameterID::CreateTime);
    auto minHeight = swapTxParameters->GetParameter<Height>(TxParameterID::MinHeight);

    bool isValidToken = isBeamSide && swapCoin && beamAmount && swapAmount && peerID && peerResponseTime && createTime && minHeight;

    if (!transactionType || *transactionType != TxType::AtomicSwap || !isValidToken)
    {
        throw std::runtime_error("swap transaction token is invalid.");
    }

    Amount swapFeeRate = 0;

    if (ethereum::IsEthereumBased(*swapCoin))
    {
        if (vm.count(cli::ETH_GAS_PRICE) == 0)
        {
            throw std::runtime_error("eth_gas_price should be specified");
        }

        swapFeeRate = ReadGasPrice(vm);

        // TODO need to unite with InitSwap
        Amount estimatedFeeRate = EstimateSwapFeerate(*swapCoin, walletDB);

        if (estimatedFeeRate > 0 && estimatedFeeRate > swapFeeRate)
        {
            throw std::runtime_error("eth_gas_price must be greater than the etimate gas price.");
        }

        Amount minFeeRate = GetMinSwapFeeRate(*swapCoin, walletDB);

        if (minFeeRate > 0 && minFeeRate > swapFeeRate)
        {
            throw std::runtime_error("eth_gas_price must be greater than the minimum gas price.");
        }

        Amount maxFeeRate = GetMaxSwapFeeRate(*swapCoin, walletDB);

        if (maxFeeRate > 0 && maxFeeRate < swapFeeRate)
        {
            throw std::runtime_error("eth_gas_price must be less than the maximum gas price.");
        }
    }
    else
    {
        if (vm.count(cli::SWAP_FEERATE) == 0)
        {
            throw std::runtime_error("swap_feerate should be specified");
        }

        // TODO need to unite with InitSwap
        Amount estimatedFeeRate = EstimateSwapFeerate(*swapCoin, walletDB);
        
        swapFeeRate = vm[cli::SWAP_FEERATE].as<Positive<Amount>>().value;

        if (estimatedFeeRate > 0 && estimatedFeeRate > swapFeeRate)
        {
            throw std::runtime_error("swap_feerate must be greater than the etimate fee rate.");
        }

        Amount minFeeRate = GetMinSwapFeeRate(*swapCoin, walletDB);

        if (minFeeRate > 0 && minFeeRate > swapFeeRate)
        {
            throw std::runtime_error("swap_feerate must be greater than the minimum fee rate.");
        }

        Amount maxFeeRate = GetMaxSwapFeeRate(*swapCoin, walletDB);

        if (maxFeeRate > 0 && maxFeeRate < swapFeeRate)
        {
            throw std::runtime_error("swap_feerate must be less than the maximum fee rate.");
        }

        RequestToBridge(walletDB, *swapCoin);

        if (!IsLockTxAmountValid(*swapCoin, *swapAmount, swapFeeRate))
        {
            throw std::runtime_error("The swap amount must be greater than the redemption fee.");
        }
    }

    Amount fee = 0;
    Amount feeForShieldedInputs = 0;

    ReadFee(vm, fee, checkFee);    
    if (*isBeamSide && !CheckFeeForShieldedInputs(*beamAmount, fee, Asset::s_BeamID, walletDB, false, feeForShieldedInputs))
        throw std::runtime_error("Fee to low");

    fee = !!feeForShieldedInputs ? fee - feeForShieldedInputs : fee;

    ProcessLibraryVersion(*swapTxParameters);

    // display swap details to user
    cout << " Swap conditions: " << "\n"
        << " Beam side:    " << *isBeamSide << "\n"
        << " Swap coin:    " << to_string(*swapCoin) << "\n"
        << " Beam amount:  " << PrintableAmount(*beamAmount) << "\n"
        << " Swap amount:  " << (ethereum::IsEthereumBased(*swapCoin) ? PrintEth(*swapAmount, *swapCoin): std::to_string(*swapAmount)) << "\n"
        << " Peer ID:      " << to_string(*peerID) << "\n"
        << " Fee:          " << PrintableAmount(fee) << "\n" << endl;
    
    // get accepting
    // TODO: Refactor
    bool isAccepted = false;
    while (true)
    {
        std::string result;
        cout << "Do you agree to these conditions? (y/n): " << endl;
        cin >> result;

        if (result == "y" || result == "n")
        {
            isAccepted = (result == "y");
            break;
        }
    }

    if (!isAccepted)
    {
        LOG_INFO() << "Swap rejected!";
        return boost::none;
    }

    // on accepting
    WalletAddress senderAddress = GenerateNewAddress(walletDB, "");

    swapTxParameters->SetParameter(TxParameterID::MyID, senderAddress.m_walletID);
    FillSwapFee(&(*swapTxParameters), fee, swapFeeRate, *isBeamSide);

    return wallet.StartTransaction(*swapTxParameters);
}

int SetSwapSettings(const po::variables_map& vm, const IWalletDB::Ptr& walletDB, AtomicSwapCoin swapCoin)
{
    switch (swapCoin)
    {
    case AtomicSwapCoin::Bitcoin:
    {
        return SetSwapSettings<bitcoin::SettingsProvider, bitcoin::Settings, bitcoin::BitcoinCoreSettings, bitcoin::ElectrumSettings>
            (vm, walletDB);
    }
    case AtomicSwapCoin::Litecoin:
    {
        return SetSwapSettings<litecoin::SettingsProvider, litecoin::Settings, litecoin::LitecoinCoreSettings, litecoin::ElectrumSettings>
            (vm, walletDB);
    }
    case AtomicSwapCoin::Qtum:
    {
        return SetSwapSettings<qtum::SettingsProvider, qtum::Settings, qtum::QtumCoreSettings, qtum::ElectrumSettings>
            (vm, walletDB);
    }
#if defined(BITCOIN_CASH_SUPPORT)
    case beam::wallet::AtomicSwapCoin::Bitcoin_Cash:
    {
        return SetSwapSettings<bitcoin_cash::SettingsProvider, bitcoin_cash::Settings, bitcoin_cash::CoreSettings, bitcoin_cash::ElectrumSettings>
            (vm, walletDB);
    }
#endif // BITCOIN_CASH_SUPPORT
    case AtomicSwapCoin::Dogecoin:
    {
        return SetSwapSettings<dogecoin::SettingsProvider, dogecoin::Settings, dogecoin::DogecoinCoreSettings, dogecoin::ElectrumSettings>
            (vm, walletDB);
    }
    case AtomicSwapCoin::Dash:
    {
        return SetSwapSettings<dash::SettingsProvider, dash::Settings, dash::DashCoreSettings, dash::ElectrumSettings>
            (vm, walletDB);
    }
    case AtomicSwapCoin::Ethereum:
    case AtomicSwapCoin::Dai:
    case AtomicSwapCoin::Usdt:
    case AtomicSwapCoin::WBTC:
    {
        return SetEthSettings(vm, walletDB, swapCoin);
    }
    default:
    {
        throw std::runtime_error("Unsupported coin for swap");
    }
    }
}

void ShowSwapSettings(const po::variables_map& vm, const IWalletDB::Ptr& walletDB, AtomicSwapCoin swapCoin)
{
    switch(swapCoin)
    {
    case AtomicSwapCoin::Bitcoin:
    {
        ShowSwapSettings<bitcoin::SettingsProvider>(walletDB, swapCoin);
        break;
    }
    case AtomicSwapCoin::Litecoin:
    {
        ShowSwapSettings<litecoin::SettingsProvider>(walletDB, swapCoin);
        break;
    }
    case AtomicSwapCoin::Qtum:
    {
        ShowSwapSettings<qtum::SettingsProvider>(walletDB, swapCoin);
        break;
    }
#if defined(BITCOIN_CASH_SUPPORT)
    case beam::wallet::AtomicSwapCoin::Bitcoin_Cash:
    {
        ShowSwapSettings<bitcoin_cash::SettingsProvider>(walletDB, swapCoin);
        break;
    }
#endif // BITCOIN_CASH_SUPPORT
    case AtomicSwapCoin::Dogecoin:
    {
        ShowSwapSettings<dogecoin::SettingsProvider>(walletDB, swapCoin);
        break;
    }
    case AtomicSwapCoin::Dash:
    {
        ShowSwapSettings<dash::SettingsProvider>(walletDB, swapCoin);
        break;
    }
    case AtomicSwapCoin::Ethereum:
    case AtomicSwapCoin::Dai:
    case AtomicSwapCoin::Usdt:
    case AtomicSwapCoin::WBTC:
    {
        ShowEthSettings(walletDB);
        break;
    }
    default:
    {
        throw std::runtime_error("Unsupported coin for swap");
    }
    }
}
} // namespace beam::bitcoin