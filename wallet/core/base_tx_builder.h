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
    class BaseTxBuilder
        :public std::enable_shared_from_this<BaseTxBuilder>
    {
    public:
        BaseTxBuilder(BaseTransaction& tx, SubTxID subTxID);
        virtual ~BaseTxBuilder() = default;

        BaseTransaction& m_Tx;
        SubTxID m_SubTxID;

        HeightRange m_Height;

        struct Coins
        {
            std::vector<Coin::ID> m_Input;
            std::vector<Coin::ID> m_Output;
            std::vector<IPrivateKeyKeeper2::ShieldedInput> m_InputShielded;

            bool IsEmpty() const {
                return m_Input.empty() && m_Output.empty() && m_InputShielded.empty();
            }

            void AddOffset(ECC::Scalar::Native&, Key::IKdf::Ptr& pMasterKdf) const;

        } m_Coins;

        Transaction::Ptr m_pTransaction;

        struct Balance
        {
            struct Entry
            {
                Amount m_In = 0;
                Amount m_Out = 0;

                bool IsEnoughNetTx(Amount) const;
            };

            typedef std::map<Asset::ID, Entry> Map;

            Map m_Map;
            Amount m_Fees = 0;

            void Add(const Coin::ID&, bool bOutp);
            void Add(const IPrivateKeyKeeper2::ShieldedInput&);

        } m_Balance;

        void RefreshBalance();

        Amount MakeInputs(Amount, Asset::ID); // make the balance (outs - ins) at least this amount. Returns actual
        Amount MakeInputsAndChange(Amount, Asset::ID); // same as above, auto creates a change if necessary
        void SaveCoins();

        void AddOutput(const Coin::ID&);
        void CreateAddNewOutput(Coin::ID&);

        bool VerifyTx();

        void GenerateInOuts();

        enum struct Stage {
            None,
            Done,
            InProgress
        };

        Stage m_GeneratingInOuts = Stage::None;
        Stage m_Signing = Stage::None;

        bool IsGeneratingInOuts() const;
        bool IsSigning() const;

        Amount m_Fee = 0;

        void VerifyAssetsEnabled(); // throws exc if disabled

    protected:

        void MakeInputs(Balance::Entry&, Amount, Asset::ID); // make the balance (outs - ins) at least this amount. Returns actual

        struct KeyKeeperHandler
            :public IPrivateKeyKeeper2::Handler
        {
            std::weak_ptr<BaseTxBuilder> m_pBuilder;
            Stage* m_pStage;

            KeyKeeperHandler(BaseTxBuilder&, Stage& s);
            ~KeyKeeperHandler();

            virtual void OnDone(IPrivateKeyKeeper2::Status::Type) override;

            virtual void OnSuccess(BaseTxBuilder&) = 0;
            virtual void OnFailed(BaseTxBuilder&, IPrivateKeyKeeper2::Status::Type);

            void Detach(BaseTxBuilder&, Stage);
            void OnAllDone(BaseTxBuilder&);
        };

        void SetInOuts(IPrivateKeyKeeper2::Method::InOuts&);
        void SetCommon(IPrivateKeyKeeper2::Method::TxCommon&);

        template<typename T1, typename T2>
        void SaveAndStore(T2& dest, TxParameterID parameterID, const T1& source)
        {
            dest = source;
            m_Tx.SetParameter(parameterID, dest, m_SubTxID);
        }

        void GenerateInputsStd();
        void GenerateInputsShielded();

        struct HandlerInOuts;
    };

    class MutualTxBuilder : public BaseTxBuilder
    {
    public:
        MutualTxBuilder(BaseTransaction& tx, SubTxID subTxID, const AmountList& amount, Amount fee);
        virtual ~MutualTxBuilder() = default;
        void MakeInputsAndChanges();
        void GenerateAssetCoin(Amount amount, bool change);
        void GenerateBeamCoin(Amount amount, bool change);
        bool LoadKernel();
        bool HasKernelID() const;
        void CreateKernel();
        virtual ECC::Point::Native GetPublicExcess() const;
        ECC::Point::Native GetPublicNonce() const;
        Asset::ID GetAssetId() const;
        bool IsAssetTx() const;
        bool GetPeerPublicExcessAndNonce();
        bool GetPeerSignature();
        bool GetPeerInputsAndOutputs();
        void FinalizeSignature();
        void FinalizeInputs();
        virtual Transaction::Ptr CreateTransaction();
        void SignSender(bool initial, bool bIsConventional = true);
        void SignReceiver(bool bIsConventional = true);
        void SignSplit();
        bool IsPeerSignatureValid() const;

        Amount GetAmount() const;
        const AmountList& GetAmountList() const;
        Amount GetFee() const;
        Height GetLifetime() const;
        Height GetMinHeight() const;
        virtual Height GetMaxHeight() const;
        const ECC::Scalar::Native& GetPartialSignature() const;
        const TxKernel& GetKernel() const;
        const Merkle::Hash& GetKernelID() const;
        void StoreKernelID();
        void ResetKernelID();
        std::string GetKernelIDString() const;
        bool UpdateMaxHeight();
        bool IsAcceptableMaxHeight(Height) const;
        ECC::Hash::Value GetLockImage() const;
        SubTxID GetSubTxID() const;

    private:
        Amount GetMinimumFee() const;
        void CheckMinimumFee();
        void SignReceiverOrSplit(bool bFromYourself, bool bIsConventional);

    protected:

        // input
        AmountList m_AmountList;
        Height m_Lifetime;

        ECC::Point::Native m_PublicNonce;
        ECC::Point::Native m_PublicExcess;

        // peer values
        ECC::Scalar::Native m_PartialSignature;
        ECC::Point::Native m_PeerPublicNonce;
        ECC::Point::Native m_PeerPublicExcess;
        std::vector<Input::Ptr> m_PeerInputs;
        std::vector<Output::Ptr> m_PeerOutputs;
        ECC::Scalar::Native m_PeerOffset;

        // deduced values,
        TxKernelStd::Ptr m_Kernel;
        ECC::Scalar::Native m_PeerSignature;

        mutable boost::optional<Merkle::Hash> m_KernelID;
    };
}
