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

#include "swap_transaction.h"

using namespace std;
using namespace ECC;

namespace beam::wallet
{
    AtomicSwapTransaction::AtomicSwapTransaction(INegotiatorGateway& gateway
                                               , beam::IWalletDB::Ptr walletDB
                                               , const TxID& txID)
        : BaseTransaction(gateway, walletDB, txID)
    {

    }

    TxType AtomicSwapTransaction::GetType() const
    {
        return TxType::AtomicSwap;
    }

    AtomicSwapTransaction::State AtomicSwapTransaction::GetState(SubTxID subTxID) const
    {
        State state = State::Initial;
        GetParameter(TxParameterID::State, state, subTxID);
        return state;
    }

    void AtomicSwapTransaction::UpdateImpl()
    {
        bool isSender = GetMandatoryParameter<bool>(TxParameterID::IsSender);
        State lockTxState = GetState(SubTxIndex::LOCK_TX);
        Amount amount = GetMandatoryParameter<Amount>(TxParameterID::Amount);
        auto lockTxBuilder = std::make_unique<LockTxBuilder>(*this, amount, GetMandatoryParameter<Amount>(TxParameterID::Fee));

        if (!lockTxBuilder->GetInitialTxParams() && lockTxState == State::Initial)
        {
            if (CheckExpired())
            {
                return;
            }

            if (isSender)
            {
                lockTxBuilder->SelectInputs();
                lockTxBuilder->AddChangeOutput();
            }

            if (!lockTxBuilder->FinalizeOutputs())
            {
                // TODO: transaction is too big :(
            }

            UpdateTxDescription(TxStatus::InProgress);
        }

        lockTxBuilder->CreateKernel();

        if (!lockTxBuilder->GetPeerPublicExcessAndNonce())
        {
            assert(IsInitiator());
            if (lockTxState == State::Initial)
            {
                SendInvitation(*lockTxBuilder, isSender);
                SetState(State::Invitation, SubTxIndex::LOCK_TX);
            }
            return;
        }

        lockTxBuilder->LoadSharedParameters();
        lockTxBuilder->SignPartial();

        if (lockTxState == State::Initial || lockTxState == State::Invitation)
        {
            lockTxBuilder->SharedUTXOProofPart2(isSender);
            SendBulletProofPart2(*lockTxBuilder, isSender);
            SetState(State::SharedUTXOProofPart2, SubTxIndex::LOCK_TX);
            return;
        }

        assert(lockTxBuilder->GetPeerSignature());
        if (!lockTxBuilder->IsPeerSignatureValid())
        {
            LOG_INFO() << GetTxID() << " Peer signature is invalid.";
            return;
        }

        lockTxBuilder->FinalizeSignature();
        // TODO: do it later
        lockTxBuilder->LoadPeerOffset();

        if (lockTxState == State::SharedUTXOProofPart2)
        {
            lockTxBuilder->SharedUTXOProofPart3(isSender);
            SendBulletProofPart3(*lockTxBuilder, isSender);
            SetState(State::SharedUTXOProofPart3, SubTxIndex::LOCK_TX);

            if (isSender)
            {
                // DEBUG
                // TODO(alex.starun): create shared utxo ?
                lockTxBuilder->AddSharedOutput(amount);

                {
                    // TEST TX
                    auto tx = lockTxBuilder->CreateTransaction();
                    beam::TxBase::Context ctx;
                    tx->IsValid(ctx);
                }
            }
            else
            {
                //return;
            }
        }

        if (lockTxState == State::SharedUTXOProofPart3)
        {
        }

        // Shared UTXO Ready
        // Create RefundTX
        State refundTxState = GetState(SubTxIndex::REFUND_TX);
        // TODO: calculating fee!
        Amount refundFee = 0;
        Amount refundAmount = amount - refundFee;
        RefundTxBuilder refundTxBuilder{ *this, refundAmount, refundFee };

        // send invite to get 
        if (!refundTxBuilder.GetInitialTxParams() && refundTxState == State::Initial)
        {
            // TODO: implement separate version
            if (CheckExpired())
            {
                return;
            }

            refundTxBuilder.InitRefundTx(isSender);
        }

        refundTxBuilder.CreateKernel();

        if (!refundTxBuilder.GetPeerPublicExcessAndNonce())
        {
            //assert(IsInitiator());
            if (refundTxState == State::Initial && isSender)
            {
                SendInvitation(refundTxBuilder, isSender);
                SetState(State::Invitation, SubTxIndex::REFUND_TX);
            }
            return;
        }

        // if !isSender -> validate minHeight
        refundTxBuilder.SignPartial();

        if (!refundTxBuilder.GetPeerSignature())
        {
            if (refundTxState == State::Initial && !isSender)
            {
                // invited participant
                assert(!IsInitiator());
                ConfirmInvitation(refundTxBuilder);
            }
            return;
        }

        assert(refundTxBuilder.GetPeerSignature());
        if (!refundTxBuilder.IsPeerSignatureValid())
        {
            LOG_INFO() << GetTxID() << " Peer signature is invalid.";
            return;
        }

        refundTxBuilder.FinalizeSignature();

        if (isSender)
        {
            // TEST TX
            refundTxBuilder.LoadPeerOffset();
            auto tx = refundTxBuilder.CreateTransaction();
            beam::TxBase::Context ctx;
            tx->IsValid(ctx);
        }

        // Create RedeemTX
        State redeemTxState = GetState(SubTxIndex::REDEEM_TX);
        // TODO: calculating fee!
        Amount redeemFee = 0;
        Amount redeemAmount = amount - redeemFee;
        RedeemTxBuilder redeemTxBuilder{ *this, redeemAmount, redeemFee };

        // send invite to get 
        if (!redeemTxBuilder.GetInitialTxParams() && redeemTxState == State::Initial)
        {
            // TODO: implement separate version
            if (CheckExpired())
            {
                return;
            }

            redeemTxBuilder.InitRedeemTx(isSender);
        }

        redeemTxBuilder.CreateKernel();

        if (!redeemTxBuilder.GetPeerPublicExcessAndNonce())
        {
            assert(IsInitiator());
            if (redeemTxState == State::Initial && !isSender)
            {
                SendInvitation(redeemTxBuilder, isSender);
                SetState(State::Invitation, SubTxIndex::REDEEM_TX);
            }
            return;
        }

        redeemTxBuilder.SignPartial();

        if (!redeemTxBuilder.GetPeerSignature())
        {
            if (redeemTxState == State::Initial)
            {
                // invited participant
                assert(!IsInitiator());
                ConfirmInvitation(redeemTxBuilder);
            }
            return;
        }

        assert(redeemTxBuilder.GetPeerSignature());
        if (!redeemTxBuilder.IsPeerSignatureValid())
        {
            LOG_INFO() << GetTxID() << " Peer signature is invalid.";
            return;
        }

        redeemTxBuilder.FinalizeSignature();

        if (!isSender)
        {
            // TEST TX
            redeemTxBuilder.LoadPeerOffset();
            auto tx = redeemTxBuilder.CreateTransaction();
            beam::TxBase::Context ctx;
            tx->IsValid(ctx);
        }
    }

