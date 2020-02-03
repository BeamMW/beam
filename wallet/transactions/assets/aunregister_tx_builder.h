// Copyright 2018 The Beam Team
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

#include "wallet/core/common.h"
#include "wallet/core/wallet_db.h"
#include "wallet/core/base_transaction.h"
#include "utility/logger.h"

namespace beam::wallet
{
    class BaseTransaction;
    class AssetUnregisterTxBuilder: public std::enable_shared_from_this<AssetUnregisterTxBuilder>
    {
    public:
        AssetUnregisterTxBuilder(BaseTransaction& tx, SubTxID subTxID);

        bool GetInitialTxParams();
        virtual Transaction::Ptr CreateTransaction();

        //
        // Coins, amounts & fees
        //
        Amount GetFee() const;
        bool GetOutputs();
        void AddRefund();
        void GenerateBeamCoin(Amount amount, bool change);
        bool CreateOutputs();

        Key::Index GetAssetOwnerIdx() const;
        PeerID GetAssetOwnerId() const;

        //
        // Blockchain stuff
        //
        const Merkle::Hash& GetKernelID() const;
        bool LoadKernel();
        bool MakeKernel();

        std::string GetKernelIDString() const;
        Height GetMinHeight() const;

    private:
        BaseTransaction& m_Tx;
        SubTxID m_SubTxID;

        beam::Key::Index m_assetOwnerIdx;
        PeerID m_assetOwnerId;

        Amount     m_Fee;
        Height     m_MinHeight;
        Height     m_MaxHeight;

        std::vector<Output::Ptr> m_Outputs;
        CoinIDList m_OutputCoins;

        //
        // Blockchain stuff
        //
        ECC::Scalar::Native m_Offset;
        TxKernelAssetDestroy::Ptr m_kernel;
        mutable boost::optional<Merkle::Hash> m_kernelID;
    };
}
