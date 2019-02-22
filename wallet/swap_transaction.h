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

#include "wallet_transaction.h"

namespace beam::wallet
{
    class LockTxBuilder;

    class AtomicSwapTransaction : public BaseTransaction
    {
        enum State : uint8_t
        {
            Initial,
            Invitation,
            SharedUTXOProofPart2,
            SharedUTXOProofPart3,

            InvitationConfirmation

        };

        enum SubTxIndex : SubTxID
        {
            LOCK_TX = 2,
            REFUND_TX = 3,
            REDEEM_TX = 4
        };

    public:
        AtomicSwapTransaction(INegotiatorGateway& gateway
                            , beam::IWalletDB::Ptr walletDB
                            , const TxID& txID);
    private:
        TxType GetType() const override;
        State GetState(SubTxID subTxID) const;
        void UpdateImpl() override;
        void SendInvitation(const LockTxBuilder& lockBuilder, bool isSender);
        void SendBulletProofPart2(const LockTxBuilder& lockBuilder, bool isSender);
        void SendBulletProofPart3(const LockTxBuilder& lockBuilder, bool isSender);

        std::unique_ptr<LockTxBuilder> m_lockTxBuilder;
    };

    class LockTxBuilder
    {
    public:
        LockTxBuilder(AtomicSwapTransaction& tx, SubTxID subTxID, Amount amount, Amount fee);

        void SelectInputs();
        void AddChangeOutput();
        void AddOutput(Amount amount, bool bChange);
        bool FinalizeOutputs();
        Output::Ptr CreateOutput(Amount amount, bool bChange, bool shared = false, Height incubation = 0);
        void CreateKernel();
        ECC::Point::Native GetPublicExcess() const;
        ECC::Point::Native GetPublicNonce() const;
        bool GetInitialTxParams();
        bool GetPeerPublicExcessAndNonce();
        bool GetPeerSignature();
        bool GetPeerInputsAndOutputs();
        void FinalizeSignature();
        Transaction::Ptr CreateTransaction();
        void SignPartial();
        bool IsPeerSignatureValid() const;

        Amount GetAmount() const;
        Amount GetFee() const;
        Height GetMinHeight() const;
        Height GetMaxHeight() const;
        const std::vector<Input::Ptr>& GetInputs() const;
        const std::vector<Output::Ptr>& GetOutputs() const;
        const ECC::Scalar::Native& GetOffset() const;
        const ECC::Scalar::Native& GetPartialSignature() const;
        const TxKernel& GetKernel() const;
        void StoreKernelID();
        std::string GetKernelIDString() const;

        void GenerateSharedBlindingFactor();

        void SharedUTXOProofPart2(bool shouldProduceMultisig);
        void SharedUTXOProofPart3(bool shouldProduceMultisig);

        const ECC::uintBig& GetSharedSeed() const;
        const ECC::Scalar::Native& GetSharedBlindingFactor() const;
        const ECC::RangeProof::Confidential::Part2& GetPart2() const;
        const ECC::RangeProof::Confidential::Part3& GetPart3() const;
        const ECC::RangeProof::Confidential& GetBulletProof() const;

        const ECC::RangeProof::Confidential::MultiSig& GetProofPartialMultiSig() const;

        ECC::Point::Native GetPublicSharedBlindingFactor() const;

        void ValidateSharedUTXO(bool shouldProduceMultisig);

    private:
        BaseTransaction& m_Tx;
        SubTxID m_subTxID;

        // input
        Amount m_Amount;
        Amount m_Fee;
        Amount m_Change;
        Height m_MinHeight;
        Height m_MaxHeight;
        std::vector<Input::Ptr> m_Inputs;
        std::vector<Output::Ptr> m_Outputs;
        ECC::Scalar::Native m_BlindingExcess; // goes to kernel
        ECC::Scalar::Native m_Offset; // goes to offset

        ECC::Scalar::Native m_SharedBlindingFactor;

        // peer values
        ECC::Scalar::Native m_PartialSignature;
        ECC::Point::Native m_PeerPublicNonce;
        ECC::Point::Native m_PeerPublicExcess;
        std::vector<Input::Ptr> m_PeerInputs;
        std::vector<Output::Ptr> m_PeerOutputs;
        ECC::Scalar::Native m_PeerOffset;

        // deduced values, 
        TxKernel::Ptr m_Kernel;
        ECC::Scalar::Native m_PeerSignature;
        ECC::Hash::Value m_Message;
        ECC::Signature::MultiSig m_MultiSig;

        // NoLeak - ?
        ECC::uintBig m_Seed;

        ECC::Key::IDV m_SharedCoinID;
        ECC::RangeProof::CreatorParams m_CreatorParams;
        ECC::RangeProof::Confidential m_Bulletproof;
        ECC::RangeProof::Confidential::MultiSig m_ProofPartialMultiSig;
    };
}