    void AtomicSwapTransaction::SendInvitation(const LockTxBuilder& lockBuilder, bool isSender)
    {
        Amount atomicSwapAmount = GetMandatoryParameter<Amount>(TxParameterID::AtomicSwapAmount);
        AtomicSwapCoin atomicSwapCoin = GetMandatoryParameter<AtomicSwapCoin>(TxParameterID::AtomicSwapCoin);

        SetTxParameter msg;
        msg.AddParameter(TxParameterID::Amount, lockBuilder.GetAmount())
            .AddParameter(TxParameterID::Fee, lockBuilder.GetFee())
            .AddParameter(TxParameterID::IsSender, !isSender)
            .AddParameter(TxParameterID::AtomicSwapAmount, atomicSwapAmount)
            .AddParameter(TxParameterID::AtomicSwapCoin, atomicSwapCoin)
            .AddParameter(TxParameterID::SubTxIndex, SubTxIndex::LOCK_TX)
            .AddParameter(TxParameterID::MinHeight, lockBuilder.GetMinHeight())
            .AddParameter(TxParameterID::PeerProtoVersion, s_ProtoVersion)
            .AddParameter(TxParameterID::PeerPublicExcess, lockBuilder.GetPublicExcess())
            .AddParameter(TxParameterID::PeerPublicNonce, lockBuilder.GetPublicNonce());

        if (!SendTxParameters(move(msg)))
        {
            OnFailed(TxFailureReason::FailedToSendParameters, false);
        }
    }

