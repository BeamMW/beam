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

Amount CalcChange(const IWalletDB::Ptr& walletDB, Amount amount)
{
    auto coins = walletDB->selectCoins(amount, Zero);
    Amount sum = accumulate(coins.begin(), coins.end(), (Amount)0, [] (Amount sum, const Coin& c) {
        return sum + c.m_ID.m_Value;
    });

    return sum <= amount ? 0 : sum - amount;
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
    const IWalletDB::Ptr& walletDB, Amount requestedSum, Amount beforehandMinFee)
{
    std::vector<Coin> vSelStd;
    std::vector<ShieldedCoin> vSelShielded;
    walletDB->selectCoins2(
        requestedSum + beforehandMinFee, Zero, vSelStd, vSelShielded, Rules::get().Shielded.MaxIns, true);
    Amount sum  = AccumulateCoinsSum(vSelStd, vSelShielded);

    TxStats ts;
    ts.m_Outputs = sum > requestedSum + beforehandMinFee ? 2 : 1;
    ts.m_InputsShielded = vSelShielded.size();
    ts.m_Kernels = 1 + ts.m_InputsShielded;

    Transaction::FeeSettings fs;
    Amount minFee = fs.Calculate(ts);
    Amount shieldedFee = ts.m_InputsShielded * (fs.m_Kernel + fs.m_ShieldedInput);

    if (sum < requestedSum + beforehandMinFee) 
        return {requestedSum, sum, beforehandMinFee, std::max(beforehandMinFee, minFee), minFee, shieldedFee, 0};

    if (beforehandMinFee >= minFee)
    {
        auto change = sum - beforehandMinFee - requestedSum;
        return {requestedSum, sum, beforehandMinFee, beforehandMinFee, minFee, shieldedFee, change};
    }
    else
    {
        auto res = CalcShieldedCoinSelectionInfo(walletDB, requestedSum, minFee);
        res.requestedFee = beforehandMinFee;
        return res;
    }
}

}  // namespace beam::wallet
