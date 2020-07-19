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

#include "common.h"
#include "wallet_db.h"
#include "base_transaction.h"

#include <condition_variable>
#include <boost/optional.hpp>
#include "utility/logger.h"

namespace beam::wallet
{

    class BaseTxBuilder : public std::enable_shared_from_this<BaseTxBuilder>
    {
    public:
        BaseTxBuilder(BaseTransaction& tx, SubTxID subTxID, const AmountList& amount, Amount fee);
        virtual ~BaseTxBuilder() = default;
        void SelectInputs();
        void SelectFeeInputsPreferUnlinked();
        void AddChange();
        void GenerateAssetCoin(Amount amount, bool change);
        void GenerateBeamCoin(Amount amount, bool change);
        bool CreateOutputs();
        bool FinalizeOutputs();
        bool LoadKernel();
        bool HasKernelID() const;
        void CreateKernel();
        void GenerateNonce();
        virtual ECC::Point::Native GetPublicExcess() const;
        ECC::Point::Native GetPublicNonce() const;
        Asset::ID GetAssetId() const;
        bool IsAssetTx() const;
        virtual bool GetInitialTxParams();
        bool GetInputs();
        bool GetOutputs();
        bool GetPeerPublicExcessAndNonce();
        bool GetPeerSignature();
        bool GetPeerInputsAndOutputs();
        void FinalizeSignature();
        bool CreateInputs();
        void FinalizeInputs();
        virtual Transaction::Ptr CreateTransaction();
        bool SignSender(bool initial, bool bIsConventional = true);
        bool SignReceiver(bool bIsConventional = true);
        bool SignSplit();
        bool IsPeerSignatureValid() const;

        Amount GetAmount() const;
        const AmountList& GetAmountList() const;
        Amount GetFee() const;
        Height GetLifetime() const;
        Height GetMinHeight() const;
        virtual Height GetMaxHeight() const;
        const std::vector<Input::Ptr>& GetInputs() const;
        const std::vector<Output::Ptr>& GetOutputs() const;
        const ECC::Scalar::Native& GetOffset() const;
        const ECC::Scalar::Native& GetPartialSignature() const;
        const TxKernel& GetKernel() const;
        const Merkle::Hash& GetKernelID() const;
        void StoreKernelID();
        void ResetKernelID();
        std::string GetKernelIDString() const;
        bool UpdateMaxHeight();
        bool IsAcceptableMaxHeight() const;
        ECC::Hash::Value GetLockImage() const;
        SubTxID GetSubTxID() const;

        const std::vector<Coin::ID>& GetInputCoins() const;
        const std::vector<Coin::ID>& GetOutputCoins() const;

    protected:

        struct KeyKeeperHandler
            :public IPrivateKeyKeeper2::Handler
        {
            std::weak_ptr<BaseTxBuilder> m_pBuilder;
            bool* m_pLink;

            KeyKeeperHandler(BaseTxBuilder&, bool& bLink);
            ~KeyKeeperHandler();

            virtual void OnDone(IPrivateKeyKeeper2::Status::Type) override;

            virtual void OnSuccess(BaseTxBuilder&) = 0;
            virtual void OnFailed(BaseTxBuilder&, IPrivateKeyKeeper2::Status::Type);

            void Detach(BaseTxBuilder&);
            void OnAllDone(BaseTxBuilder&);
        };

    private:
        Amount GetMinimumFee() const;
        void CheckMinimumFee();
        bool SignReceiverOrSplit(bool bFromYourself, bool bIsConventional);

        template<typename T1, typename T2>
        void StoreAndLoad(TxParameterID parameterID, const T1& source, T2& dest)
        {
            m_Tx.SetParameter(parameterID, source, m_SubTxID);
            m_Tx.GetParameter(parameterID, dest, m_SubTxID);
        }

        void CreateInputsStd();
        void CreateInputsShielded();

    protected:
        BaseTransaction& m_Tx;
        SubTxID m_SubTxID;

        // input
        AmountList m_AmountList;
        Amount m_Fee;
        Amount m_ChangeBeam;
        Amount m_ChangeAsset;
        Height m_Lifetime;
        Height m_MinHeight;
        Height m_MaxHeight;
        std::vector<Input::Ptr> m_Inputs;
        std::vector<Output::Ptr> m_Outputs;
        ECC::Scalar::Native m_Offset; // goes to offset
        std::vector<TxKernelShieldedInput::Ptr> m_InputsShielded;

        std::vector<Coin::ID> m_InputCoins;
        std::vector<Coin::ID> m_OutputCoins;
        std::vector<ShieldedTxo::BaseKey> m_InputCoinsShielded;
        ECC::Point::Native m_PublicNonce;
        ECC::Point::Native m_PublicExcess;

        // peer values
        ECC::Scalar::Native m_PartialSignature;
        ECC::Point::Native m_PeerPublicNonce;
        ECC::Point::Native m_PeerPublicExcess;
        std::vector<Input::Ptr> m_PeerInputs;
        std::vector<Output::Ptr> m_PeerOutputs;
        ECC::Scalar::Native m_PeerOffset;
        Height m_PeerMaxHeight;

        // deduced values,
        TxKernelStd::Ptr m_Kernel;
        ECC::Scalar::Native m_PeerSignature;

        mutable boost::optional<Merkle::Hash> m_KernelID;
        io::AsyncEvent::Ptr m_AsyncCompletedEvent;

        bool m_CreatingInputs = false;
        bool m_CreatingInputsShielded = false;
        bool m_CreatingOutputs = false;
        bool m_Signing = false;

        struct ShieldedInputContext;
    };
}
