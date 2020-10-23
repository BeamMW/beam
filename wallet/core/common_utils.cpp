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

    if (amountAsset)
    {
        auto coins = walletDB->selectCoins(amountAsset, assetId);
        const auto assetAvailable = accumulate(coins.begin(), coins.end(), (Amount)0, [] (Amount sum, const Coin& c)
        {
            return sum + c.m_ID.m_Value;
        });

        result.changeAsset = assetAvailable <= amountAsset ? 0 : assetAvailable - amountAsset;
        if (isBeamTx)
        {
            result.changeBeam = result.changeAsset;
        }
    }

    if (!isBeamTx && beamFee)
    {
        auto coins = walletDB->selectCoins(amountAsset, assetId);
        const auto beamAvailable = accumulate(coins.begin(), coins.end(), (Amount)0, [assetId] (Amount sum, const Coin& c)
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

ShieldedCoinsSelectionInfo CalcShieldedCoinSelectionInfo(
    const IWalletDB::Ptr& walletDB, Amount requestedSum, Amount requestedFee, bool isPushTx /* = false */)
{
    std::vector<Coin> vSelStd;
    std::vector<ShieldedCoin> vSelShielded;

    Transaction::FeeSettings fs;
    TxStats ts;
    if(isPushTx)
        ++ts.m_OutputsShielded;

    Amount shieldedOutputsFee = ts.m_OutputsShielded * (fs.m_Kernel + fs.m_Output + fs.m_ShieldedOutput);

    walletDB->selectCoins2(
        requestedSum + requestedFee + shieldedOutputsFee, Zero, vSelStd, vSelShielded, Rules::get().Shielded.MaxIns, true);
    Amount sum  = AccumulateCoinsSum(vSelStd, vSelShielded);

    ts.m_Outputs = sum > requestedSum + requestedFee + shieldedOutputsFee ? 2 : 1;
    ts.m_InputsShielded = vSelShielded.size();
    ts.m_Kernels = ts.m_Outputs + ts.m_InputsShielded + ts.m_OutputsShielded;

    Amount minFee = fs.Calculate(ts);
    Amount shieldedInputsFee = ts.m_InputsShielded * (fs.m_Kernel + fs.m_ShieldedInput);

    Amount selectedFee = std::max(requestedFee + shieldedOutputsFee, minFee);

    if (!shieldedInputsFee)
    {
        selectedFee = minFee;
        if (selectedFee - shieldedOutputsFee < kMinFeeInGroth)
            selectedFee = shieldedOutputsFee + kMinFeeInGroth;
        auto change = sum - requestedSum - selectedFee;
        return {requestedSum, sum, requestedFee, selectedFee, std::max(selectedFee, minFee), shieldedInputsFee, shieldedOutputsFee, change};
    }
    else if (selectedFee == minFee && selectedFee - (shieldedInputsFee + shieldedOutputsFee) < kMinFeeInGroth)
    {
        auto res = CalcShieldedCoinSelectionInfo(walletDB, requestedSum, shieldedInputsFee + kMinFeeInGroth, isPushTx);
        res.requestedFee = requestedFee;
        return res;
    }

    if (sum < requestedSum + selectedFee)
    {
        return {requestedSum, sum, requestedFee, selectedFee, std::max(selectedFee, minFee), shieldedInputsFee, shieldedOutputsFee, 0};
    }

    if (selectedFee >= minFee)
    {
        auto change = sum - requestedSum - selectedFee;
        return {requestedSum, sum, requestedFee, selectedFee, std::max(selectedFee, minFee), shieldedInputsFee, shieldedOutputsFee, change};
    }
    else
    {
        auto res = CalcShieldedCoinSelectionInfo(walletDB, requestedSum, minFee - shieldedOutputsFee, isPushTx);
        res.requestedFee = requestedFee;
        return res;
    }
}

Amount GetFeeWithAdditionalValueForShieldedInputs(const BaseTxBuilder& builder)
{
    Amount shieldedFee = GetShieldedFee(builder.m_Coins.m_InputShielded.size());
    return !!shieldedFee ? shieldedFee + builder.m_Fee : builder.m_Fee;
}

}  // namespace beam::wallet
