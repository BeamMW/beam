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

#include <boost/format.hpp>
#include <boost/multiprecision/cpp_dec_float.hpp>

using namespace std;
using namespace beam;
using namespace beam::wallet;
using namespace ECC;

namespace beam::wallet
{
bool ReadAmount(const po::variables_map& vm, Amount& amount, const Amount& limit, bool asset)
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
            ssLimit << PrintableAmount(limit, false, asset ? kAmountASSET : "", asset ? kAmountAGROTH : "");
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

bool ReadFee(const po::variables_map& vm, Amount& fee, bool checkFee)
{
    if (auto it = vm.find(cli::FEE); it != vm.end())
    {
        fee = it->second.as<Nonnegative<Amount>>().value;
    }
    else
    {
        fee = kMinFeeInGroth;
    }

    if (checkFee && fee < kMinFeeInGroth)
    {
        LOG_ERROR() << kErrorFeeToLow;
        return false;
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
    if (!LoadReceiverParams(*receiverParams, params))
    {
        return false;
    }
    if (auto peerID = params.GetParameter<WalletID>(beam::wallet::TxParameterID::PeerID); !peerID || std::to_string(*peerID) != addressOrToken)
    {
        params.SetParameter(beam::wallet::TxParameterID::OriginalToken, addressOrToken);
    }

    if (vm.find(cli::MAX_PRIVACY_ADDRESS) != vm.end() && vm[cli::MAX_PRIVACY_ADDRESS].as<bool>())
    {
        params.SetParameter(TxParameterID::TransactionType, TxType::PushTransaction);
    }
    return true;
}

bool LoadBaseParamsForTX(const po::variables_map& vm, Asset::ID& assetId, Amount& amount, Amount& fee, WalletID& receiverWalletID, bool checkFee, bool skipReceiverWalletID)
{
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
    }

    if (!ReadAmount(vm, amount))
    {
        return false;
    }

    if (!ReadFee(vm, fee, checkFee))
    {
        return false;
    }

    if(vm.count(cli::ASSET_ID)) // asset id can be zero if beam only
    {
        assetId = vm[cli::ASSET_ID].as<Positive<uint32_t>>().value;
    }

    return true;
}

bool CheckFeeForShieldedInputs(Amount amount, Amount fee, Asset::ID assetId, const IWalletDB::Ptr& walletDB, bool isPushTx, Amount& feeForShieldedInputs)
{
    Transaction::FeeSettings fs;
    Amount shieldedOutputsFee = isPushTx ? fs.m_Kernel + fs.m_Output + fs.m_ShieldedOutput : 0;

    auto coinSelectionRes = CalcShieldedCoinSelectionInfo(
        walletDB, amount, (isPushTx && fee > shieldedOutputsFee) ? fee - shieldedOutputsFee : fee, isPushTx);
    feeForShieldedInputs = coinSelectionRes.shieldedInputsFee;

    bool isBeam = assetId == Asset::s_BeamID;
    if (isBeam && (coinSelectionRes.selectedSumBeam - coinSelectionRes.selectedFee - coinSelectionRes.changeBeam) < amount)
    {
        LOG_ERROR() << kErrorNotEnoughtCoins;
        return false;
    }

    if (!isBeam && (coinSelectionRes.selectedSumAsset - coinSelectionRes.changeAsset < amount))
    {
        // TODO: enough beam & asset
        LOG_ERROR() << kErrorNotEnoughtCoins;
        return false;
    }

    if (coinSelectionRes.minimalFee > fee)
    {
        if (isPushTx && !coinSelectionRes.shieldedInputsFee)
        {
            LOG_ERROR() << boost::format(kErrorFeeForShieldedOutToLow) % coinSelectionRes.minimalFee;
        }
        else
        {
            LOG_ERROR() << boost::format(kErrorFeeForShieldedToLow) % coinSelectionRes.minimalFee;
        }
        return false;
    }

    return true;
}

} // namespace beam::wallet