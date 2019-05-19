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

#include "lock_tx_builder.h"
#include "shared_tx_builder.h"
#include "../bitcoin/bitcoin_side.h"

using namespace ECC;

namespace beam::wallet
{
    namespace
    {
        constexpr Amount kMinFeeInGroth = 10;
    }

    AtomicSwapTransaction::AtomicSwapTransaction(INegotiatorGateway& gateway
                                               , beam::IWalletDB::Ptr walletDB
                                               , const TxID& txID)
        : BaseTransaction(gateway, walletDB, txID)
    {
    }

    void AtomicSwapTransaction::Update()
    {
        try
        {
            UpdateImpl();

            CheckExpired();
        }
        catch (const TransactionFailedException& ex)
        {
            LOG_ERROR() << GetTxID() << " exception msg: " << ex.what();
            OnFailed(ex.GetReason(), ex.ShouldNofify());
        }
        catch (const std::exception& ex)
        {
            LOG_ERROR() << GetTxID() << " exception msg: " << ex.what();
        }
    }

    void AtomicSwapTransaction::Cancel()
    {
        State state = GetState(kDefaultSubTxID);

        switch (state)
        {
        case State::HandlingContractTX:
            if (!IsBeamSide())
            {
                break;
            }
        case State::Initial:
        case State::Invitation:
        case State::BuildingBeamLockTX:
        case State::BuildingBeamRedeemTX:
        case State::BuildingBeamRefundTX:
        {
            SetNextState(State::Cancelled);
            return;
        }
        default:
            break;
        }

        LOG_INFO() << GetTxID() << " You cannot cancel transaction in state: " << static_cast<int>(state);
    }

    bool AtomicSwapTransaction::Rollback(Height height)
    {
        State state = GetState(kDefaultSubTxID);
        Height proofHeight;

        if (IsBeamSide())
        {
            if (GetParameter(TxParameterID::KernelProofHeight, proofHeight, SubTxIndex::BEAM_LOCK_TX)
                && proofHeight > height
                && state != State::SendingBeamLockTX)
            {
                SetState(State::SendingBeamLockTX);
                return true;
            }

            if (GetParameter(TxParameterID::KernelProofHeight, proofHeight, SubTxIndex::BEAM_REFUND_TX)
                && proofHeight > height
                && state != State::SendingBeamRefundTX)
            {
                SetState(State::SendingBeamRefundTX);
                return true;
            }
        }
        else
        {
            if (GetParameter(TxParameterID::KernelProofHeight, proofHeight, SubTxIndex::BEAM_REDEEM_TX) 
                && proofHeight > height 
                && state != State::SendingBeamRedeemTX)
            {
                SetState(State::SendingBeamRedeemTX);
                return true;
            }
        }

        return false;
    }

