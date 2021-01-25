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

#include "wallet/core/common_utils.h"

#include "wallet/core/base_transaction.h"
#include "wallet/core/base_tx_builder.h"
#include "wallet/core/strings_resources.h"
#include "utility/logger.h"

#include <boost/format.hpp>
#include <numeric>

namespace beam::wallet
{
WalletAddress GenerateNewAddress(
        const IWalletDB::Ptr& walletDB,
        const std::string& label,
        WalletAddress::ExpirationStatus expirationStatus,
        bool saveRequired)
{
    WalletAddress address;
    walletDB->createAddress(address);

    address.setExpiration(expirationStatus);
    address.m_label = label;
    if (saveRequired)
    {
        walletDB->saveAddress(address);
    }

    LOG_INFO() << boost::format(kAddrNewGenerated) 
                % std::to_string(address.m_walletID);
    if (!label.empty()) {
        LOG_INFO() << boost::format(kAddrNewGeneratedLabel) % label;
    }
    return address;
}

bool ReadTreasury(ByteBuffer& bb, const std::string& sPath)
{
    if (sPath.empty())
        return false;

    std::FStream f;
    if (!f.Open(sPath.c_str(), true))
        return false;

    size_t nSize = static_cast<size_t>(f.get_Remaining());
    if (!nSize)
        return false;

    bb.resize(f.get_Remaining());
    return f.read(&bb.front(), nSize) == nSize;
}

std::string TxIDToString(const TxID& txId)
{
    return to_hex(txId.data(), txId.size());
}

Change CalcChange(const IWalletDB::Ptr& walletDB, Amount amountAsset, Amount beamFee, Asset::ID assetId)
{
    Change result;
    result.assetId = assetId;

    const bool isBeamTx = assetId == Asset::s_BeamID;
    if (isBeamTx)
    {
        amountAsset += beamFee;
    }

    if (amountAsset > 0)
    {
        auto coins = walletDB->selectCoins(amountAsset, assetId);
        const auto assetAvailable = accumulate(coins.begin(), coins.end(), AmountBig::Type(Zero), [] (const AmountBig::Type& sum, const Coin& c)
        {
            auto result = sum;
            result += AmountBig::Type(c.m_ID.m_Value);
            return result;
        });

        {
            // Do not use bigAmountAsset outside of this block, it is negated below
            AmountBig::Type bigAmountAsset(amountAsset);
            if (assetAvailable > bigAmountAsset)
            {
                AmountBig::Type changeAsset = assetAvailable;
                bigAmountAsset.Negate();
                changeAsset += bigAmountAsset;

                assert(AmountBig::get_Hi(result.changeAsset) == 0);
                result.changeAsset = AmountBig::get_Lo(changeAsset);
            }
        }

        if (isBeamTx)
        {
            result.changeBeam = AmountBig::get_Lo(result.changeAsset);
        }
    }

    if (!isBeamTx && beamFee)
    {
        auto coins = walletDB->selectCoins(amountAsset, Asset::s_BeamID);
        const auto beamAvailable = accumulate(coins.begin(), coins.end(), (Amount)0, [] (Amount sum, const Coin& c)
        {
            return sum + c.m_ID.m_Value;
        });

        result.changeBeam = beamAvailable <= beamFee ? 0 : beamAvailable - beamFee;
    }

    return result;
}

Amount AccumulateCoinsSum(const std::vector<Coin>& vSelStd, const std::vector<ShieldedCoin>& vSelShielded)
{
    Amount sum = accumulate(vSelStd.begin(), vSelStd.end(), (Amount)0, [] (Amount sum, const Coin& c) {
        return sum + c.m_ID.m_Value;
    });

    sum = accumulate(vSelShielded.begin(), vSelShielded.end(), sum, [] (Amount sum, const ShieldedCoin& c) {
        return sum + c.m_CoinID.m_Value;
    });

    return sum;
}

ShieldedCoinsSelectionInfo CalcShieldedCoinSelectionInfo(const IWalletDB::Ptr& walletDB, Amount requestedSum, Amount requestedFee, Asset::ID assetId, bool isPushTx)
{
    std::vector<Coin> beamCoins, nonbeamCoins;
    std::vector<ShieldedCoin> beamShielded, nonbeamShielded;

    Transaction::FeeSettings fs;
    TxStats ts;
    if(isPushTx)
    {
        ++ts.m_OutputsShielded;
    }

    const bool isBeam = assetId == Asset::s_BeamID;
    Amount shieldedOutputsFee = ts.m_OutputsShielded * (fs.m_Kernel + fs.m_Output + fs.m_ShieldedOutput);

    if (isBeam)
    {
         walletDB->selectCoins2(requestedSum + requestedFee + shieldedOutputsFee, Asset::s_BeamID, beamCoins, beamShielded, Rules::get().Shielded.MaxIns, true);
    }
    else
    {
        walletDB->selectCoins2(requestedFee + shieldedOutputsFee, Asset::s_BeamID, beamCoins, beamShielded, Rules::get().Shielded.MaxIns, true);
        walletDB->selectCoins2(requestedSum, assetId, nonbeamCoins, nonbeamShielded, Rules::get().Shielded.MaxIns, true);
    }

    Amount reqBeam = isBeam ? requestedSum : 0;
    Amount sumBeam = AccumulateCoinsSum(beamCoins, beamShielded);
    Amount reqNonBeam = isBeam ? 0 : requestedSum;
    Amount sumNonBeam = AccumulateCoinsSum(nonbeamCoins, nonbeamShielded);

    ts.m_Outputs  = sumBeam > (reqBeam + requestedFee + shieldedOutputsFee) ? 2 : 1 + sumNonBeam > reqNonBeam ? 1 : 0;
    ts.m_InputsShielded = beamShielded.size() + nonbeamShielded.size();
    ts.m_Kernels = ts.m_Outputs + ts.m_InputsShielded + ts.m_OutputsShielded;

    Amount minFee = fs.Calculate(ts);
    Amount shieldedInputsFee = ts.m_InputsShielded * (fs.m_Kernel + fs.m_ShieldedInput);
    Amount selectedFee = requestedFee >= shieldedInputsFee + shieldedOutputsFee
        ? requestedFee
        : std::max(requestedFee + shieldedOutputsFee, minFee);

    if (!shieldedInputsFee)
    {
        selectedFee = minFee;
        if (selectedFee - shieldedOutputsFee < kMinFeeInGroth)
        {
            selectedFee = shieldedOutputsFee + kMinFeeInGroth;
        }

        // if asset is beam then changeAsset == changeBeam by convention
        auto changeBeam  = sumBeam - reqBeam - selectedFee;
        auto changeAsset = isBeam ? changeBeam : sumNonBeam - reqNonBeam;
        bool isSelectedEnought = isBeam ? (sumBeam >= requestedSum + selectedFee) : (sumNonBeam >= requestedSum && sumBeam >= selectedFee);

        return {
            requestedSum,
            sumBeam,
            isBeam ? sumBeam : sumNonBeam,
            requestedFee,
            selectedFee,
            std::max(selectedFee, minFee),
            shieldedInputsFee,
            shieldedOutputsFee,
            changeBeam,
            changeAsset,
            assetId,
            isSelectedEnought
        };
    }
    else if (selectedFee == minFee && selectedFee - (shieldedInputsFee + shieldedOutputsFee) < kMinFeeInGroth)
    {
        auto res = CalcShieldedCoinSelectionInfo(walletDB, requestedSum, shieldedInputsFee + kMinFeeInGroth, assetId, isPushTx);
        res.requestedFee = requestedFee;
        return res;
    }

    if (sumBeam < reqBeam + selectedFee || sumNonBeam < reqNonBeam)
    {
        auto changeBeam = sumBeam < reqBeam + selectedFee ? 0 : sumBeam - reqBeam + selectedFee;
        auto changeAsset = isBeam ? changeBeam : (sumNonBeam < reqNonBeam ? 0 : sumNonBeam - reqNonBeam);
        bool isSelectedEnought = isBeam ? (sumBeam >= requestedSum + selectedFee) : (sumNonBeam >= requestedSum && sumBeam >= selectedFee);
        return {
            requestedSum,
            sumBeam,
            isBeam ? sumBeam : sumNonBeam,
            requestedFee,
            selectedFee,
            std::max(selectedFee, minFee),
            shieldedInputsFee,
            shieldedOutputsFee,
            changeBeam,
            changeAsset,
            assetId,
            isSelectedEnought
        };
    }

    if (selectedFee >= minFee)
    {
        auto changeBeam = sumBeam - reqBeam - selectedFee;
        auto changeAsset = isBeam ? changeBeam : sumNonBeam - reqNonBeam;
        bool isSelectedEnought = isBeam ? (sumBeam >= requestedSum + selectedFee) : (sumNonBeam >= requestedSum && sumBeam >= selectedFee);

        return {
            requestedSum,
            sumBeam,
            isBeam ? sumBeam : sumNonBeam,
            requestedFee,
            selectedFee,
            std::max(selectedFee, minFee),
            shieldedInputsFee,
            shieldedOutputsFee,
            changeBeam,
            changeAsset,
            assetId,
            isSelectedEnought
        };
    }
    else
    {
        auto res = CalcShieldedCoinSelectionInfo(walletDB, requestedSum, minFee - shieldedOutputsFee, assetId, isPushTx);
        res.requestedFee = requestedFee;
        return res;
    }
}

Amount GetFeeWithAdditionalValueForShieldedInputs(const BaseTxBuilder& builder)
{
    Amount shieldedFee = CalculateShieldedFeeByKernelsCount(builder.m_Coins.m_InputShielded.size());
    return shieldedFee + builder.m_Fee;
}

}  // namespace beam::wallet
