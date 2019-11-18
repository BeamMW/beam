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

#include "wallet/common.h"
#include "wallet/wallet_db.h"
#include "wallet/base_transaction.h"
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
        AssetIssueTxBuilder(bool issue, BaseTransaction& tx, SubTxID subTxID, uint32_t assetIdx, IPrivateKeyKeeper::Ptr keyKeeper);

        bool GetInitialTxParams();
        virtual Transaction::Ptr CreateTransaction();

        //
        // Coins, amounts & fees
        //
        Amount GetFee() const;
        Amount GetAmountBeam() const;
        Amount GetAmountAsset() const;
        const  AmountList& GetAmountList() const;
        void   AddChange();
        void   SelectInputs();
        bool   GetInputs();
        bool   GetOutputs();
        void   GenerateAssetCoin(Amount amount);
        void   GenerateBeamCoin(Amount amount);
        bool   CreateInputs();
        bool   CreateOutputs();

        //
        // Blockchain stuff
        //
        const Merkle::Hash& GetKernelID() const;
        const Merkle::Hash& GetEmissionKernelID() const;
        bool LoadKernels();
        void CreateKernels();
        void SignKernels();

        std::string GetKernelIDString() const;
        Height GetMinHeight() const;

    private:
        const CoinIDList& GetInputCoins() const;
        const CoinIDList& GetOutputCoins() const;
        Coin GenerateBeamChangeCoin(Amount amount) const;
        Coin GenerateAssetChangeCoin(Amount amount) const;

    private:
        BaseTransaction& m_Tx;
        IPrivateKeyKeeper::Ptr m_keyKeeper;
        SubTxID m_SubTxID;

        beam::Key::ID m_kernelKeyId;
        beam::AssetID m_assetId;
        uint32_t      m_assetIdx;

        bool       m_issue;
        AmountList m_AmountList;
        Amount     m_Fee;
        Amount     m_ChangeBeam;
        Amount     m_ChangeAsset;
        Height     m_MinHeight;
        Height     m_MaxHeight;

        std::vector<Input::Ptr>  m_Inputs;
        std::vector<Output::Ptr> m_Outputs;
        CoinIDList m_InputCoins;
        CoinIDList m_OutputCoins;

        //
        // Blockchain stuff
        //
        ECC::Scalar::Native m_Offset;

        TxKernel::Ptr m_Kernel;
        TxKernel::Ptr m_EmissionKernel;

        mutable boost::optional<Merkle::Hash> m_KernelID;
        mutable boost::optional<Merkle::Hash> m_EmissionKernelID;
    };
}
