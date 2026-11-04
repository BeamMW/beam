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

    bool ParseAmount(const std::string& strAmount, Amount& amount, std::string& error, const Amount& limit = std::numeric_limits<AmountSigned>::max(), Asset::ID assetId = Asset::s_BeamID);
    bool ReadAmount(const po::variables_map& vm, Amount& amount, const Amount& limit = std::numeric_limits<AmountSigned>::max(), Asset::ID assetId = Asset::s_BeamID);
    bool ReadFee(const po::variables_map& vm, Amount& fee, const Wallet&, bool hasShieldedOutputs = false);
    bool LoadReceiverParams(const po::variables_map& vm, TxParameters& params);
    bool LoadReceiverParams(const std::string& addressOrToken, bool sendOffline, TxParameters& params);
    bool LoadBaseParamsForTX(const po::variables_map& vm, const Wallet&, Asset::ID& assetId, Amount& amount, Amount& fee, WalletID& receiverWalletID, bool skipReceiverWalletID = false);

    struct ReceiverAmount
    {
        std::string m_Address;
        Amount m_Amount = 0;
    };

    // Parses a list of <address>:<amount> entries (amount in Beams) separated by commas, semicolons
    // or whitespace (so a file with one entry per line works too). '#' starts a comment till the end of the line.
    // Fails on an empty list, a malformed entry, an invalid amount or a duplicate address.
    bool ParseReceiversList(const std::string& str, std::vector<ReceiverAmount>& receivers, std::string& error);
}
