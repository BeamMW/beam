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
    bool ReadTreasury(ByteBuffer&, const std::string& sPath);

    Amount AccumulateCoinsSum(const std::vector<Coin>& vSelStd, const std::vector<ShieldedCoin>& vSelShielded);

    struct CoinsSelectionInfo
    {
        Amount m_requestedSum = 0U;
        Amount m_selectedSumAsset = 0U; // if assetId is BEAM then selectedSumAsset == selectedSumBeam
        Amount m_selectedSumBeam = 0U;
        Amount m_changeAsset = 0U; // if assetId is BEAM then changeAsset == changeBeam
        Amount m_changeBeam = 0U;
        Amount m_minimalRawFee = 0U; // the very minimum fee for tx elements, wouldn't suffice for decoys
        Amount m_minimalExplicitFee = 0U; // the minimum recommended for this tx type.
        Amount m_explicitFee = 0U;
        Amount m_involuntaryFee = 0U;

        Asset::ID m_assetID = Asset::s_BeamID;
        bool m_isEnought = true;

        // set m_requestedSum, m_assetID and m_explicitFee before calling the following
        void Calculate(Height, const IWalletDB::Ptr& walletDB, bool isPushTx = false);

        Amount get_TotalFee() const;
        Amount get_NettoValue() const;
    };
}
