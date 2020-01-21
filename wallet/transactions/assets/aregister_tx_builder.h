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
    class AssetRegisterTxBuilder: public std::enable_shared_from_this<AssetRegisterTxBuilder>
    {
    public:
        AssetRegisterTxBuilder(BaseTransaction& tx, SubTxID subTxID);

        bool GetInitialTxParams();
        virtual Transaction::Ptr CreateTransaction();

        //
        // Coins, amounts & fees
        //
        Amount GetFee() const;
        Amount GetAmountBeam() const;
        const AmountList& GetAmountList() const;
        void AddChange();
        void SelectInputs();
        bool GetInputs();
        bool GetOutputs();
        void GenerateBeamCoin(Amount amount, bool change);
        bool CreateInputs();
        bool CreateOutputs();
        Key::Index GetAssetOwnerIdx() const;

        //
        // Blockchain stuff
        //
        const Merkle::Hash& GetKernelID() const;
        bool LoadKernel();
        bool MakeKernel();

        std::string GetKernelIDString() const;
        Height GetMinHeight() const;

    protected:

        template <typename Result, typename Func, typename ContinueFunc>
        void DoAsync(Func&& asyncFunc, ContinueFunc&& continueFunc)
        {
            auto thisHolder = shared_from_this();
            auto txHolder = m_Tx.shared_from_this(); // increment use counter of tx object. We use it to avoid tx object desctruction during Update call.
            m_Tx.GetAsyncAcontext().OnAsyncStarted();

            asyncFunc(
                [thisHolder, this, txHolder, continueFunc](Result&& res)
                {
                    continueFunc(std::move(res));
                    m_Tx.UpdateAsync(); // may complete transaction
                    m_Tx.GetAsyncAcontext().OnAsyncFinished();
                },
                [thisHolder, this, txHolder](const std::exception_ptr& ex)
                {
                    m_Tx.GetAsyncAcontext().OnAsyncFinished();
                    std::rethrow_exception(ex);
                });
        }

    private:
        const CoinIDList& GetInputCoins() const;
        const CoinIDList& GetOutputCoins() const;

    private:
        BaseTransaction& m_Tx;
        SubTxID m_SubTxID;

        beam::Key::Index m_assetOwnerIdx;
        Amount     m_Fee;
        Amount     m_ChangeBeam;
        AmountList m_AmountList;
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
        TxKernelAssetCreate::Ptr m_kernel;
        mutable boost::optional<Merkle::Hash> m_kernelID;
    };
}
