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

#include "bitcoin/bitcoin.hpp"

using namespace std;
using namespace ECC;

namespace beam::wallet
{
    namespace
    {
        uint32_t kBeamLockTime = 1440;
    }

    AtomicSwapTransaction::AtomicSwapTransaction(INegotiatorGateway& gateway
                                               , beam::IWalletDB::Ptr walletDB
                                               , const TxID& txID)
        : BaseTransaction(gateway, walletDB, txID)
    {

    }

    bool AtomicSwapTransaction::SetRegisteredStatus(Transaction::Ptr transaction, bool isRegistered)
    {
        Merkle::Hash kernelID;
        transaction->m_vKernels.back()->get_ID(kernelID);

        SubTxIndex subTxID = SubTxIndex::LOCK_TX;
        Merkle::Hash lockTxKernelID = GetMandatoryParameter<Merkle::Hash>(TxParameterID::KernelID, SubTxIndex::LOCK_TX);

        if (kernelID != lockTxKernelID)
        {
            subTxID = IsSender() ? SubTxIndex::REFUND_TX : SubTxIndex::REDEEM_TX;
        }

        return SetParameter(TxParameterID::TransactionRegistered, isRegistered, false, subTxID);
    }

    void AtomicSwapTransaction::SetNextState(State state)
    {
        SetState(state);
        if (!m_EventToUpdate)
        {
            m_EventToUpdate = io::AsyncEvent::create(io::Reactor::get_Current(), [this]() { UpdateImpl(); });
        }

        m_EventToUpdate->post();
    }

    TxType AtomicSwapTransaction::GetType() const
    {
        return TxType::AtomicSwap;
    }

    AtomicSwapTransaction::State AtomicSwapTransaction::GetState(SubTxID subTxID) const
    {
        State state = State::BuildingBeamLockTX;
        GetParameter(TxParameterID::State, state, subTxID);
        return state;
    }

    AtomicSwapTransaction::SubTxState AtomicSwapTransaction::GetSubTxState(SubTxID subTxID) const
    {
        SubTxState state = SubTxState::Initial;
        GetParameter(TxParameterID::State, state, subTxID);
        return state;
    }

    void AtomicSwapTransaction::UpdateImpl()
    {
        State state = GetState(kDefaultSubTxID);
        bool isBeamOwner = IsSender();

        switch (state)
        {
        case State::BuildingLockTX:
        {
            assert(!isBeamOwner);
            // build LOCK_TX
            SetNextState(State::BuildingRefundTX);
            break;
        }
        case State::BuildingRefundTX:
            assert(!isBeamOwner);
            SetNextState(State::BuildingBeamLockTX);
            break;
        case State::BuildingRedeemTX:
        {
            assert(isBeamOwner);
            SetNextState(State::BuildingBeamLockTX);
            break;
        }
        case State::BuildingBeamLockTX:
        {
            auto lockTxState = BuildLockTx();
            if (lockTxState != SubTxState::Constructed)
                break;

            SetNextState(State::BuildingBeamRefundTX);
            break;
        }
        case State::BuildingBeamRefundTX:
        {
            auto subTxState = BuildRefundTx();
            if (subTxState != SubTxState::Constructed)
                break;

            SetNextState(State::BuildingBeamRedeemTX);
            break;
        }
        case State::BuildingBeamRedeemTX:
        {
            auto subTxState = BuildRedeemTx();
            if (subTxState != SubTxState::Constructed)
                break;

            SetNextState(State::SendingBeamLockTX);
            break;
        }

        case State::SendingContractTX:
        {
            LOG_DEBUG() << "SendingContractTX - Not implemented yet.";
            SetNextState(State::SendingBeamLockTX);
            break;
        }
        case State::SendingRefundTX:
            break;
        case State::SendingRedeemTX:
            break;

        case State::SendingBeamLockTX:
        {
            // TODO: load m_LockTx
            if (m_LockTx && !SendSubTx(m_LockTx, SubTxIndex::LOCK_TX))
                break;

            if (!isBeamOwner)
            {
                // validate second chain height (second coin timelock)
            }

            if (!IsSubTxCompleted(SubTxIndex::LOCK_TX))
                break;
            
            LOG_DEBUG() << GetTxID()<< " Lock TX completed.";

            // TODO: change this
            SetParameter(TxParameterID::KernelProofHeight, Height(0));

            SetNextState(State::SendingBeamRedeemTX);
            break;
        }
        case State::SendingBeamRedeemTX:
        {
            if (m_RedeemTx && !SendSubTx(m_RedeemTx, SubTxIndex::REDEEM_TX))
                break;

            if (isBeamOwner)
            {
                if (IsBeamLockTimeExpired())
                {
                    LOG_DEBUG() << GetTxID() << " Beam locktime expired.";

                    // TODO: implement
                    SetNextState(State::SendingBeamRefundTX);
                    break;
                }

                // request kernel body for getting secret(preimage)
                ECC::uintBig preimage(Zero);
                if (!GetPreimageFromChain(preimage))
                    break;

                LOG_DEBUG() << GetTxID() << " Got preimage: " << preimage;
                // Redeem second Coin

                SetNextState(State::CompleteSwap);
            }
            else
            {
                if (!IsSubTxCompleted(SubTxIndex::REDEEM_TX))
                    break;

                LOG_DEBUG() << GetTxID() << " Redeem TX completed!";

                SetNextState(State::CompleteSwap);
            }
            break;
        }
        case State::SendingBeamRefundTX:
        {
            assert(isBeamOwner);

            if (m_RefundTx && !SendSubTx(m_RefundTx, SubTxIndex::REFUND_TX))
                break;

            if (!IsSubTxCompleted(SubTxIndex::REFUND_TX))
                break;

            LOG_DEBUG() << GetTxID() << " Refund TX completed!";

            SetNextState(State::CompleteSwap);
            break;
        }
        case State::CompleteSwap:
        {
            LOG_DEBUG() << GetTxID() << " Swap completed.";

            UpdateTxDescription(TxStatus::Completed);
            break;
        }

        default:
            break;
        }
    }

