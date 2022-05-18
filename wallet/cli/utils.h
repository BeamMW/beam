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

namespace beam::wallet
{
	class Wallet;

    bool ReadAmount(const po::variables_map& vm, Amount& amount, const Amount& limit = std::numeric_limits<AmountSigned>::max(), Asset::ID assetId = Asset::s_BeamID);
    bool ReadFee(const po::variables_map& vm, Amount& fee, const Wallet&, bool hasShieldedOutputs = false);
    bool LoadReceiverParams(const po::variables_map& vm, TxParameters& params);
    bool LoadBaseParamsForTX(const po::variables_map& vm, const Wallet&, Asset::ID& assetId, Amount& amount, Amount& fee, WalletID& receiverWalletID, bool skipReceiverWalletID = false);
}
