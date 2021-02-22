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
    address.setLabel(label);
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

Amount CalcCoinSelectionInfo2(Height h, const IWalletDB::Ptr& walletDB, Amount requestedSum, Asset::ID aid, Amount valFeeInputShielded, Amount& feesInvoluntary)
{
    std::vector<Coin> vStd;
    std::vector<ShieldedCoin> vShielded;

    uint32_t nMaxShielded = Rules::get().Shielded.MaxIns;
    if (aid)
        nMaxShielded /= 2; // leave some for std

    walletDB->selectCoins2(h, requestedSum, aid, vStd, vShielded, nMaxShielded, true);

    Amount ret = 0;
    for (const auto& c : vStd)
        ret += c.m_ID.m_Value;

    for (const auto& c : vShielded)
        ret += c.m_CoinID.m_Value;

    auto nInputsShielded = static_cast<uint32_t>(vShielded.size());
    feesInvoluntary = valFeeInputShielded * nInputsShielded;

    return ret;
}


void CoinsSelectionInfo::Calculate(Height h, const IWalletDB::Ptr& walletDB, bool isPushTx)
{
    Transaction::FeeSettings fs(h);
    m_minimalExplicitFee = fs.m_Kernel;
    m_minimalExplicitFee += isPushTx ? fs.m_ShieldedOutputTotal : fs.m_Output; // tx value

    m_isEnought = true;

    Amount valBeams = m_requestedSum;
    m_involuntaryFee = 0;

    if (m_assetID)
    {
        m_changeAsset = 0;
        m_selectedSumAsset = CalcCoinSelectionInfo2(h, walletDB, m_requestedSum, m_assetID, fs.m_ShieldedInputTotal, m_involuntaryFee);

        if (m_selectedSumAsset < m_requestedSum) {
            m_isEnought = false;
        }
        else {
            if (m_selectedSumAsset > m_requestedSum) {
                // change
                m_changeAsset = m_selectedSumAsset - m_requestedSum;
                m_minimalExplicitFee += fs.m_Output;
            }
        }

        valBeams = 0;
    }

    std::setmax(m_explicitFee, m_minimalExplicitFee);
    valBeams += m_explicitFee + m_involuntaryFee;

    m_changeBeam = 0;

    Amount feeInvoluntary2;
    m_selectedSumBeam = CalcCoinSelectionInfo2(h, walletDB, valBeams, 0, fs.m_ShieldedInputTotal, feeInvoluntary2);

    if (m_selectedSumBeam > valBeams + feeInvoluntary2) {

        // change output is necessary
        m_minimalExplicitFee += fs.m_Output;

        if (m_explicitFee < m_minimalExplicitFee)
        {
            // retry
            valBeams += m_minimalExplicitFee - m_explicitFee;
            m_explicitFee = m_minimalExplicitFee;

            m_selectedSumBeam = CalcCoinSelectionInfo2(h, walletDB, valBeams, 0, fs.m_ShieldedInputTotal, feeInvoluntary2);
        }
    }

    valBeams += feeInvoluntary2;
    m_involuntaryFee += feeInvoluntary2;

    if (m_selectedSumBeam < valBeams) {
        m_isEnought = false;
    }
    else {
        if (m_selectedSumBeam > valBeams) {
            m_changeBeam = m_selectedSumBeam - valBeams;
        }
    }

    if (!m_assetID)
    {
        m_selectedSumAsset = m_selectedSumBeam;
        m_changeAsset = m_changeBeam;
    }
}

Amount CoinsSelectionInfo::get_TotalFee() const
{
    return m_explicitFee + m_involuntaryFee;
}

Amount CoinsSelectionInfo::get_NettoValue() const
{
    Amount val = m_selectedSumAsset - m_changeAsset;
    if (m_assetID)
        return val;

    // subtract the fee
    Amount fees = get_TotalFee();

    return (val > fees) ? (val - fees) : 0;
}

}  // namespace beam::wallet