    void AtomicSwapTransaction::SendBulletProofPart2(const LockTxBuilder& lockBuilder, bool isSender)
    {
        SetTxParameter msg;
        msg.AddParameter(TxParameterID::SubTxIndex, SubTxIndex::LOCK_TX)
            .AddParameter(TxParameterID::PeerSignature, lockBuilder.GetPartialSignature())
            .AddParameter(TxParameterID::PeerOffset, lockBuilder.GetOffset());
        if (isSender)
        {
            auto proofPartialMultiSig = lockBuilder.GetProofPartialMultiSig();
            msg.AddParameter(TxParameterID::PeerSharedBulletProofMSig, proofPartialMultiSig);
        }
        else
        {
            auto bulletProof = lockBuilder.GetSharedProof();
            msg.AddParameter(TxParameterID::PeerProtoVersion, s_ProtoVersion)
                .AddParameter(TxParameterID::PeerPublicExcess, lockBuilder.GetPublicExcess())
                .AddParameter(TxParameterID::PeerPublicNonce, lockBuilder.GetPublicNonce())
                .AddParameter(TxParameterID::PeerPublicSharedBlindingFactor, lockBuilder.GetPublicSharedBlindingFactor())
                .AddParameter(TxParameterID::PeerSharedBulletProofPart2, bulletProof.m_Part2);
        }

        if (!SendTxParameters(move(msg)))
        {
            OnFailed(TxFailureReason::FailedToSendParameters, false);
        }
    }

    void AtomicSwapTransaction::SendBulletProofPart3(const LockTxBuilder& lockBuilder, bool isSender)
    {
        SetTxParameter msg;

        msg.AddParameter(TxParameterID::SubTxIndex, SubTxIndex::LOCK_TX);
        // if !isSender -> send p3
        // else send full bulletproof? output?

        if (!isSender)
        {
            auto bulletProof = lockBuilder.GetSharedProof();
            msg.AddParameter(TxParameterID::PeerSharedBulletProofPart3, bulletProof.m_Part3);
        }

        if (!SendTxParameters(move(msg)))
        {
            OnFailed(TxFailureReason::FailedToSendParameters, false);
        }
    }

    void AtomicSwapTransaction::SendInvitation(const RefundTxBuilder& refundBuilder, bool isSender)
    {
        SetTxParameter msg;
        msg.AddParameter(TxParameterID::SubTxIndex, SubTxIndex::REFUND_TX)
            .AddParameter(TxParameterID::Amount, refundBuilder.GetAmount())
            .AddParameter(TxParameterID::Fee, refundBuilder.GetFee())
            .AddParameter(TxParameterID::IsSender, !isSender)
            .AddParameter(TxParameterID::MinHeight, refundBuilder.GetMinHeight())
            .AddParameter(TxParameterID::PeerProtoVersion, s_ProtoVersion)
            .AddParameter(TxParameterID::PeerPublicExcess, refundBuilder.GetPublicExcess())
            .AddParameter(TxParameterID::PeerPublicNonce, refundBuilder.GetPublicNonce());

        if (!SendTxParameters(move(msg)))
        {
            OnFailed(TxFailureReason::FailedToSendParameters, false);
        }
    }

    void AtomicSwapTransaction::ConfirmInvitation(const RefundTxBuilder& builder)
    {
        SetTxParameter msg;
        msg.AddParameter(TxParameterID::SubTxIndex, SubTxIndex::REFUND_TX)
            .AddParameter(TxParameterID::PeerProtoVersion, s_ProtoVersion)
            .AddParameter(TxParameterID::PeerPublicExcess, builder.GetPublicExcess())
            .AddParameter(TxParameterID::PeerSignature, builder.GetPartialSignature())
            .AddParameter(TxParameterID::PeerPublicNonce, builder.GetPublicNonce())
            .AddParameter(TxParameterID::PeerOffset, builder.GetOffset());

        if (!SendTxParameters(move(msg)))
        {
            OnFailed(TxFailureReason::FailedToSendParameters, false);
        }
    }

