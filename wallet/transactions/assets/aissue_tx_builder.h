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
    class AssetIssueTxBuilder: public std::enable_shared_from_this<AssetIssueTxBuilder>
    {
    public:
        //
        // Transaction
        //
        AssetIssueTxBuilder(bool issue, BaseTransaction& tx, SubTxID subTxID);

        bool GetInitialTxParams();
        virtual Transaction::Ptr CreateTransaction();

        //
        // Coins, amounts & fees
        //
        Amount   GetFee() const;
        Amount   GetTransactionAmount() const;
        Amount   GetAmountAsset() const;
        const    AmountList& GetAmountList() const;
        void     AddChange();
        void     SelectInputCoins();
        bool     GetInputs();
        bool     GetOutputs();
        void     GenerateAssetCoin(Amount amount, bool change);
        void     GenerateBeamCoin(Amount amount, bool change);
        void     CreateInputs();
        void     CreateOutputs();

        Asset::ID  GetAssetId() const;
        PeerID     GetAssetOwnerId() const;

        //
        // Blockchain stuff
        //
        const Merkle::Hash& GetKernelID() const;
        bool LoadKernel();
        void MakeKernel();

        std::string GetKernelIDString() const;
        Height GetMinHeight() const;

    private:
        const CoinIDList& GetInputCoins() const;
        const CoinIDList& GetOutputCoins() const;

    private:
        BaseTransaction& m_Tx;
        SubTxID m_SubTxID;
        PeerID  m_assetOwnerId;

        bool        m_issue;
        std::string m_Metadata;
        AmountList  m_AmountList;
        Amount      m_Fee;
        Amount      m_ChangeBeam;
        Amount      m_ChangeAsset;
        Height      m_MinHeight;
        Height      m_MaxHeight;

        std::vector<Input::Ptr>  m_Inputs;
        std::vector<Output::Ptr> m_Outputs;
        CoinIDList m_InputCoins;
        CoinIDList m_OutputCoins;

        //
        // Blockchain stuff
        //
        ECC::Scalar::Native m_Offset;
        TxKernelAssetEmit::Ptr m_Kernel;
        mutable boost::optional<Merkle::Hash> m_KernelID;
    };
}