    AtomicSwapTransaction::SubTxState AtomicSwapTransaction::BuildLockTx()
    {
        // load state
        SubTxState lockTxState = SubTxState::Initial;
        GetParameter(TxParameterID::State, lockTxState, SubTxIndex::LOCK_TX);

        bool isSender = IsSender();
        auto lockTxBuilder = std::make_unique<LockTxBuilder>(*this, GetAmount(), GetMandatoryParameter<Amount>(TxParameterID::Fee));

        if (!lockTxBuilder->GetInitialTxParams() && lockTxState == SubTxState::Initial)
        {
            // TODO: check expired!

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
            if (lockTxState == SubTxState::Initial)
            {
                SendInvitation(*lockTxBuilder, isSender);
                SetState(SubTxState::Invitation, SubTxIndex::LOCK_TX);
                lockTxState = SubTxState::Invitation;
            }
            return lockTxState;
        }

        lockTxBuilder->LoadSharedParameters();
        lockTxBuilder->SignPartial();

        if (lockTxState == SubTxState::Initial || lockTxState == SubTxState::Invitation)
        {
            lockTxBuilder->SharedUTXOProofPart2(isSender);
            SendBulletProofPart2(*lockTxBuilder, isSender);
            SetState(SubTxState::SharedUTXOProofPart2, SubTxIndex::LOCK_TX);
            lockTxState = SubTxState::SharedUTXOProofPart2;
            return lockTxState;
        }

        assert(lockTxBuilder->GetPeerSignature());
        if (!lockTxBuilder->IsPeerSignatureValid())
        {
            LOG_INFO() << GetTxID() << " Peer signature is invalid.";
            return lockTxState;
        }

        lockTxBuilder->FinalizeSignature();

        if (lockTxState == SubTxState::SharedUTXOProofPart2)
        {
            lockTxBuilder->SharedUTXOProofPart3(isSender);
            SendBulletProofPart3(*lockTxBuilder, isSender);
            SetState(SubTxState::Constructed, SubTxIndex::LOCK_TX);
            lockTxState = SubTxState::Constructed;
        }

        if (isSender && lockTxState == SubTxState::Constructed)
        {
            // Create TX
            auto transaction = lockTxBuilder->CreateTransaction();
            beam::TxBase::Context context;
            if (!transaction->IsValid(context))
            {
                // TODO: check
                OnFailed(TxFailureReason::InvalidTransaction, true);
                return lockTxState;
            }

            // TODO: return
            m_LockTx = transaction;

            return lockTxState;
        }

        return lockTxState;
    }

