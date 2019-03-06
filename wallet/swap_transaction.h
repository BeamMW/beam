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

    class AtomicSwapTransaction : public BaseTransaction
    {
        enum State : uint8_t
        {
            BuildingLockTX,
            BuildingRefundTX,
            BuildingRedeemTX,

            SendingLockTX,
            SendingRefundTX,
            SendingRedeemTX,

            CompleteSwap,
        };

        enum SubTxState : uint8_t
        {
            Initial,
            Invitation,
            SharedUTXOProofPart2,
            SharedUTXOProofDone,
            Constructed,

            InvitationConfirmation,
            Registration,
            KernelConfirmation
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

        bool SetRegisteredStatus(Transaction::Ptr transaction, bool isRegistered) override;

    private:
        TxType GetType() const override;
        State GetState(SubTxID subTxID) const;
        SubTxState GetSubTxState(SubTxID subTxID) const;
        void UpdateImpl() override;
        void SendInvitation(const LockTxBuilder& lockBuilder, bool isSender);
        void SendBulletProofPart2(const LockTxBuilder& lockBuilder, bool isSender);
        void SendBulletProofPart3(const LockTxBuilder& lockBuilder, bool isSender);

        void SendSharedTxInvitation(const BaseTxBuilder& builder, bool shouldSendLockImage = false);
        void ConfirmSharedTxInvitation(const BaseTxBuilder& builder);

        SubTxState BuildLockTx();
        SubTxState BuildRefundTx();
        SubTxState BuildRedeemTx();

        bool SendSubTx(Transaction::Ptr transaction, SubTxID subTxID);

        bool IsBeamLockTimeExpired() const;

        // wait SubTX in BEAM chain(request kernel proof), returns true if got kernel proof
        bool IsSubTxCompleted(SubTxID subTxID) const;

        bool GetPreimageFromChain(ECC::uintBig& preimage) const;

        Amount GetAmount() const;
        bool IsSender() const;

        mutable boost::optional<bool> m_IsSender;
        mutable boost::optional<beam::Amount> m_Amount;

        Transaction::Ptr m_LockTx;
        Transaction::Ptr m_RefundTx;
        Transaction::Ptr m_RedeemTx;
    };

    class LockTxBuilder: public BaseTxBuilder
    {
    public:
        LockTxBuilder(BaseTransaction& tx, Amount amount, Amount fee);

        Transaction::Ptr CreateTransaction() override;

        void LoadSharedParameters();
        void SharedUTXOProofPart2(bool shouldProduceMultisig);
        void SharedUTXOProofPart3(bool shouldProduceMultisig);

        const ECC::RangeProof::Confidential& GetSharedProof() const;
        const ECC::RangeProof::Confidential::MultiSig& GetProofPartialMultiSig() const;
        ECC::Point::Native GetPublicSharedBlindingFactor() const;

    private:

        void AddSharedOutput();
        void LoadPeerOffset();

        const ECC::uintBig& GetSharedSeed() const;
        const ECC::Scalar::Native& GetSharedBlindingFactor() const;
        const ECC::RangeProof::CreatorParams& GetProofCreatorParams();

        ECC::Point::Native GetSharedCommitment();

        ECC::Scalar::Native m_SharedBlindingFactor;
        ECC::NoLeak<ECC::uintBig> m_SharedSeed;
        beam::Coin m_SharedCoin;
        ECC::RangeProof::Confidential m_SharedProof;

        // deduced values, 
        boost::optional<ECC::RangeProof::CreatorParams> m_CreatorParams;
        ECC::RangeProof::Confidential::MultiSig m_ProofPartialMultiSig;
    };

    class SharedTxBuilder : public BaseTxBuilder
    {
    public:
        SharedTxBuilder(BaseTransaction& tx, SubTxID subTxID, Amount amount, Amount fee);

        void InitTx(bool isTxOwner, bool shouldInitSecret);
        Transaction::Ptr CreateTransaction() override;

        bool GetSharedParameters();

    protected:

        void InitInputAndOutputs();
        void InitOffset();
        void InitSecret();
        void LoadPeerOffset();


        ECC::Scalar::Native m_SharedBlindingFactor;
        ECC::Point::Native m_PeerPublicSharedBlindingFactor;
    };
}