    void AtomicSwapTransaction::SetNextState(State state)
    {
        SetState(state);
        UpdateAsync();
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

    AtomicSwapTransaction::SubTxState AtomicSwapTransaction::GetSubTxState(SubTxID subTxID) const
    {
        SubTxState state = SubTxState::Initial;
        GetParameter(TxParameterID::State, state, subTxID);
        return state;
    }

    Amount AtomicSwapTransaction::GetWithdrawFee() const
    {
        // TODO(alex.starun): implement fee calculation
        return kMinFeeInGroth;
    }

    void AtomicSwapTransaction::UpdateImpl()
    {
        State state = GetState(kDefaultSubTxID);
        bool isBeamOwner = IsBeamSide();

        if (Height minHeight = 0; (state == State::Initial) && IsInitiator() && !GetParameter(TxParameterID::MinHeight, minHeight))
        {
            // init all heights
            Height currentHeight = m_WalletDB->getCurrentHeight();
            Height responseTime = GetMandatoryParameter<Height>(TxParameterID::PeerResponseHeight);
            SetParameter(TxParameterID::MinHeight, currentHeight, false);
            SetParameter(TxParameterID::PeerResponseHeight, responseTime + currentHeight);
        }

        if (!m_secondSide)
        {
            m_secondSide = m_Gateway.GetSecondSide(GetTxID());

            if (!m_secondSide)
            {
                return;
            }
        }

        CheckSubTxFailures();

        switch (state)
        {
        case State::Initial:
        {
            if (!m_secondSide->Initial())
                break;

            SetNextState(State::Invitation);
            break;
        }
        case State::Invitation:
        {
            if (IsInitiator())
            {
                // init locktime
                if (!m_secondSide->InitLockTime())
                {
                    UpdateAsync();
                    break;
                }
                SendInvitation();
            }
            
            SetNextState(State::BuildingBeamLockTX);
            break;
        }
        case State::BuildingBeamLockTX:
        {
            auto lockTxState = BuildBeamLockTx();
            if (lockTxState != SubTxState::Constructed)
                break;            
            LOG_INFO() << GetTxID() << " Beam LockTX constructed.";
            SetNextState(State::BuildingBeamRefundTX);
            break;
        }
        case State::BuildingBeamRefundTX:
        {
            auto subTxState = BuildBeamWithdrawTx(SubTxIndex::BEAM_REFUND_TX, m_WithdrawTx);
            if (subTxState != SubTxState::Constructed)
                break;

            m_WithdrawTx.reset();
            LOG_INFO() << GetTxID() << " Beam RefundTX constructed.";
            SetNextState(State::BuildingBeamRedeemTX);
            break;
        }
        case State::BuildingBeamRedeemTX:
        {
            auto subTxState = BuildBeamWithdrawTx(SubTxIndex::BEAM_REDEEM_TX, m_WithdrawTx);
            if (subTxState != SubTxState::Constructed)
                break;

            m_WithdrawTx.reset();
            LOG_INFO() << GetTxID() << " Beam RedeemTX constructed.";
            SetNextState(State::HandlingContractTX);
            break;
        }
        case State::HandlingContractTX:
        {
            if (!isBeamOwner)
            {
                if (!m_secondSide->SendLockTx())
                    break;

                SendExternalTxDetails();
            }
            else
            {
                if (!m_secondSide->ConfirmLockTx())
                {
                    UpdateOnNextTip();
                    break;
                }
            }

            LOG_INFO() << GetTxID() << " LockTX completed.";
            SetNextState(State::SendingBeamLockTX);
            break;
        }
        case State::SendingRefundTX:
        {
            assert(!isBeamOwner);

            if (!m_secondSide->IsLockTimeExpired())
            {
                UpdateOnNextTip();
                break;
            }

            if (!m_secondSide->SendRefund())
                break;

            LOG_INFO() << GetTxID() << " RefundTX completed!";
            SetNextState(State::CompleteSwap);
            break;
        }
        case State::SendingRedeemTX:
        {
            assert(isBeamOwner);
            if (!m_secondSide->SendRedeem())
                break;
            
            LOG_INFO() << GetTxID() << " RedeemTX completed!";
            SetNextState(State::CompleteSwap);
            break;
        }
        case State::SendingBeamLockTX:
        {
            if (!m_LockTx && isBeamOwner)
            {
                BuildBeamLockTx();
            }

            if (m_LockTx && !SendSubTx(m_LockTx, SubTxIndex::BEAM_LOCK_TX))
                break;

            if (!isBeamOwner && m_secondSide->IsLockTimeExpired())
            {
                LOG_INFO() << GetTxID() << " Locktime is expired.";
                SetNextState(State::SendingRefundTX);
                break;
            }

            if (!CompleteSubTx(SubTxIndex::BEAM_LOCK_TX))
                break;
            
            LOG_INFO() << GetTxID()<< " Beam LockTX completed.";
            SetNextState(State::SendingBeamRedeemTX);
            break;
        }
        case State::SendingBeamRedeemTX:
        {
            if (isBeamOwner)
            {
                UpdateOnNextTip();

                if (IsBeamLockTimeExpired())
                {
                    LOG_INFO() << GetTxID() << " Beam locktime expired.";
                    SetNextState(State::SendingBeamRefundTX);
                    break;
                }

                // request kernel body for getting secret(preimage)
                ECC::uintBig preimage(Zero);
                if (!GetPreimageFromChain(preimage, SubTxIndex::BEAM_REDEEM_TX))
                    break;

                LOG_DEBUG() << GetTxID() << " Got preimage: " << preimage;

                // Redeem second Coin
                SetNextState(State::SendingRedeemTX);
            }
            else
            {
                if (!CompleteBeamWithdrawTx(SubTxIndex::BEAM_REDEEM_TX))
                    break;

                LOG_INFO() << GetTxID() << " Beam RedeemTX completed!";
                SetNextState(State::CompleteSwap);
            }
            break;
        }
        case State::SendingBeamRefundTX:
        {
            assert(isBeamOwner);
            if (!IsBeamLockTimeExpired())
            {
                UpdateOnNextTip();
                break;
            }

            if (!CompleteBeamWithdrawTx(SubTxIndex::BEAM_REFUND_TX))
                break;

            LOG_INFO() << GetTxID() << " Beam Refund TX completed!";
            SetNextState(State::CompleteSwap);
            break;
        }
        case State::CompleteSwap:
        {
            LOG_INFO() << GetTxID() << " Swap completed.";
            CompleteTx();
            break;
        }
        case State::Cancelled:
        {
            LOG_INFO() << GetTxID() << " Transaction cancelled.";
            // TODO roman.strilec: need to implement notification of counterparty
            RollbackTx();
            break;
        }

        default:
            break;
        }
    }

    void AtomicSwapTransaction::RollbackTx()
    {
        LOG_INFO() << GetTxID() << " Transaction failed. Rollback...";

        if (IsBeamSide())
        {
			uint8_t nRegistered = proto::TxStatus::Unspecified;
            if (GetParameter(TxParameterID::TransactionRegistered, nRegistered, SubTxIndex::BEAM_LOCK_TX) && (proto::TxStatus::Ok == nRegistered))
            {
                SetNextState(State::SendingBeamRefundTX);
                return;
            }
        }
        else
        {
			uint8_t nRegistered = proto::TxStatus::Unspecified;
			if (GetParameter(TxParameterID::TransactionRegistered, nRegistered, SubTxIndex::LOCK_TX) && (proto::TxStatus::Ok == nRegistered))
            {
                SetNextState(State::SendingRefundTX);
                return;
            }
        }

        // Default rollback
        // TODO(alex.starun): check
        UpdateTxDescription(TxStatus::Cancelled);

        GetWalletDB()->rollbackTx(GetTxID());
        m_Gateway.on_tx_completed(GetTxID());
    }

    void AtomicSwapTransaction::NotifyFailure(TxFailureReason)
    {
        //assert(false && "Not implemented yet.");
        LOG_DEBUG() << GetTxID() << " NotifyFailure not implemented yet.";
    }

    void AtomicSwapTransaction::OnFailed(TxFailureReason reason, bool notify)
    {
        LOG_ERROR() << GetTxID() << " Failed. " << GetFailureMessage(reason);

        if (notify)
        {
            NotifyFailure(reason);
        }

        SetParameter(TxParameterID::FailureReason, reason, false);

        RollbackTx();
    }

    bool AtomicSwapTransaction::CheckExpired()
    {
        if (IsBeamSide())
        {
			uint8_t nRegistered = proto::TxStatus::Unspecified;
            if (!GetParameter(TxParameterID::TransactionRegistered, nRegistered, SubTxIndex::BEAM_LOCK_TX))
            {
                Block::SystemState::Full state;
                Height lockTxMaxHeight = MaxHeight;

                if (GetParameter(TxParameterID::MaxHeight, lockTxMaxHeight, SubTxIndex::BEAM_LOCK_TX) && GetTip(state) && state.m_Height > lockTxMaxHeight)
                {
                    LOG_INFO() << GetTxID() << " Transaction expired. Current height: " << state.m_Height << ", max kernel height: " << lockTxMaxHeight;
                    OnFailed(TxFailureReason::TransactionExpired, false);
                    return true;
                }
            }
        }
        return false;
    }

    bool AtomicSwapTransaction::CompleteBeamWithdrawTx(SubTxID subTxID)
    {
        if (!m_WithdrawTx)
        {
            BuildBeamWithdrawTx(subTxID, m_WithdrawTx);
        }

        if (m_WithdrawTx && !SendSubTx(m_WithdrawTx, subTxID))
        {
            return false;
        }

        if (!CompleteSubTx(subTxID))
        {
            return false;
        }

        return true;
    }

    AtomicSwapTransaction::SubTxState AtomicSwapTransaction::BuildBeamLockTx()
    {
        // load state
        SubTxState lockTxState = SubTxState::Initial;
        GetParameter(TxParameterID::State, lockTxState, SubTxIndex::BEAM_LOCK_TX);

        bool isBeamOwner = IsBeamSide();
        auto fee = GetMandatoryParameter<Amount>(TxParameterID::Fee);
        auto lockTxBuilder = std::make_unique<LockTxBuilder>(*this, GetAmount(), fee);

        if (!lockTxBuilder->GetInitialTxParams() && lockTxState == SubTxState::Initial)
        {
            // TODO: check expired!

            if (isBeamOwner)
            {
                Height maxResponseHeight = 0;
                if (GetParameter(TxParameterID::PeerResponseHeight, maxResponseHeight))
                {
                    LOG_INFO() << GetTxID() << "[" << static_cast<SubTxID>(SubTxIndex::BEAM_LOCK_TX) << "]"
                        << " Max height for response: " << maxResponseHeight;
                }

                lockTxBuilder->SelectInputs();
                lockTxBuilder->AddChange();
                lockTxBuilder->CreateOutputs();
            }

            if (!lockTxBuilder->FinalizeOutputs())
            {
                // TODO: transaction is too big :(
            }

            UpdateTxDescription(TxStatus::InProgress);
        }

        bool newGenerated = lockTxBuilder->GenerateBlindingExcess();
        if (newGenerated && lockTxState != SubTxState::Initial)
        {
            OnSubTxFailed(TxFailureReason::InvalidState, SubTxIndex::BEAM_LOCK_TX);
            return lockTxState;
        }

        lockTxBuilder->GenerateNonce();

        if (!lockTxBuilder->UpdateMaxHeight())
        {
            OnSubTxFailed(TxFailureReason::MaxHeightIsUnacceptable, SubTxIndex::BEAM_LOCK_TX, true);
            return lockTxState;
        }

        if (!lockTxBuilder->GetPeerPublicExcessAndNonce())
        {
            if (lockTxState == SubTxState::Initial && isBeamOwner)
            {
                if (!IsInitiator())
                {
                    // When swap started not from Beam side, we should save MaxHeight
                    SetParameter(TxParameterID::MaxHeight, lockTxBuilder->GetMaxHeight(), false, SubTxIndex::BEAM_LOCK_TX);
                }

                SendLockTxInvitation(*lockTxBuilder);
                SetState(SubTxState::Invitation, SubTxIndex::BEAM_LOCK_TX);
                lockTxState = SubTxState::Invitation;
            }
            return lockTxState;
        }

        lockTxBuilder->LoadSharedParameters();
        lockTxBuilder->CreateKernel();
        lockTxBuilder->SignPartial();

        if (lockTxState == SubTxState::Initial || lockTxState == SubTxState::Invitation)
        {
            if (!lockTxBuilder->SharedUTXOProofPart2(isBeamOwner))
            {
                return lockTxState;
            }
            SendMultiSigProofPart2(*lockTxBuilder, isBeamOwner);
            SetState(SubTxState::SharedUTXOProofPart2, SubTxIndex::BEAM_LOCK_TX);
            lockTxState = SubTxState::SharedUTXOProofPart2;
            return lockTxState;
        }

        if (!lockTxBuilder->GetPeerSignature())
        {
            return lockTxState;
        }

        if (!lockTxBuilder->IsPeerSignatureValid())
        {
            OnSubTxFailed(TxFailureReason::InvalidPeerSignature, SubTxIndex::BEAM_LOCK_TX, true);
            return lockTxState;
        }

        lockTxBuilder->FinalizeSignature();

        if (lockTxState == SubTxState::SharedUTXOProofPart2)
        {
            if (!lockTxBuilder->SharedUTXOProofPart3(isBeamOwner))
            {
                return lockTxState;
            }
            SendMultiSigProofPart3(*lockTxBuilder, isBeamOwner);
            SetState(SubTxState::Constructed, SubTxIndex::BEAM_LOCK_TX);
            lockTxState = SubTxState::Constructed;
        }

        if (isBeamOwner && lockTxState == SubTxState::Constructed)
        {
            // Create TX
            auto transaction = lockTxBuilder->CreateTransaction();
            TxBase::Context::Params pars;
            TxBase::Context context(pars);
            if (!transaction->IsValid(context))
            {
                OnSubTxFailed(TxFailureReason::InvalidTransaction, SubTxIndex::BEAM_LOCK_TX, true);
                return lockTxState;
            }

            // TODO: return
            m_LockTx = transaction;
        }

        return lockTxState;
    }

    AtomicSwapTransaction::SubTxState AtomicSwapTransaction::BuildBeamWithdrawTx(SubTxID subTxID, Transaction::Ptr& resultTx)
    {
        SubTxState subTxState = GetSubTxState(subTxID);

        Amount withdrawFee = 0;
        Amount withdrawAmount = 0;

        if (!GetParameter(TxParameterID::Amount, withdrawAmount, subTxID) ||
            !GetParameter(TxParameterID::Fee, withdrawFee, subTxID))
        {
            withdrawAmount = GetAmount() - withdrawFee;

            SetParameter(TxParameterID::Amount, withdrawAmount, subTxID);
            SetParameter(TxParameterID::Fee, withdrawFee, subTxID);
        }

        bool isTxOwner = (IsBeamSide() && (SubTxIndex::BEAM_REFUND_TX == subTxID)) || (!IsBeamSide() && (SubTxIndex::BEAM_REDEEM_TX == subTxID));
        SharedTxBuilder builder{ *this, subTxID, withdrawAmount, withdrawFee };

        if (!builder.GetSharedParameters())
        {
            return subTxState;
        }

        // send invite to get 
        if (!builder.GetInitialTxParams() && subTxState == SubTxState::Initial)
        {
            builder.InitTx(isTxOwner);
        }

        bool newGenerated = builder.GenerateBlindingExcess();
        if (newGenerated && subTxState != SubTxState::Initial)
        {
            OnSubTxFailed(TxFailureReason::InvalidState, subTxID);
            return subTxState;
        }

        builder.GenerateNonce();
        builder.CreateKernel();

        if (!builder.GetPeerPublicExcessAndNonce())
        {
            if (subTxState == SubTxState::Initial && isTxOwner)
            {
                SendSharedTxInvitation(builder, SubTxIndex::BEAM_REDEEM_TX == subTxID);
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
                ConfirmSharedTxInvitation(builder);
                SetState(SubTxState::Constructed, subTxID);
                subTxState = SubTxState::Constructed;
            }
            return subTxState;
        }

        if (!builder.IsPeerSignatureValid())
        {
            OnSubTxFailed(TxFailureReason::InvalidPeerSignature, subTxID, true);
            return subTxState;
        }

        builder.FinalizeSignature();

        SetState(SubTxState::Constructed, subTxID);
        subTxState = SubTxState::Constructed;

        if (isTxOwner)
        {
            auto transaction = builder.CreateTransaction();
            TxBase::Context::Params pars;
            TxBase::Context context(pars);
            if (!transaction->IsValid(context))
            {
                OnSubTxFailed(TxFailureReason::InvalidTransaction, subTxID, true);
                return subTxState;
            }
            resultTx = transaction;
        }

        return subTxState;
    }

    bool AtomicSwapTransaction::SendSubTx(Transaction::Ptr transaction, SubTxID subTxID)
    {
		uint8_t nRegistered = proto::TxStatus::Unspecified;
        if (!GetParameter(TxParameterID::TransactionRegistered, nRegistered, subTxID))
        {
            m_Gateway.register_tx(GetTxID(), transaction, subTxID);
            return (proto::TxStatus::Ok == nRegistered);
        }

        if (proto::TxStatus::Ok != nRegistered)
        {
            OnSubTxFailed(TxFailureReason::FailedToRegister, subTxID, subTxID == SubTxIndex::BEAM_LOCK_TX);
            return false;
        }

        return true;
    }

    bool AtomicSwapTransaction::IsBeamLockTimeExpired() const
    {
        Height lockTimeHeight = MaxHeight;
        GetParameter(TxParameterID::MinHeight, lockTimeHeight);

        Block::SystemState::Full state;

        return GetTip(state) && state.m_Height > (lockTimeHeight + kBeamLockTimeInBlocks);
    }

    bool AtomicSwapTransaction::CompleteSubTx(SubTxID subTxID)
    {
        Height hProof = 0;
        GetParameter(TxParameterID::KernelProofHeight, hProof, subTxID);
        if (!hProof)
        {
            Merkle::Hash kernelID = GetMandatoryParameter<Merkle::Hash>(TxParameterID::KernelID, subTxID);
            m_Gateway.confirm_kernel(GetTxID(), kernelID, subTxID);
            return false;
        }

        if ((SubTxIndex::BEAM_REDEEM_TX == subTxID) || (SubTxIndex::BEAM_REFUND_TX == subTxID))
        {
            // store Coin in DB
            auto amount = GetMandatoryParameter<Amount>(TxParameterID::Amount, subTxID);
            Coin withdrawUtxo(amount);

            withdrawUtxo.m_createTxId = GetTxID();
            withdrawUtxo.m_ID = GetMandatoryParameter<Coin::ID>(TxParameterID::SharedCoinID, subTxID);

            GetWalletDB()->save(withdrawUtxo);
        }

        std::vector<Coin> modified = GetWalletDB()->getCoinsByTx(GetTxID());
        for (auto& coin : modified)
        {
            bool bIn = (coin.m_createTxId == m_ID);
            bool bOut = (coin.m_spentTxId == m_ID);
            if (bIn || bOut)
            {
                if (bIn)
                {
                    coin.m_confirmHeight = std::min(coin.m_confirmHeight, hProof);
                    coin.m_maturity = hProof + Rules::get().Maturity.Std; // so far we don't use incubation for our created outputs
                }
                if (bOut)
                    coin.m_spentHeight = std::min(coin.m_spentHeight, hProof);
            }
        }

        GetWalletDB()->save(modified);

        return true;
    }

    bool AtomicSwapTransaction::GetPreimageFromChain(ECC::uintBig& preimage, SubTxID subTxID) const
    {
        Height hProof = 0;
        GetParameter(TxParameterID::KernelProofHeight, hProof, subTxID);
        GetParameter(TxParameterID::PreImage, preimage, subTxID);

        if (!hProof)
        {
            Merkle::Hash kernelID = GetMandatoryParameter<Merkle::Hash>(TxParameterID::KernelID, SubTxIndex::BEAM_REDEEM_TX);
            m_Gateway.get_kernel(GetTxID(), kernelID, subTxID);
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

    bool AtomicSwapTransaction::IsBeamSide() const
    {
        if (!m_IsBeamSide.is_initialized())
        {
            bool isBeamSide = false;
            GetParameter(TxParameterID::AtomicSwapIsBeamSide, isBeamSide);
            m_IsBeamSide = isBeamSide;
        }
        return *m_IsBeamSide;
    }

    void AtomicSwapTransaction::SendInvitation()
    {
        auto swapAmount = GetMandatoryParameter<Amount>(TxParameterID::AtomicSwapAmount);
        auto swapCoin = GetMandatoryParameter<AtomicSwapCoin>(TxParameterID::AtomicSwapCoin);
        auto swapAddress = GetMandatoryParameter<std::string>(TxParameterID::AtomicSwapAddress);
        auto swapLockTime = GetMandatoryParameter<Timestamp>(TxParameterID::AtomicSwapExternalLockTime);
        auto minHeight = GetMandatoryParameter<Height>(TxParameterID::MinHeight);
        auto lifetime = GetMandatoryParameter<Height>(TxParameterID::Lifetime);

        // send invitation
        SetTxParameter msg;
        msg.AddParameter(TxParameterID::Amount, GetAmount())
            .AddParameter(TxParameterID::Fee, GetMandatoryParameter<Amount>(TxParameterID::Fee))
            .AddParameter(TxParameterID::IsSender, !IsSender())
            .AddParameter(TxParameterID::MinHeight, minHeight)
            .AddParameter(TxParameterID::Lifetime, lifetime)
            .AddParameter(TxParameterID::AtomicSwapAmount, swapAmount)
            .AddParameter(TxParameterID::AtomicSwapCoin, swapCoin)
            .AddParameter(TxParameterID::AtomicSwapPeerAddress, swapAddress)
            .AddParameter(TxParameterID::AtomicSwapExternalLockTime, swapLockTime)
            .AddParameter(TxParameterID::AtomicSwapIsBeamSide, !IsBeamSide())
            .AddParameter(TxParameterID::PeerProtoVersion, s_ProtoVersion);

        if (!SendTxParameters(std::move(msg)))
        {
            OnFailed(TxFailureReason::FailedToSendParameters, false);
        }
    }

    void AtomicSwapTransaction::SendExternalTxDetails()
    {
        SetTxParameter msg;
        m_secondSide->AddTxDetails(msg);

        if (!SendTxParameters(std::move(msg)))
        {
            OnFailed(TxFailureReason::FailedToSendParameters, false);
        }
    }

    void AtomicSwapTransaction::SendLockTxInvitation(const LockTxBuilder& lockBuilder)
    {
        auto swapAddress = GetMandatoryParameter<std::string>(TxParameterID::AtomicSwapAddress);

        SetTxParameter msg;
        msg.AddParameter(TxParameterID::AtomicSwapPeerAddress, swapAddress)
            .AddParameter(TxParameterID::Fee, lockBuilder.GetFee())
            .AddParameter(TxParameterID::SubTxIndex, SubTxIndex::BEAM_LOCK_TX)
            .AddParameter(TxParameterID::PeerMaxHeight, lockBuilder.GetMaxHeight())
            .AddParameter(TxParameterID::PeerPublicExcess, lockBuilder.GetPublicExcess())
            .AddParameter(TxParameterID::PeerPublicNonce, lockBuilder.GetPublicNonce());

        if (!SendTxParameters(std::move(msg)))
        {
            OnFailed(TxFailureReason::FailedToSendParameters, false);
        }
    }

    void AtomicSwapTransaction::SendMultiSigProofPart2(const LockTxBuilder& lockBuilder, bool isMultiSigProofOwner)
    {
        SetTxParameter msg;
        msg.AddParameter(TxParameterID::SubTxIndex, SubTxIndex::BEAM_LOCK_TX)
            .AddParameter(TxParameterID::PeerSignature, lockBuilder.GetPartialSignature())
            .AddParameter(TxParameterID::PeerOffset, lockBuilder.GetOffset())
            .AddParameter(TxParameterID::PeerPublicSharedBlindingFactor, lockBuilder.GetPublicSharedBlindingFactor());
        if (isMultiSigProofOwner)
        {
            auto proofPartialMultiSig = lockBuilder.GetProofPartialMultiSig();
            msg.AddParameter(TxParameterID::PeerSharedBulletProofMSig, proofPartialMultiSig);
        }
        else
        {
            auto bulletProof = lockBuilder.GetSharedProof();
            msg.AddParameter(TxParameterID::PeerPublicExcess, lockBuilder.GetPublicExcess())
                .AddParameter(TxParameterID::PeerPublicNonce, lockBuilder.GetPublicNonce())
                .AddParameter(TxParameterID::PeerSharedBulletProofPart2, bulletProof.m_Part2)
                .AddParameter(TxParameterID::PeerMaxHeight, lockBuilder.GetMaxHeight());
        }

        if (!SendTxParameters(std::move(msg)))
        {
            OnFailed(TxFailureReason::FailedToSendParameters, false);
        }
    }

    void AtomicSwapTransaction::SendMultiSigProofPart3(const LockTxBuilder& lockBuilder, bool isMultiSigProofOwner)
    {
        if (!isMultiSigProofOwner)
        {
            auto bulletProof = lockBuilder.GetSharedProof();
            SetTxParameter msg;
            msg.AddParameter(TxParameterID::SubTxIndex, SubTxIndex::BEAM_LOCK_TX)
                .AddParameter(TxParameterID::PeerSharedBulletProofPart3, bulletProof.m_Part3);

            if (!SendTxParameters(std::move(msg)))
            {
                OnFailed(TxFailureReason::FailedToSendParameters, false);
            }
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

        if (!SendTxParameters(std::move(msg)))
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

        if (!SendTxParameters(std::move(msg)))
        {
            OnFailed(TxFailureReason::FailedToSendParameters, false);
        }
    }

    void AtomicSwapTransaction::OnSubTxFailed(TxFailureReason reason, SubTxID subTxID, bool notify)
    {
        LOG_ERROR() << GetTxID() << "[" << subTxID << "]" << " Failed. " << GetFailureMessage(reason);

        SetParameter(TxParameterID::FailureReason, reason, false, subTxID);
        OnFailed(TxFailureReason::SubTxFailed, notify);
    }

    void AtomicSwapTransaction::CheckSubTxFailures()
    {
        State state = GetState(kDefaultSubTxID);
        TxFailureReason reason = TxFailureReason::Unknown;

        if ((state == State::Initial ||
            state == State::Invitation ||
            state == State::HandlingContractTX) && GetParameter(TxParameterID::FailureReason, reason, SubTxIndex::LOCK_TX))
        {
            OnSubTxFailed(reason, SubTxIndex::LOCK_TX, false);
        }
    }

} // namespace