    AtomicSwapTransaction::SubTxState AtomicSwapTransaction::BuildRefundTx()
    {
        SubTxID subTxID = SubTxIndex::REFUND_TX;
        SubTxState subTxState = GetSubTxState(subTxID);
        // TODO: calculating fee!
        Amount refundFee = 0;
        Amount refundAmount = GetAmount() - refundFee;
        bool isTxOwner = IsSender();
        SharedTxBuilder builder{ *this, subTxID, refundAmount, refundFee };

        if (!builder.GetSharedParameters())
        {
            return subTxState;
        }

        // send invite to get 
        if (!builder.GetInitialTxParams() && subTxState == SubTxState::Initial)
        {
            // TODO: check expired!
            builder.InitTx(isTxOwner, false);
        }

        builder.CreateKernel();

        if (!builder.GetPeerPublicExcessAndNonce())
        {
            if (subTxState == SubTxState::Initial && isTxOwner)
            {
                SendSharedTxInvitation(builder);
                SetState(SubTxState::Invitation, subTxID);
                subTxState = SubTxState::Invitation;
            }
            return subTxState;
        }

        builder.SignPartial();

        if (!builder.GetPeerSignature())
        {
            if (subTxState == SubTxState::Initial && !isTxOwner)
            {
                // invited participant
                assert(!IsInitiator());
                ConfirmSharedTxInvitation(builder);
                SetState(SubTxState::Constructed, subTxID);
                subTxState = SubTxState::Constructed;
            }
            return subTxState;
        }

        if (!builder.IsPeerSignatureValid())
        {
            LOG_INFO() << GetTxID() << " Peer signature is invalid.";
            return subTxState;
        }

        builder.FinalizeSignature();

        SetState(SubTxState::Constructed, subTxID);
        subTxState = SubTxState::Constructed;

        if (isTxOwner)
        {
            auto transaction = builder.CreateTransaction();
            beam::TxBase::Context context;
            transaction->IsValid(context);

            m_RefundTx = transaction;
        }

        return subTxState;
    }

    AtomicSwapTransaction::SubTxState AtomicSwapTransaction::BuildRedeemTx()
    {
        SubTxID subTxID = SubTxIndex::REDEEM_TX;
        SubTxState subTxState = GetSubTxState(subTxID);
        // TODO: calculating fee!
        Amount redeemFee = 0;
        Amount redeemAmount = GetAmount() - redeemFee;
        bool isTxOwner = !IsSender();
        SharedTxBuilder builder{ *this, subTxID, redeemAmount, redeemFee };

        if (!builder.GetSharedParameters())
        {
            return subTxState;
        }

        // send invite to get 
        if (!builder.GetInitialTxParams() && subTxState == SubTxState::Initial)
        {
            // TODO: check expired!
            builder.InitTx(isTxOwner, true);
        }

        builder.CreateKernel();

        if (!builder.GetPeerPublicExcessAndNonce())
        {
            if (subTxState == SubTxState::Initial && isTxOwner)
            {
                // send invitation with LockImage
                SendSharedTxInvitation(builder, true);
                SetState(SubTxState::Invitation, subTxID);
                subTxState = SubTxState::Invitation;
            }
            return subTxState;
        }

        builder.SignPartial();

        if (!builder.GetPeerSignature())
        {
            if (subTxState == SubTxState::Initial && !isTxOwner)
            {
                // invited participant
                assert(IsInitiator());
                ConfirmSharedTxInvitation(builder);
                SetState(SubTxState::Constructed, subTxID);
                subTxState = SubTxState::Constructed;
            }
            return subTxState;
        }

        if (!builder.IsPeerSignatureValid())
        {
            LOG_INFO() << GetTxID() << " Peer signature is invalid.";
            return subTxState;
        }

        builder.FinalizeSignature();

        SetState(SubTxState::Constructed, subTxID);
        subTxState = SubTxState::Constructed;

        if (isTxOwner)
        {
            auto transaction = builder.CreateTransaction();
            beam::TxBase::Context context;
            if (!transaction->IsValid(context))
            {
                // TODO: check
                OnFailed(TxFailureReason::InvalidTransaction, true);
                return subTxState;
            }

            m_RedeemTx = transaction;
        }

        return subTxState;
    }