    void AtomicSwapTransaction::SendInvitation(const RedeemTxBuilder& builder, bool isSender)
    {
        SetTxParameter msg;
        msg.AddParameter(TxParameterID::SubTxIndex, SubTxIndex::REDEEM_TX)
            //.AddParameter(TxParameterID::Amount, builder.GetAmount())
            .AddParameter(TxParameterID::Fee, builder.GetFee())
            .AddParameter(TxParameterID::IsSender, !isSender) // ???
            .AddParameter(TxParameterID::MinHeight, builder.GetMinHeight())
            .AddParameter(TxParameterID::PeerProtoVersion, s_ProtoVersion)
            .AddParameter(TxParameterID::PeerPublicExcess, builder.GetPublicExcess())
            .AddParameter(TxParameterID::PeerPublicNonce, builder.GetPublicNonce());

        if (!SendTxParameters(move(msg)))
        {
            OnFailed(TxFailureReason::FailedToSendParameters, false);
        }
    }

    void AtomicSwapTransaction::ConfirmInvitation(const RedeemTxBuilder& builder)
    {
        SetTxParameter msg;
        msg.AddParameter(TxParameterID::SubTxIndex, SubTxIndex::REDEEM_TX)
            .AddParameter(TxParameterID::PeerProtoVersion, s_ProtoVersion)
            .AddParameter(TxParameterID::PeerPublicExcess, builder.GetPublicExcess())
            .AddParameter(TxParameterID::PeerSignature, builder.GetPartialSignature())
            .AddParameter(TxParameterID::PeerPublicNonce, builder.GetPublicNonce())
            .AddParameter(TxParameterID::PeerLockImage, builder.GetLockImage())
            .AddParameter(TxParameterID::PeerOffset, builder.GetOffset());

        if (!SendTxParameters(move(msg)))
        {
            OnFailed(TxFailureReason::FailedToSendParameters, false);
        }
    }

    LockTxBuilder::LockTxBuilder(BaseTransaction& tx, Amount amount, Amount fee)
        : BaseTxBuilder(tx, AtomicSwapTransaction::SubTxIndex::LOCK_TX, {amount}, fee)
    {
    }

    void LockTxBuilder::LoadPeerOffset()
    {
        m_Tx.GetParameter(TxParameterID::PeerOffset, m_PeerOffset, m_SubTxID);
    }

    void LockTxBuilder::SharedUTXOProofPart2(bool shouldProduceMultisig)
    {
        if (shouldProduceMultisig)
        {
            Oracle oracle;
            oracle << (beam::Height)0; // CHECK, coin maturity
            // load peer part2
            m_Tx.GetParameter(TxParameterID::PeerSharedBulletProofPart2, m_SharedProof.m_Part2, m_SubTxID);
            // produce multisig
            m_SharedProof.CoSign(GetSharedSeed(), GetSharedBlindingFactor(), GetProofCreatorParams(), oracle, RangeProof::Confidential::Phase::Step2, &m_ProofPartialMultiSig);

            // save SharedBulletProofMSig and BulletProof ?
            m_Tx.SetParameter(TxParameterID::SharedBulletProof, m_SharedProof, m_SubTxID);
        }
        else
        {
            ZeroObject(m_SharedProof.m_Part2);
            RangeProof::Confidential::MultiSig::CoSignPart(GetSharedSeed(), m_SharedProof.m_Part2);
        }
    }

    void LockTxBuilder::SharedUTXOProofPart3(bool shouldProduceMultisig)
    {
        if (shouldProduceMultisig)
        {
            Oracle oracle;
            oracle << (beam::Height)0; // CHECK!
            // load peer part3
            m_Tx.GetParameter(TxParameterID::PeerSharedBulletProofPart3, m_SharedProof.m_Part3, m_SubTxID);
            // finalize proof
            m_SharedProof.CoSign(GetSharedSeed(), GetSharedBlindingFactor(), GetProofCreatorParams(), oracle, RangeProof::Confidential::Phase::Finalize);

            m_Tx.SetParameter(TxParameterID::SharedBulletProof, m_SharedProof, m_SubTxID);
        }
        else
        {
            m_Tx.GetParameter(TxParameterID::PeerSharedBulletProofMSig, m_ProofPartialMultiSig, m_SubTxID);

            ZeroObject(m_SharedProof.m_Part3);
            m_ProofPartialMultiSig.CoSignPart(GetSharedSeed(), GetSharedBlindingFactor(), m_SharedProof.m_Part3);
        }
    }

