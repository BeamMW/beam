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

        if (lockTxState == State::SharedUTXOProofPart2)
        {
            lockTxBuilder->SharedUTXOProofPart3(isSender);
            SendBulletProofPart3(*lockTxBuilder, isSender);
            SetState(State::SharedUTXOProofPart3, SubTxIndex::LOCK_TX);

            if (isSender)
            {
                // TEST TX
                auto tx = lockTxBuilder->CreateTransaction();
                beam::TxBase::Context ctx;
                tx->IsValid(ctx);
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
        SharedTxBuilder refundTxBuilder{ *this, SubTxIndex::REFUND_TX, refundAmount, refundFee };

        // send invite to get 
        if (!refundTxBuilder.GetInitialTxParams() && refundTxState == State::Initial)
        {
            // TODO: implement separate version
            if (CheckExpired())
            {
                return;
            }

            refundTxBuilder.InitTx(isSender, false);
        }

        refundTxBuilder.CreateKernel();

        if (!refundTxBuilder.GetPeerPublicExcessAndNonce())
        {
            if (refundTxState == State::Initial && isSender)
            {
                SendSharedTxInvitation(refundTxBuilder);
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
                ConfirmSharedTxInvitation(refundTxBuilder);
            }
            else
            {
                return;
            }
        }
        else
        {
            assert(isSender);

            if (!refundTxBuilder.IsPeerSignatureValid())
            {
                LOG_INFO() << GetTxID() << " Peer signature is invalid.";
                return;
            }

            refundTxBuilder.FinalizeSignature();

            // TEST TX
            auto tx = refundTxBuilder.CreateTransaction();
            beam::TxBase::Context ctx;
            tx->IsValid(ctx);
        }

        // Create RedeemTX
        State redeemTxState = GetState(SubTxIndex::REDEEM_TX);
        // TODO: calculating fee!
        Amount redeemFee = 0;
        Amount redeemAmount = amount - redeemFee;
        SharedTxBuilder redeemTxBuilder{ *this, SubTxIndex::REDEEM_TX, redeemAmount, redeemFee };

        // send invite to get 
        if (!redeemTxBuilder.GetInitialTxParams() && redeemTxState == State::Initial)
        {
            // TODO: implement separate version
            if (CheckExpired())
            {
                return;
            }

            redeemTxBuilder.InitTx(!isSender, true);
        }

        redeemTxBuilder.CreateKernel();

        if (!redeemTxBuilder.GetPeerPublicExcessAndNonce())
        {
            if (redeemTxState == State::Initial && !isSender)
            {
                // send invitation with LockImage
                SendSharedTxInvitation(redeemTxBuilder, true);
                SetState(State::Invitation, SubTxIndex::REDEEM_TX);
            }
            return;
        }

        redeemTxBuilder.SignPartial();

        if (!redeemTxBuilder.GetPeerSignature())
        {
            if (redeemTxState == State::Initial && isSender)
            {
                // invited participant
                assert(IsInitiator());
                ConfirmSharedTxInvitation(redeemTxBuilder);
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
            .AddParameter(TxParameterID::PeerProtoVersion, s_ProtoVersion)
            .AddParameter(TxParameterID::SubTxIndex, SubTxIndex::LOCK_TX)
            .AddParameter(TxParameterID::MinHeight, lockBuilder.GetMinHeight())
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
            .AddParameter(TxParameterID::PeerOffset, lockBuilder.GetOffset())
            .AddParameter(TxParameterID::PeerPublicSharedBlindingFactor, lockBuilder.GetPublicSharedBlindingFactor());
        if (isSender)
        {
            auto proofPartialMultiSig = lockBuilder.GetProofPartialMultiSig();
            msg.AddParameter(TxParameterID::PeerSharedBulletProofMSig, proofPartialMultiSig);
        }
        else
        {
            auto bulletProof = lockBuilder.GetSharedProof();
            msg.AddParameter(TxParameterID::PeerPublicExcess, lockBuilder.GetPublicExcess())
                .AddParameter(TxParameterID::PeerPublicNonce, lockBuilder.GetPublicNonce())
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

    void AtomicSwapTransaction::SendSharedTxInvitation(const BaseTxBuilder& builder, bool shouldSendLockImage /*= false*/)
    {
        SetTxParameter msg;
        msg.AddParameter(TxParameterID::SubTxIndex, builder.GetSubTxID())
            .AddParameter(TxParameterID::Amount, builder.GetAmount())
            .AddParameter(TxParameterID::Fee, builder.GetFee())
            .AddParameter(TxParameterID::MinHeight, builder.GetMinHeight())
            .AddParameter(TxParameterID::PeerPublicExcess, builder.GetPublicExcess())
            .AddParameter(TxParameterID::PeerPublicNonce, builder.GetPublicNonce());
    
        if (shouldSendLockImage)
        {
            msg.AddParameter(TxParameterID::PeerLockImage, builder.GetLockImage());
        }

        if (!SendTxParameters(move(msg)))
        {
            OnFailed(TxFailureReason::FailedToSendParameters, false);
        }
    }

    void AtomicSwapTransaction::ConfirmSharedTxInvitation(const BaseTxBuilder& builder)
    {
        SetTxParameter msg;
        msg.AddParameter(TxParameterID::SubTxIndex, builder.GetSubTxID())
            .AddParameter(TxParameterID::PeerPublicExcess, builder.GetPublicExcess())
            .AddParameter(TxParameterID::PeerSignature, builder.GetPartialSignature())
            .AddParameter(TxParameterID::PeerPublicNonce, builder.GetPublicNonce())
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

    void LockTxBuilder::AddSharedOutput()
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
            RangeProof::Confidential::GenerateSeed(m_SharedSeed.V, m_SharedBlindingFactor, GetAmount(), oracle);
            m_Tx.SetParameter(TxParameterID::SharedSeed, m_SharedSeed.V, m_SubTxID);
        }
        else
        {
            // load remaining shared parameters
            m_Tx.GetParameter(TxParameterID::SharedSeed, m_SharedSeed.V, m_SubTxID);
            m_Tx.GetParameter(TxParameterID::SharedCoinID, m_SharedCoin.m_ID, m_SubTxID);
            m_Tx.GetParameter(TxParameterID::SharedBulletProof, m_SharedProof, m_SubTxID);
        }

        ECC::Scalar::Native blindingFactor = -m_SharedBlindingFactor;
        m_Offset += blindingFactor;
    }

    Transaction::Ptr LockTxBuilder::CreateTransaction()
    {
        AddSharedOutput();
        LoadPeerOffset();
        return BaseTxBuilder::CreateTransaction();
    }

    const ECC::uintBig& LockTxBuilder::GetSharedSeed() const
    {
        return m_SharedSeed.V;
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

    SharedTxBuilder::SharedTxBuilder(BaseTransaction& tx, SubTxID subTxID, Amount amount, Amount fee)
        : BaseTxBuilder(tx, subTxID, { amount }, fee)
    {
    }

    Transaction::Ptr SharedTxBuilder::CreateTransaction()
    {
        LoadPeerOffset();
        return BaseTxBuilder::CreateTransaction();
    }

    void SharedTxBuilder::InitTx(bool isTxOwner, bool shouldInitSecret)
    {
        if (isTxOwner)
        {
            if (shouldInitSecret)
            {
                // init secret (preimage)
                InitSecret();
            }

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
            InitOffset();
        }
    }

    void SharedTxBuilder::InitInputAndOutputs()
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

    void SharedTxBuilder::InitOffset()
    {
        ECC::Scalar::Native blindingFactor = m_Tx.GetMandatoryParameter<Scalar::Native>(TxParameterID::SharedBlindingFactor, AtomicSwapTransaction::SubTxIndex::LOCK_TX);
        m_Offset += blindingFactor;
        m_Tx.SetParameter(TxParameterID::Offset, m_Offset, false, m_SubTxID);
    }

    void SharedTxBuilder::InitSecret()
    {
        uintBig preimage;
        GenRandom(preimage);
        m_Tx.SetParameter(TxParameterID::PreImage, preimage, false, m_SubTxID);
    }

    void SharedTxBuilder::LoadPeerOffset()
    {
        m_Tx.GetParameter(TxParameterID::PeerOffset, m_PeerOffset, m_SubTxID);
    }
} // namespace