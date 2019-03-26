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
        enum class State : uint8_t
        {
            Initial,
            Invitation,

            BuildingBeamLockTX,
            BuildingBeamRefundTX,
            BuildingBeamRedeemTX,

            HandlingContractTX,
            SendingRefundTX,
            SendingRedeemTX,

            SendingBeamLockTX,
            SendingBeamRefundTX,
            SendingBeamRedeemTX,

            CompleteSwap,
        };

        enum class SubTxState : uint8_t
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

        enum class SwapTxState : uint8_t
        {
            Initial,
            CreatingTx,
            SigningTx,
            Constructed
        };

    public:
        enum SubTxIndex : SubTxID
        {
            BEAM_LOCK_TX = 2,
            BEAM_REFUND_TX = 3,
            BEAM_REDEEM_TX = 4,
            LOCK_TX = 5,
            REFUND_TX = 6,
            REDEEM_TX = 7
        };

        AtomicSwapTransaction(INegotiatorGateway& gateway
                            , beam::IWalletDB::Ptr walletDB
                            , const TxID& txID);

    private:
        void SetNextState(State state);
        void UpdateAsync();

        TxType GetType() const override;
        State GetState(SubTxID subTxID) const;
        SubTxState GetSubTxState(SubTxID subTxID) const;
        void UpdateImpl() override;
        void SendInvitation();
        void SendExternalTxDetails();
        void SendLockTxInvitation(const LockTxBuilder& lockBuilder);
        void SendMultiSigProofPart2(const LockTxBuilder& lockBuilder, bool isMultiSigProofOwner);
        void SendMultiSigProofPart3(const LockTxBuilder& lockBuilder, bool isMultiSigProofOwner);

        void SendSharedTxInvitation(const BaseTxBuilder& builder, bool shouldSendLockImage = false);
        void ConfirmSharedTxInvitation(const BaseTxBuilder& builder);


        SubTxState BuildBeamLockTx();
        SubTxState BuildBeamRefundTx();
        SubTxState BuildBeamRedeemTx();

        //
        SwapTxState BuildLockTx();
        SwapTxState BuildWithdrawTx(SubTxID subTxID);
        bool RegisterExternalTx(const std::string& rawTransaction, SubTxID subTxID);
        void GetSwapLockTxConfirmations();

        void OnGetRawChangeAddress(const std::string& response);
        void OnFundRawTransaction(const std::string& response);
        void OnSignLockTransaction(const std::string& response);
        void OnCreateWithdrawTransaction(const std::string& response);
        void OnDumpPrivateKey(SubTxID subTxID, const std::string& response);
        void OnGetSwapLockTxConfirmations(const std::string& response);

        bool SendSubTx(Transaction::Ptr transaction, SubTxID subTxID);

        bool IsBeamLockTimeExpired() const;

        // wait SubTX in BEAM chain(request kernel proof), returns true if got kernel proof
        bool CompleteSubTx(SubTxID subTxID);

        bool GetPreimageFromChain(ECC::uintBig& preimage, SubTxID subTxID) const;

        Amount GetAmount() const;
        bool IsSender() const;
        bool IsBeamSide() const;

        mutable boost::optional<bool> m_IsBeamSide;
        mutable boost::optional<bool> m_IsSender;
        mutable boost::optional<beam::Amount> m_Amount;

        Transaction::Ptr m_LockTx;
        Transaction::Ptr m_RefundTx;
        Transaction::Ptr m_RedeemTx;

        io::AsyncEvent::Ptr m_EventToUpdate;

        // TODO: make a separate struct
        // btc additional params
        uint16_t m_SwapLockTxConfirmations = 0;
        boost::optional<std::string> m_SwapLockRawTx;
        boost::optional<std::string> m_SwapWithdrawRawTx;
    };

    class LockTxBuilder: public BaseTxBuilder
    {
    public:
        LockTxBuilder(BaseTransaction& tx, Amount amount, Amount fee);

        Transaction::Ptr CreateTransaction() override;

        void LoadSharedParameters();
        bool SharedUTXOProofPart2(bool shouldProduceMultisig);
        bool SharedUTXOProofPart3(bool shouldProduceMultisig);

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

        void InitTx(bool isTxOwner);
        Transaction::Ptr CreateTransaction() override;

        bool GetSharedParameters();

    protected:

        void InitInput();
        void InitOutput();

        void InitOffset();
        void LoadPeerOffset();


        ECC::Scalar::Native m_SharedBlindingFactor;
        ECC::Point::Native m_PeerPublicSharedBlindingFactor;
    };
}
