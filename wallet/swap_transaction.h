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

#include "base_transaction.h"
#include "base_tx_builder.h"


namespace beam::wallet
{
    class LockTxBuilder;
    class RefundTxBuilder;

    class AtomicSwapTransaction : public BaseTransaction
    {
        enum State : uint8_t
        {
            Initial,
            Invitation,
            SharedUTXOProofPart2,
            SharedUTXOProofPart3,
            SharedUTXOProofDone,
            Constructed,

            InvitationConfirmation

        };

    public:
        enum SubTxIndex : SubTxID
        {
            LOCK_TX = 2,
            REFUND_TX = 3,
            REDEEM_TX = 4
        };

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

        void SendInvitation(const RefundTxBuilder& lockBuilder, bool isSender);
        void ConfirmInvitation(const RefundTxBuilder& builder);
    };

    class LockTxBuilder: public BaseTxBuilder
    {
    public:
        LockTxBuilder(AtomicSwapTransaction& tx, Amount amount, Amount fee);

        void AddSharedOutput(Amount amount);

        void LoadSharedParameters();
        void SharedUTXOProofPart2(bool shouldProduceMultisig);
        void SharedUTXOProofPart3(bool shouldProduceMultisig);

        const ECC::uintBig& GetSharedSeed() const;
        const ECC::Scalar::Native& GetSharedBlindingFactor() const;
        const ECC::RangeProof::Confidential& GetSharedProof() const;
        const ECC::RangeProof::Confidential::MultiSig& GetProofPartialMultiSig() const;
        ECC::Point::Native GetPublicSharedBlindingFactor() const;

        void LoadPeerOffset();

    private:

        ECC::Point::Native GetSharedCommitment();
        const ECC::RangeProof::CreatorParams& GetProofCreatorParams();

        ECC::Scalar::Native m_SharedBlindingFactor;
        // NoLeak - ?
        ECC::uintBig m_SharedSeed;
        beam::Coin m_SharedCoin;
        ECC::RangeProof::Confidential m_SharedProof;

        // deduced values, 
        boost::optional<ECC::RangeProof::CreatorParams> m_CreatorParams;
        ECC::RangeProof::Confidential::MultiSig m_ProofPartialMultiSig;
    };

    class RefundTxBuilder : public BaseTxBuilder
    {
    public:
        RefundTxBuilder(AtomicSwapTransaction& tx, Amount amount, Amount fee);

        void InitRefundTx(bool isSender);
        void LoadPeerOffset();
    private:
        void InitInputAndOutputs();
    };
}
