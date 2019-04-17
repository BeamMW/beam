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
#include "nlohmann/json.hpp"

#include "lock_tx_builder.h"
#include "shared_tx_builder.h"

using namespace ECC;
using json = nlohmann::json;

namespace beam::wallet
{
    namespace
    {

    }

    AtomicSwapTransaction::AtomicSwapTransaction(INegotiatorGateway& gateway
                                               , beam::IWalletDB::Ptr walletDB
                                               , const TxID& txID)
        : BaseTransaction(gateway, walletDB, txID)
    {

    }

    void AtomicSwapTransaction::SetSecondSide(SecondSide::Ptr value)
    {
        m_secondSide = value;
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

    void AtomicSwapTransaction::UpdateImpl()
    {
        State state = GetState(kDefaultSubTxID);
        bool isBeamOwner = IsBeamSide();

        switch (state)
        {
        case State::Initial:
        {
            if (!m_secondSide->Initial(isBeamOwner))
                break;

            SetNextState(State::Invitation);
            break;
        }
        case State::Invitation:
        {
            if (IsInitiator())
            {
                // init locktime
                m_secondSide->InitLockTime();
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

            SetNextState(State::BuildingBeamRefundTX);
            break;
        }
        case State::BuildingBeamRefundTX:
        {
            auto subTxState = BuildBeamWithdrawTx(SubTxIndex::BEAM_REFUND_TX, m_WithdrawTx);
            if (subTxState != SubTxState::Constructed)
                break;

            m_WithdrawTx.reset();
            SetNextState(State::BuildingBeamRedeemTX);
            break;
        }
        case State::BuildingBeamRedeemTX:
        {
            auto subTxState = BuildBeamWithdrawTx(SubTxIndex::BEAM_REDEEM_TX, m_WithdrawTx);
            if (subTxState != SubTxState::Constructed)
                break;

            m_WithdrawTx.reset();
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

            SetNextState(State::SendingBeamLockTX);
            break;
        }
        case State::SendingRefundTX:
        {
            assert(!isBeamOwner);
            if (!m_secondSide->SendRefund())
                break;

            assert(false && "Not implemented yet.");
            break;
        }
        case State::SendingRedeemTX:
        {
            assert(isBeamOwner);
            if (!m_secondSide->SendRedeem())
                break;
            
            LOG_DEBUG() << GetTxID() << " Redeem TX completed!";
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

            if (!isBeamOwner)
            {
                // validate second chain height (second coin timelock)
                // SetNextState(State::SendingRefundTX);
            }

            if (!CompleteSubTx(SubTxIndex::BEAM_LOCK_TX))
                break;
            
            LOG_DEBUG() << GetTxID()<< " Beam Lock TX completed.";
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
                    LOG_DEBUG() << GetTxID() << " Beam locktime expired.";

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

                LOG_DEBUG() << GetTxID() << " Beam Redeem TX completed!";
                SetNextState(State::CompleteSwap);
            }
            break;
        }
        case State::SendingBeamRefundTX:
        {
            assert(isBeamOwner);
            if (!CompleteBeamWithdrawTx(SubTxIndex::BEAM_REFUND_TX))
                break;

            LOG_DEBUG() << GetTxID() << " Beam Refund TX completed!";
            SetNextState(State::CompleteSwap);
            break;
        }
        case State::CompleteSwap:
        {
            LOG_DEBUG() << GetTxID() << " Swap completed.";
            CompleteTx();
            break;
        }

        default:
            break;
        }
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
        // TODO: check
        Amount fee = 0;
        GetParameter(TxParameterID::Fee, fee);

        if (!fee)
        {
            return lockTxState;
        }

        auto lockTxBuilder = std::make_unique<LockTxBuilder>(*this, GetAmount(), fee);

        bool newGenerated = lockTxBuilder->GenerateBlindingExcess();
        if (newGenerated && lockTxState != SubTxState::Initial)
        {
            OnFailed(TxFailureReason::InvalidState);
            return lockTxState;
        }

        if (!lockTxBuilder->GetInitialTxParams() && lockTxState == SubTxState::Initial)
        {
            // TODO: check expired!

            if (isBeamOwner)
            {
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

        lockTxBuilder->GenerateNonce();

        if (!lockTxBuilder->GetPeerPublicExcessAndNonce())
        {
            if (lockTxState == SubTxState::Initial && isBeamOwner)
            {
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
            LOG_INFO() << GetTxID() << " Peer signature is invalid.";
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
                // TODO: check
                OnFailed(TxFailureReason::InvalidTransaction, true);
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

        if (!GetParameter(TxParameterID::Amount, withdrawAmount, subTxID))
        {
            // TODO: calculating fee!
            withdrawFee = 0;
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
            // TODO: check expired!
            builder.InitTx(isTxOwner);
        }

        bool newGenerated = builder.GenerateBlindingExcess();
        if (newGenerated && subTxState != SubTxState::Initial)
        {
            OnFailed(TxFailureReason::InvalidState);
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
            LOG_INFO() << GetTxID() << " Peer signature is invalid.";
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
                // TODO: check
                OnFailed(TxFailureReason::InvalidTransaction, true);
                return subTxState;
            }
            resultTx = transaction;
        }

        return subTxState;
    }

    bool AtomicSwapTransaction::SendSubTx(Transaction::Ptr transaction, SubTxID subTxID)
    {
        bool isRegistered = false;
        if (!GetParameter(TxParameterID::TransactionRegistered, isRegistered, subTxID))
        {
            m_Gateway.register_tx(GetTxID(), transaction, subTxID);
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

            GetWalletDB()->store(withdrawUtxo);
        }

        std::vector<Coin> modified;
        m_WalletDB->visit([&](const Coin& coin)
        {
            bool bIn = (coin.m_createTxId == m_ID);
            bool bOut = (coin.m_spentTxId == m_ID);
            if (bIn || bOut)
            {
                modified.emplace_back();
                Coin& c = modified.back();
                c = coin;

                if (bIn)
                {
                    c.m_confirmHeight = std::min(c.m_confirmHeight, hProof);
                    c.m_maturity = hProof + Rules::get().Maturity.Std; // so far we don't use incubation for our created outputs
                }
                if (bOut)
                    c.m_spentHeight = std::min(c.m_spentHeight, hProof);
            }

            return true;
        });

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

        // send invitation
        SetTxParameter msg;
        msg.AddParameter(TxParameterID::Amount, GetAmount())
            .AddParameter(TxParameterID::Fee, GetMandatoryParameter<Amount>(TxParameterID::Fee))
            .AddParameter(TxParameterID::IsSender, !IsSender())
            .AddParameter(TxParameterID::MinHeight, minHeight)
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
            .AddParameter(TxParameterID::MinHeight, lockBuilder.GetMinHeight())
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
                .AddParameter(TxParameterID::PeerSharedBulletProofPart2, bulletProof.m_Part2);
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
} // namespace