    bool AtomicSwapTransaction::SendSubTx(Transaction::Ptr transaction, SubTxID subTxID)
    {
        bool isRegistered = false;
        if (!GetParameter(TxParameterID::TransactionRegistered, isRegistered, subTxID))
        {
            m_Gateway.register_tx(GetTxID(), transaction);
            return isRegistered;
        }

        if (!isRegistered)
        {
            OnFailed(TxFailureReason::FailedToRegister, true);
            return isRegistered;
        }

        return isRegistered;
    }

    bool AtomicSwapTransaction::IsBeamLockTimeExpired() const
    {
        Height lockTimeHeight = MaxHeight;
        GetParameter(TxParameterID::MinHeight, lockTimeHeight);

        Block::SystemState::Full state;

        return GetTip(state) && state.m_Height > (lockTimeHeight + kBeamLockTime);
    }

    bool AtomicSwapTransaction::IsSubTxCompleted(SubTxID subTxID) const
    {
        Height hProof = 0;
        // TODO: check
        GetParameter(TxParameterID::KernelProofHeight, hProof/*, subTxID*/);
        if (!hProof)
        {
            Merkle::Hash kernelID = GetMandatoryParameter<Merkle::Hash>(TxParameterID::KernelID, subTxID);
            m_Gateway.confirm_kernel(GetTxID(), kernelID);
            return false;
        }
        return true;
    }

    bool AtomicSwapTransaction::GetPreimageFromChain(ECC::uintBig& preimage) const
    {
        Height hProof = 0;
        GetParameter(TxParameterID::KernelProofHeight, hProof/*, subTxID*/);
        GetParameter(TxParameterID::PreImage, preimage);

        if (!hProof)
        {
            Merkle::Hash kernelID = GetMandatoryParameter<Merkle::Hash>(TxParameterID::KernelID, SubTxIndex::REDEEM_TX);
            m_Gateway.get_kernel(GetTxID(), kernelID);
            return false;
        }

        return true;
    }

    Amount AtomicSwapTransaction::GetAmount() const
    {
        if (!m_Amount.is_initialized())
        {
            m_Amount = GetMandatoryParameter<Amount>(TxParameterID::Amount);
        }
        return *m_Amount;
    }

    bool AtomicSwapTransaction::IsSender() const
    {
        if (!m_IsSender.is_initialized())
        {
            m_IsSender = GetMandatoryParameter<bool>(TxParameterID::IsSender);
        }
        return *m_IsSender;
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

    bool SharedTxBuilder::GetSharedParameters()
    {
        return m_Tx.GetParameter(TxParameterID::SharedBlindingFactor, m_SharedBlindingFactor, AtomicSwapTransaction::SubTxIndex::LOCK_TX)
            && m_Tx.GetParameter(TxParameterID::PeerPublicSharedBlindingFactor, m_PeerPublicSharedBlindingFactor, AtomicSwapTransaction::SubTxIndex::LOCK_TX);
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

        // TODO: move it to separate function
        Point::Native commitment(Zero);
        Tag::AddValue(commitment, nullptr, GetAmount());
        commitment += Context::get().G * m_SharedBlindingFactor;
        commitment += m_PeerPublicSharedBlindingFactor;

        auto& input = m_Inputs.emplace_back(make_unique<Input>());
        input->m_Commitment = commitment;
        m_Tx.SetParameter(TxParameterID::Inputs, m_Inputs, false, m_SubTxID);

        m_Offset += m_SharedBlindingFactor;

        // add output
        AddOutput(GetAmount(), false);
    }

    void SharedTxBuilder::InitOffset()
    {
        m_Offset += m_SharedBlindingFactor;
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