    void LockTxBuilder::AddSharedOutput(Amount amount)
    {
        Output::Ptr output = make_unique<Output>();
        output->m_Commitment = GetSharedCommitment();
        output->m_pConfidential = std::make_unique<ECC::RangeProof::Confidential>();
        *(output->m_pConfidential) = m_SharedProof;

        m_Outputs.push_back(std::move(output));
    }

    void LockTxBuilder::LoadSharedParameters()
    {
        if (!m_Tx.GetParameter(TxParameterID::SharedBlindingFactor, m_SharedBlindingFactor, m_SubTxID))
        {
            m_SharedCoin = m_Tx.GetWalletDB()->generateSharedCoin(GetAmount());
            m_Tx.SetParameter(TxParameterID::SharedCoinID, m_SharedCoin.m_ID, m_SubTxID);

            // blindingFactor = sk + sk1
            beam::SwitchCommitment switchCommitment;
            switchCommitment.Create(m_SharedBlindingFactor, *m_Tx.GetWalletDB()->get_ChildKdf(m_SharedCoin.m_ID.m_SubIdx), m_SharedCoin.m_ID);
            m_Tx.SetParameter(TxParameterID::SharedBlindingFactor, m_SharedBlindingFactor, m_SubTxID);

            Oracle oracle;
            RangeProof::Confidential::GenerateSeed(m_SharedSeed, m_SharedBlindingFactor, GetAmount(), oracle);
            m_Tx.SetParameter(TxParameterID::SharedSeed, m_SharedSeed, m_SubTxID);
        }
        else
        {
            // load remaining shared parameters
            m_Tx.GetParameter(TxParameterID::SharedSeed, m_SharedSeed, m_SubTxID);
            m_Tx.GetParameter(TxParameterID::SharedCoinID, m_SharedCoin.m_ID, m_SubTxID);
            m_Tx.GetParameter(TxParameterID::SharedBulletProof, m_SharedProof, m_SubTxID);
        }

        ECC::Scalar::Native blindingFactor = -m_SharedBlindingFactor;
        m_Offset += blindingFactor;
    }

    const ECC::uintBig& LockTxBuilder::GetSharedSeed() const
    {
        return m_SharedSeed;
    }

    const ECC::Scalar::Native& LockTxBuilder::GetSharedBlindingFactor() const
    {
        return m_SharedBlindingFactor;
    }

    const ECC::RangeProof::Confidential& LockTxBuilder::GetSharedProof() const
    {
        return m_SharedProof;
    }

    const ECC::RangeProof::Confidential::MultiSig& LockTxBuilder::GetProofPartialMultiSig() const
    {
        return m_ProofPartialMultiSig;
    }

    ECC::Point::Native LockTxBuilder::GetPublicSharedBlindingFactor() const
    {
        return Context::get().G * GetSharedBlindingFactor();
    }

    const ECC::RangeProof::CreatorParams& LockTxBuilder::GetProofCreatorParams()
    {
        if (!m_CreatorParams.is_initialized())
        {
            ECC::RangeProof::CreatorParams creatorParams;
            creatorParams.m_Kidv = m_SharedCoin.m_ID;
            beam::Output::GenerateSeedKid(creatorParams.m_Seed.V, GetSharedCommitment(), *m_Tx.GetWalletDB()->get_MasterKdf());
            m_CreatorParams = creatorParams;
        }
        return m_CreatorParams.get();
    }

    ECC::Point::Native LockTxBuilder::GetSharedCommitment()
    {
        Point::Native commitment(Zero);
        // TODO: check pHGen
        Tag::AddValue(commitment, nullptr, GetAmount());
        commitment += GetPublicSharedBlindingFactor();
        commitment += m_Tx.GetMandatoryParameter<Point::Native>(TxParameterID::PeerPublicSharedBlindingFactor, m_SubTxID);

        return commitment;
    }

    RefundTxBuilder::RefundTxBuilder(BaseTransaction& tx, Amount amount, Amount fee)
        : BaseTxBuilder(tx, AtomicSwapTransaction::SubTxIndex::REFUND_TX, { amount }, fee)
    {
    }

