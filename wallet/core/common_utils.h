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

#pragma once

#include "wallet/core/wallet_db.h"
#include <string>

namespace beam::wallet
{
WalletAddress GenerateNewAddress(
        const IWalletDB::Ptr& walletDB,
        const std::string& label,
        WalletAddress::ExpirationStatus expirationStatus
            = WalletAddress::ExpirationStatus::OneDay,
        bool saveRequired = true);
bool ReadTreasury(ByteBuffer&, const std::string& sPath);
std::string TxIDToString(const TxID& txId);
Amount CalcChange(const IWalletDB::Ptr& walletDB, Amount amount);
Amount AccumulateCoinsSum(
        const std::vector<Coin>& vSelStd,
        const std::vector<ShieldedCoin>& vSelShielded);
struct ShieldedCoinsSelectionInfo
{
        Amount requestedSum = 0;
        Amount selectedSum = 0;
        Amount requestedFee = 0;
        Amount selectedFee = 0;
        Amount minimalFee = 0;
        Amount shieldedInputsFee = 0;
        Amount shieldedOutputsFee = 0;
        Amount change = 0;
};
ShieldedCoinsSelectionInfo CalcShieldedCoinSelectionInfo(
        const IWalletDB::Ptr& walletDB, Amount requestedSum, Amount requestedFee, bool isPushTx = false);
class BaseTxBuilder;
Amount GetFeeWithAdditionalValueForShieldedInputs(const BaseTxBuilder& builder);

}  // namespace beam::wallet
