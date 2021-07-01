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

#include "utils.h"

#include "wallet/core/common_utils.h"
#include "wallet/core/strings_resources.h"
#include "wallet/core/wallet.h"

#include <boost/format.hpp>
#include <boost/serialization/nvp.hpp>
#include <boost/multiprecision/cpp_dec_float.hpp>

using namespace std;
using namespace beam;
using namespace beam::wallet;
using namespace ECC;

namespace beam::wallet
{
    bool ReadAmount(const po::variables_map& vm, Amount& amount, const Amount& limit, Asset::ID assetId)
    {
        if (vm.count(cli::AMOUNT) == 0)
        {
            LOG_ERROR() << kErrorAmountMissing;
            return false;
        }

        const auto strAmount = vm[cli::AMOUNT].as<std::string>();

        try
        {
            boost::multiprecision::cpp_dec_float_50 preciseAmount(strAmount.c_str());
            preciseAmount *= Rules::Coin;

            if (preciseAmount == 0)
            {
                LOG_ERROR() << kErrorZeroAmount;
                return false;
            }

            if (preciseAmount < 0)
            {
                LOG_ERROR() << (boost::format(kErrorNegativeAmount) % strAmount).str();
                return false;
            }

            if (preciseAmount > limit)
            {
                std::stringstream ssLimit;
                ssLimit << PrintableAmount(limit, false, assetId);
                LOG_ERROR() << (boost::format(kErrorTooBigAmount) % strAmount % ssLimit.str()).str();
                return false;
            }

            amount = preciseAmount.convert_to<Amount>();
        }
        catch (const std::runtime_error& err)
        {
            LOG_ERROR() << "the argument ('" << strAmount << "') for option '--amount' is invalid.";
            LOG_ERROR() << err.what();
            return false;
        }

        return true;
    }

    Amount get_MinFee(const Wallet& wallet, bool hasShieldedOutputs)
    {
        auto& fs = Transaction::FeeSettings::get(wallet.get_TipHeight());
        return hasShieldedOutputs ? fs.get_DefaultShieldedOut() : fs.get_DefaultStd();
    }

    bool ReadFee(const po::variables_map& vm, Amount& fee, const Wallet& wallet, bool checkFee, bool hasShieldedOutputs /*= false*/)
    {
        if (auto it = vm.find(cli::FEE); it != vm.end())
        {
            fee = it->second.as<Positive<Amount>>().value;
            auto minFee = get_MinFee(wallet, hasShieldedOutputs);
            if (checkFee && (fee < minFee))
            {
                LOG_ERROR() << (boost::format(kErrorFeeToLow) % minFee).str();
                return false;
            }
        }
        else
        {
            fee = get_MinFee(wallet, hasShieldedOutputs);
        }

        return true;
    }

    bool LoadReceiverParams(const po::variables_map& vm, TxParameters& params)
    {
        if (vm.find(cli::RECEIVER_ADDR) == vm.end())
        {
            LOG_ERROR() << kErrorReceiverAddrMissing;
            return false;
        }

        auto addressOrToken = vm[cli::RECEIVER_ADDR].as<string>();
        auto receiverParams = ParseParameters(addressOrToken);

        if (!receiverParams)
        {
            LOG_ERROR() << kErrorReceiverAddrMissing;
            return false;
        }

        auto type = GetAddressType(addressOrToken);

        switch(type)
        {
        case TxAddressType::PublicOffline:
        case TxAddressType::MaxPrivacy:
        case TxAddressType::Regular:
            break;
        case TxAddressType::Offline:
            if (!vm[cli::SEND_OFFLINE].as<bool>())
            {
                // Since v6.0 by default offline address triggers the regular online transaction
                // To execute and offline payment the --offline switch should be specified
                type = TxAddressType::Regular;
            }
            break;
        default:
            LOG_ERROR() << kErrorReceiverAddrMissing;
            return false;
        }

        if (!LoadReceiverParams(*receiverParams, params, type))
        {
            return false;
        }

        if (auto peerID = params.GetParameter<WalletID>(beam::wallet::TxParameterID::PeerID); !peerID || std::to_string(*peerID) != addressOrToken)
        {
            params.SetParameter(beam::wallet::TxParameterID::OriginalToken, addressOrToken);
        }

        return true;
    }

    bool LoadBaseParamsForTX(const po::variables_map& vm, const Wallet& wallet, Asset::ID& assetId, Amount& amount, Amount& fee, WalletID& receiverWalletID, bool checkFee, bool skipReceiverWalletID)
    {
        bool hasShieldedOutputs = false;
        if (!skipReceiverWalletID)
        {
            TxParameters params;
            if (!LoadReceiverParams(vm, params))
            {
                return false;
            }
            if (auto peerID = params.GetParameter<WalletID>(TxParameterID::PeerID); peerID)
            {
                receiverWalletID = *peerID;
            }
            if (auto txType = params.GetParameter<TxType>(TxParameterID::TransactionType))
            {
                hasShieldedOutputs = *txType == TxType::PushTransaction;
            }
        }

        if (!ReadAmount(vm, amount))
        {
            return false;
        }

        if (!ReadFee(vm, fee, wallet, checkFee, hasShieldedOutputs))
        {
            return false;
        }

        if(vm.count(cli::ASSET_ID)) // asset id can be zero if beam only
        {
            assetId = vm[cli::ASSET_ID].as<Positive<uint32_t>>().value;
        }

        return true;
    }
} // namespace beam::wallet