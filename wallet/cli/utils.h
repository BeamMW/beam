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

#pragma once

#include <boost/program_options.hpp>

#include "utility/cli/options.h"

#include "wallet/core/common.h"
#include "wallet/core/wallet_db.h"

namespace beam::wallet
{
bool ReadAmount(const po::variables_map& vm, Amount& amount, const Amount& limit = std::numeric_limits<Amount>::max(), bool asset = false);
bool ReadFee(const po::variables_map& vm, Amount& fee, bool checkFee);
bool LoadReceiverParams(const po::variables_map& vm, TxParameters& params);
bool LoadBaseParamsForTX(const po::variables_map& vm, Asset::ID& assetId, Amount& amount, Amount& fee, WalletID& receiverWalletID, bool checkFee, bool skipReceiverWalletID = false);
bool CheckFeeForShieldedInputs(Amount amount, Amount fee, const IWalletDB::Ptr& walletDB, bool isPushTx, Amount& feeForShieldedInputs);
} // namespace beam::wallet