    void RefundTxBuilder::InitRefundTx(bool isSender)
    {
        if (isSender)
        {
            // select shared UTXO as input and create output utxo
            InitInputAndOutputs();

            if (!FinalizeOutputs())
            {
                // TODO: transaction is too big :(
            }
        }
        else
        {
            // init offset
            ECC::Scalar::Native blindingFactor = m_Tx.GetMandatoryParameter<Scalar::Native>(TxParameterID::SharedBlindingFactor, AtomicSwapTransaction::SubTxIndex::LOCK_TX);
            m_Offset += blindingFactor;
            m_Tx.SetParameter(TxParameterID::Offset, m_Offset, false, m_SubTxID);
        }
    }

    void RefundTxBuilder::LoadPeerOffset()
    {
        m_Tx.GetParameter(TxParameterID::PeerOffset, m_PeerOffset, m_SubTxID);
    }

    void RefundTxBuilder::InitInputAndOutputs()
    {
        // load shared utxo as input
        ECC::Scalar::Native blindingFactor = m_Tx.GetMandatoryParameter<Scalar::Native>(TxParameterID::SharedBlindingFactor, AtomicSwapTransaction::SubTxIndex::LOCK_TX);

        // TODO: move it to separate function
        Point::Native commitment(Zero);
        Tag::AddValue(commitment, nullptr, GetAmount());
        commitment += Context::get().G * blindingFactor;
        commitment += m_Tx.GetMandatoryParameter<Point::Native>(TxParameterID::PeerPublicSharedBlindingFactor, AtomicSwapTransaction::SubTxIndex::LOCK_TX);

        auto& input = m_Inputs.emplace_back(make_unique<Input>());
        input->m_Commitment = commitment;
        m_Tx.SetParameter(TxParameterID::Inputs, m_Inputs, false, m_SubTxID);

        m_Offset += blindingFactor;

        // add output
        AddOutput(GetAmount(), false);
    }

    RedeemTxBuilder::RedeemTxBuilder(BaseTransaction& tx, Amount amount, Amount fee)
        : BaseTxBuilder(tx, AtomicSwapTransaction::SubTxIndex::REDEEM_TX, { amount }, fee)
    {
    }

    void RedeemTxBuilder::InitRedeemTx(bool isSender)
    {
        if (!isSender)
        {
            // init secret (preimage)
            uintBig preimage;
            GenRandom(preimage);
            m_Tx.SetParameter(TxParameterID::PreImage, preimage, false, m_SubTxID);

            // select shared UTXO as input and create output utxo
            InitInputAndOutputs();

            if (!FinalizeOutputs())
            {
                // TODO: transaction is too big :(
            }
        }
        else
        {
            // init offset
            ECC::Scalar::Native blindingFactor = m_Tx.GetMandatoryParameter<Scalar::Native>(TxParameterID::SharedBlindingFactor, AtomicSwapTransaction::SubTxIndex::LOCK_TX);
            m_Offset += blindingFactor;
            m_Tx.SetParameter(TxParameterID::Offset, m_Offset, false, m_SubTxID);
        }
    }

    void RedeemTxBuilder::LoadPeerOffset()
    {
        m_Tx.GetParameter(TxParameterID::PeerOffset, m_PeerOffset, m_SubTxID);
    }

    void RedeemTxBuilder::InitInputAndOutputs()
    {
        // load shared utxo as input
        ECC::Scalar::Native blindingFactor = m_Tx.GetMandatoryParameter<Scalar::Native>(TxParameterID::SharedBlindingFactor, AtomicSwapTransaction::SubTxIndex::LOCK_TX);

        // TODO: move it to separate function
        Point::Native commitment(Zero);
        Tag::AddValue(commitment, nullptr, GetAmount());
        commitment += Context::get().G * blindingFactor;
        commitment += m_Tx.GetMandatoryParameter<Point::Native>(TxParameterID::PeerPublicSharedBlindingFactor, AtomicSwapTransaction::SubTxIndex::LOCK_TX);

        auto& input = m_Inputs.emplace_back(make_unique<Input>());
        input->m_Commitment = commitment;
        m_Tx.SetParameter(TxParameterID::Inputs, m_Inputs, false, m_SubTxID);

        m_Offset += blindingFactor;

        // add output
        AddOutput(GetAmount(), false);
    }
} // namespace