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

#include "swap_transaction.h"

#include "bitcoin/bitcoin.hpp"

#include "lock_tx_builder.h"
#include "shared_tx_builder.h"
#include "wallet/bitcoin/bitcoin_side.h"
#include "wallet/wallet.h"

using namespace ECC;

namespace beam::wallet
{
    template <typename T>
    boost::optional<T> GetTxParameterAsOptional(const BaseTransaction& tx, TxParameterID paramID, SubTxID subTxID = kDefaultSubTxID)
    {
        if (T value{}; tx.GetParameter(paramID, value, subTxID))
        {
            return value;
        }
        return boost::optional<T>();
    }

    bool IsCommonTxParameterExternalSettable(TxParameterID paramID, const boost::optional<bool>& isInitiator)
    {
        switch (paramID)
        {
            case TxParameterID::AtomicSwapExternalLockTime:
                return isInitiator && !*isInitiator;
            case TxParameterID::PeerProtoVersion:
            case TxParameterID::AtomicSwapPeerPublicKey:
            case TxParameterID::FailureReason:
            case TxParameterID::AtomicSwapPeerPrivateKey:
                return true;
            default:
                return false;
        }
    }

    bool IsBeamLockTxParameterExternalSettable(TxParameterID paramID, const boost::optional<bool>& isBeamSide, const boost::optional<bool>& isInitiator)
    {
        switch (paramID)
        {
            case TxParameterID::MinHeight:
                return isInitiator && !isInitiator.get();
            case TxParameterID::Fee:
                return isBeamSide && !isBeamSide.get();
            case TxParameterID::PeerSignature:
            case TxParameterID::PeerOffset:
            case TxParameterID::PeerSharedBulletProofPart3:
                return isBeamSide && isBeamSide.get();
            case TxParameterID::PeerMaxHeight:
            case TxParameterID::PeerPublicNonce:
            case TxParameterID::PeerPublicExcess:
            case TxParameterID::PeerSharedBulletProofPart2:
            case TxParameterID::PeerPublicSharedBlindingFactor:
                return true;
            default:
                return false;
        }
    }

    bool IsBeamWithdrawTxParameterExternalSettable(TxParameterID paramID, SubTxID subTxID, const boost::optional<bool>& isBeamSide)
    {
        boost::optional<bool> isTxOwner;
        if (isBeamSide)
        {
            isTxOwner = (isBeamSide.get() && (SubTxIndex::BEAM_REFUND_TX == subTxID)) || (!isBeamSide.get() && (SubTxIndex::BEAM_REDEEM_TX == subTxID));
        }

        switch (paramID)
        {
            case TxParameterID::Amount:
            case TxParameterID::Fee:
            case TxParameterID::MinHeight:
                return isTxOwner && !isTxOwner.get();
            case TxParameterID::PeerOffset:
                return isTxOwner && isTxOwner.get();
            case TxParameterID::PeerPublicExcess:
            case TxParameterID::PeerPublicNonce:
            case TxParameterID::PeerSignature:
                return true;
            default:
                return false;
        }
    }

    ///
    AtomicSwapTransaction::WrapperSecondSide::WrapperSecondSide(ISecondSideProvider& gateway, BaseTransaction& tx)
        : m_gateway(gateway)
        , m_tx(tx)
    {
    }

    SecondSide::Ptr AtomicSwapTransaction::WrapperSecondSide::operator -> ()
    {
        return GetSecondSide();
    }

    SecondSide::Ptr AtomicSwapTransaction::WrapperSecondSide::GetSecondSide()
    {
        if (!m_secondSide)
        {
            m_secondSide = m_gateway.GetSecondSide(m_tx);

            if (!m_secondSide)
            {
                throw UninitilizedSecondSide();
            }
        }

        return m_secondSide;
    }

    ////////////
    // Creator
    AtomicSwapTransaction::Creator::Creator(IWalletDB::Ptr walletDB)
        : m_walletDB(walletDB)
    {

    }

    void AtomicSwapTransaction::Creator::RegisterFactory(AtomicSwapCoin coinType, ISecondSideFactory::Ptr factory)
    {
        m_factories.emplace(coinType, factory);
    }

    BaseTransaction::Ptr AtomicSwapTransaction::Creator::Create(INegotiatorGateway& gateway
                                                              , IWalletDB::Ptr walletDB
                                                              , IPrivateKeyKeeper::Ptr keyKeeper
                                                              , const TxID& txID)
    {
        return BaseTransaction::Ptr(new AtomicSwapTransaction(gateway, walletDB, keyKeeper, txID, *this));
    }

    SecondSide::Ptr AtomicSwapTransaction::Creator::GetSecondSide(BaseTransaction& tx)
    {
        AtomicSwapCoin coinType = tx.GetMandatoryParameter<AtomicSwapCoin>(TxParameterID::AtomicSwapCoin);
        auto it = m_factories.find(coinType);
        if (it == m_factories.end())
        {
            throw SecondSideFactoryNotRegisteredException();
        }
        bool isBeamSide = tx.GetMandatoryParameter<bool>(TxParameterID::AtomicSwapIsBeamSide);
        return it->second->CreateSecondSide(tx, isBeamSide);
    }

    TxParameters AtomicSwapTransaction::Creator::CheckAndCompleteParameters(const TxParameters& parameters)
    {
        auto peerID = parameters.GetParameter<WalletID>(TxParameterID::PeerID);
        if (peerID)
        {
            auto receiverAddr = m_walletDB->getAddress(*peerID);
            if (receiverAddr && receiverAddr->m_OwnID)
            {
                LOG_INFO() << "Failed to initiate the atomic swap. Not able to use own address as receiver's.";
                throw FailToStartSwapException();
            }
        }
        return parameters;
    }

    AtomicSwapTransaction::AtomicSwapTransaction(INegotiatorGateway& gateway
                                               , IWalletDB::Ptr walletDB
                                               , IPrivateKeyKeeper::Ptr keyKeeper
                                               , const TxID& txID
                                               , ISecondSideProvider& secondSideProvider)
        : BaseTransaction(gateway, walletDB, keyKeeper, txID)
        , m_secondSide(secondSideProvider, *this)
    {
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
        case State::BuildingBeamLockTX:
        case State::BuildingBeamRedeemTX:
        case State::BuildingBeamRefundTX:
        {
            SetNextState(State::Canceled);
            return;
        }
        default:
            break;
        }

        LOG_INFO() << GetTxID() << " You cannot cancel transaction in state: " << static_cast<int>(state);
    }

    bool AtomicSwapTransaction::Rollback(Height height)
    {
        Height proofHeight = 0;
        bool isRolledback = false;

        if (IsBeamSide())
        {
            if (GetParameter(TxParameterID::KernelProofHeight, proofHeight, SubTxIndex::BEAM_REFUND_TX)
                && proofHeight > height)
            {
                SetParameter(TxParameterID::KernelProofHeight, Height(0), false, SubTxIndex::BEAM_REFUND_TX);
                SetParameter(TxParameterID::KernelUnconfirmedHeight, Height(0), false, SubTxIndex::BEAM_REFUND_TX);

                SetState(State::SendingBeamRefundTX);
                isRolledback = true;
            }

            if (GetParameter(TxParameterID::KernelProofHeight, proofHeight, SubTxIndex::BEAM_LOCK_TX)
                && proofHeight > height)
            {
                SetParameter(TxParameterID::KernelProofHeight, Height(0), false, SubTxIndex::BEAM_LOCK_TX);
                SetParameter(TxParameterID::KernelUnconfirmedHeight, Height(0), false, SubTxIndex::BEAM_LOCK_TX);

                SetState(State::SendingBeamLockTX);
                isRolledback = true;
            }
        }
        else
        {
            if (GetParameter(TxParameterID::KernelProofHeight, proofHeight, SubTxIndex::BEAM_REDEEM_TX) 
                && proofHeight > height)
            {
                SetParameter(TxParameterID::KernelProofHeight, Height(0), false, SubTxIndex::BEAM_REDEEM_TX);
                SetParameter(TxParameterID::KernelUnconfirmedHeight, Height(0), false, SubTxIndex::BEAM_REDEEM_TX);

                SetState(State::SendingBeamRedeemTX);
                isRolledback = true;
            }
        }

        if (isRolledback)
        {
            UpdateTxDescription(TxStatus::InProgress);
        }

        return isRolledback;
    }

    bool AtomicSwapTransaction::IsTxParameterExternalSettable(TxParameterID paramID, SubTxID subTxID) const
    {
        switch (subTxID)
        {
            case kDefaultSubTxID:
            {
                auto isInitiator = GetTxParameterAsOptional<bool>(*this, TxParameterID::IsInitiator);
                return IsCommonTxParameterExternalSettable(paramID, isInitiator);
            }
            case SubTxIndex::BEAM_LOCK_TX:
            {
                auto isBeamSide = GetTxParameterAsOptional<bool>(*this, TxParameterID::AtomicSwapIsBeamSide);
                auto isInitiator = GetTxParameterAsOptional<bool>(*this, TxParameterID::IsInitiator);
                return IsBeamLockTxParameterExternalSettable(paramID, isBeamSide, isInitiator);
            }
            case SubTxIndex::BEAM_REDEEM_TX:
            case SubTxIndex::BEAM_REFUND_TX:
            {
                auto isBeamSide = GetTxParameterAsOptional<bool>(*this, TxParameterID::AtomicSwapIsBeamSide);
                return IsBeamWithdrawTxParameterExternalSettable(paramID, subTxID, isBeamSide);
            }
            case SubTxIndex::LOCK_TX:
            {
                if (bool isBeamSide = false; GetParameter(TxParameterID::AtomicSwapIsBeamSide, isBeamSide) && isBeamSide)
                {
                    return TxParameterID::AtomicSwapExternalTxID == paramID
                        || TxParameterID::AtomicSwapExternalTxOutputIndex == paramID;
                }
                return false;
            }
            case SubTxIndex::REDEEM_TX:
                return false;
            case SubTxIndex::REFUND_TX:
                return false;
            default:
                assert(false && "unexpected subTxID!");
                return false;
        }
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

    bool AtomicSwapTransaction::IsInSafety() const
    {
        auto isRegistered = [this](SubTxID beamSubTxID, SubTxID coinSubTxID)
        {
            bool isBeamSide = GetMandatoryParameter<bool>(TxParameterID::AtomicSwapIsBeamSide);
            uint8_t status = proto::TxStatus::Unspecified;
            if (GetParameter(TxParameterID::TransactionRegistered, status, isBeamSide ? coinSubTxID : beamSubTxID))
            {
                return status == proto::TxStatus::Ok;
            }
            return false;
        };

        State state = GetState(kDefaultSubTxID);
        switch (state)
        {
        case State::Initial:
            return true;
        case State::SendingRedeemTX:
        case State::SendingBeamRedeemTX:
            return isRegistered(BEAM_REDEEM_TX, REDEEM_TX);
        case State::SendingRefundTX:
        case State::SendingBeamRefundTX:
            return isRegistered(BEAM_REFUND_TX, REFUND_TX);
        default:
            return false;
        }
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
        try
        {
            CheckSubTxFailures();

            State state = GetState(kDefaultSubTxID);
            bool isBeamOwner = IsBeamSide();

            switch (state)
            {
            case State::Initial:
            {
                if (Height responseHeight = MaxHeight; !GetParameter(TxParameterID::PeerResponseHeight, responseHeight))
                {
                    Height minHeight = GetMandatoryParameter<Height>(TxParameterID::MinHeight);
                    Height responseTime = GetMandatoryParameter<Height>(TxParameterID::PeerResponseTime);
                    SetParameter(TxParameterID::PeerResponseHeight, minHeight + responseTime);
                }

                // validate Lifetime
                Height lifeTime = GetMandatoryParameter<Height>(TxParameterID::Lifetime);
                if (lifeTime > kBeamLockTxLifetimeMax)
                {
                    LOG_ERROR() << GetTxID() << "[" << static_cast<SubTxID>(SubTxIndex::BEAM_LOCK_TX) << "] " << "Transaction's lifetime is unacceptable.";
                    OnSubTxFailed(TxFailureReason::InvalidTransaction, SubTxIndex::BEAM_LOCK_TX, true);
                    break;
                }

                if (IsInitiator())
                {
                    if (!m_secondSide->Initialize())
                    {
                        break;
                    }

                    m_secondSide->InitLockTime();

                    // Init BEAM_LOCK_TX MinHeight
                    auto currentHeight = GetWalletDB()->getCurrentHeight();
                    SetParameter(TxParameterID::MinHeight, currentHeight, false, SubTxIndex::BEAM_LOCK_TX);

                    SendInvitation();
                    LOG_INFO() << GetTxID() << " Invitation sent.";
                }
                else
                {
                    // TODO: refactor this
                    // hack, used for increase refCount!
                    auto secondSide = m_secondSide.GetSecondSide();

                    Height lockTime = 0;
                    if (!GetParameter(TxParameterID::AtomicSwapExternalLockTime, lockTime))
                    {
                        //we doesn't have an answer from other participant
                        UpdateOnNextTip();
                        break;
                    }

                    if (!secondSide->Initialize())
                    {
                        break;
                    }

                    if (!secondSide->ValidateLockTime())
                    {
                        LOG_ERROR() << GetTxID() << "[" << static_cast<SubTxID>(SubTxIndex::LOCK_TX) << "] " << "Lock height is unacceptable.";
                        OnSubTxFailed(TxFailureReason::InvalidTransaction, SubTxIndex::LOCK_TX, true);
                        break;
                    }

                    // validate BEAM_LOCK_TX MinHeight
                    // mainMinHeight < minHeight < mainPeerResponseHeight
                    Height mainMinHeight = GetMandatoryParameter<Height>(TxParameterID::MinHeight);
                    Height mainPeerResponseHeight = GetMandatoryParameter<Height>(TxParameterID::PeerResponseHeight);
                    auto minHeight = GetMandatoryParameter<Height>(TxParameterID::MinHeight, SubTxIndex::BEAM_LOCK_TX);
                    if (minHeight < mainMinHeight || minHeight >= mainPeerResponseHeight)
                    {
                        OnSubTxFailed(TxFailureReason::MinHeightIsUnacceptable, SubTxIndex::BEAM_LOCK_TX, true);
                        break;
                    }
                }

                // save LifeTime & MaxHeight for BEAM_LOCK_TX
                Height beamLockTxMaxHeight = GetMandatoryParameter<Height>(TxParameterID::MinHeight, SubTxIndex::BEAM_LOCK_TX) + lifeTime;
                SetParameter(TxParameterID::Lifetime, lifeTime, false, SubTxIndex::BEAM_LOCK_TX);
                SetParameter(TxParameterID::MaxHeight, beamLockTxMaxHeight, false, SubTxIndex::BEAM_LOCK_TX);

                SetNextState(State::BuildingBeamLockTX);
                break;
            }
            case State::BuildingBeamLockTX:
            {
                auto lockTxState = BuildBeamLockTx();
                if (lockTxState != SubTxState::Constructed)
                {
                    UpdateOnNextTip();
                    break;
                }
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
                    if (!m_secondSide->HasEnoughTimeToProcessLockTx())
                    {
                        OnFailed(NotEnoughTimeToFinishBtcTx, true);
                        break;
                    }
                    
                    if (!m_secondSide->SendLockTx())
                        break;

                    SendExternalTxDetails();

                    // Beam LockTx: switch to the state of awaiting for proofs
                    uint8_t nCode = proto::TxStatus::Ok; // compiler workaround (ref to static const)
                    SetParameter(TxParameterID::TransactionRegistered, nCode, false, SubTxIndex::BEAM_LOCK_TX);
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

                if (!m_secondSide->IsLockTimeExpired() && !m_secondSide->IsQuickRefundAvailable())
                {
                    UpdateOnNextTip();
                    break;
                }

                if (!m_secondSide->SendRefund())
                    break;

                if (!m_secondSide->ConfirmRefundTx())
                {
                    UpdateOnNextTip();
                    break;
                }

                LOG_INFO() << GetTxID() << " RefundTX completed!";
                SetNextState(State::Refunded);
                break;
            }
            case State::SendingRedeemTX:
            {
                assert(isBeamOwner);
                if (!m_secondSide->SendRedeem())
                    break;

                if (!m_secondSide->ConfirmRedeemTx())
                {
                    UpdateOnNextTip();
                    break;
                }

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

                LOG_INFO() << GetTxID() << " Beam LockTX completed.";
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
                        // If we already got SecretPrivateKey for RedeemTx, don't send refundTx,
                        // because it looks like we got rollback and we just should rerun TX's.
                        NoLeak<uintBig> secretPrivateKey;
                        if (!GetParameter(TxParameterID::AtomicSwapSecretPrivateKey, secretPrivateKey.V, SubTxIndex::BEAM_REDEEM_TX))
                        {
                            LOG_INFO() << GetTxID() << " Beam locktime expired.";
                            SetNextState(State::SendingBeamRefundTX);
                            break;
                        }
                    }

                    // request kernel body for getting secretPrivateKey
                    if (!GetKernelFromChain(SubTxIndex::BEAM_REDEEM_TX))
                        break;

                    ExtractSecretPrivateKey();

                    // Redeem second Coin
                    SetNextState(State::SendingRedeemTX);
                }
                else
                {
                    if (!IsBeamRedeemTxRegistered() && !IsSafeToSendBeamRedeemTx())
                    {
                        LOG_INFO() << GetTxID() << " Not enough time to finish Beam redeem transaction.";
                        SetNextState(State::SendingRefundTX);
                        break;
                    }

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

                SendQuickRefundPrivateKey();
                SetNextState(State::Refunded);
                break;
            }
            case State::CompleteSwap:
            {
                LOG_INFO() << GetTxID() << " Swap completed.";
                UpdateTxDescription(TxStatus::Completed);
                GetGateway().on_tx_completed(GetTxID());
                break;
            }
            case State::Canceled:
            {
                LOG_INFO() << GetTxID() << " Transaction cancelled.";
                NotifyFailure(TxFailureReason::Canceled);
                UpdateTxDescription(TxStatus::Canceled);

                RollbackTx();

                GetGateway().on_tx_completed(GetTxID());
                break;
            }
            case State::Failed:
            {
                TxFailureReason reason = TxFailureReason::Unknown;
                if (GetParameter(TxParameterID::FailureReason, reason))
                {
                    if (reason == TxFailureReason::Canceled)
                    {
                        LOG_ERROR() << GetTxID() << " Swap cancelled. The other side has cancelled the transaction.";
                    }
                    else
                    {
                        LOG_ERROR() << GetTxID() << " The other side has failed the transaction. Reason: " << GetFailureMessage(reason);
                    }
                }
                else
                {
                    LOG_ERROR() << GetTxID() << " Transaction failed.";
                }
                UpdateTxDescription(TxStatus::Failed);
                GetGateway().on_tx_completed(GetTxID());
                break;
            }

            case State::Refunded:
            {
                LOG_INFO() << GetTxID() << " Swap has not succeeded.";
                UpdateTxDescription(TxStatus::Failed);
                GetGateway().on_tx_completed(GetTxID());
                break;
            }

            default:
                break;
            }
        }
        catch (const UninitilizedSecondSide&)
        {
        }
    }

    void AtomicSwapTransaction::RollbackTx()
    {
        LOG_INFO() << GetTxID() << " Rollback...";

        GetWalletDB()->rollbackTx(GetTxID());
    }

    void AtomicSwapTransaction::NotifyFailure(TxFailureReason reason)
    {
        SetTxParameter msg;
        msg.AddParameter(TxParameterID::FailureReason, reason);

        if (IsBeamSide())
        {
            State state = GetState(kDefaultSubTxID);

            switch (state)
            {
            case State::BuildingBeamLockTX:
            case State::BuildingBeamRedeemTX:
            case State::BuildingBeamRefundTX:
            case State::HandlingContractTX:
            case State::Canceled:
            {
                NoLeak<uintBig> secretPrivateKey;

                if (GetParameter(TxParameterID::AtomicSwapPrivateKey, secretPrivateKey.V))
                {
                    LOG_DEBUG() << GetTxID() << " send additional info for quick refund";

                    // send our private key of redeem tx. we are good :)
                    msg.AddParameter(TxParameterID::AtomicSwapPeerPrivateKey, secretPrivateKey.V);
                }
                break;
            }            
            default:
                break;
            }
        }
        SendTxParameters(std::move(msg));
    }

    void AtomicSwapTransaction::OnFailed(TxFailureReason reason, bool notify)
    {
        LOG_ERROR() << GetTxID() << " Failed. " << GetFailureMessage(reason);

        if (notify)
        {
            NotifyFailure(reason);
        }

        SetParameter(TxParameterID::InternalFailureReason, reason, false);

        State state = GetState(kDefaultSubTxID);
        bool isBeamSide = IsBeamSide();

        switch (state)
        {
        case State::Initial:
        {
            break;
        }
        case State::BuildingBeamLockTX:
        case State::BuildingBeamRedeemTX:
        case State::BuildingBeamRefundTX:
        {
            RollbackTx();

            break;
        }
        case State::HandlingContractTX:
        {
            RollbackTx();
            
            break;
        }
        case State::SendingBeamLockTX:
        {
            if (isBeamSide)
            {
                RollbackTx();
                break;
            }
            else
            {
                SetNextState(State::SendingRefundTX);
                return;
            }
        }
        case State::SendingBeamRedeemTX:
        {
            if (isBeamSide)
            {
                assert(false && "Impossible case!");
                return;
            }
            else
            {
                SetNextState(State::SendingRefundTX);
                return;
            }
        }
        case State::SendingRedeemTX:
        {            
            if (isBeamSide)
            {
                LOG_ERROR() << GetTxID() << " Unexpected error.";
                return;
            }
            else
            {
                assert(false && "Impossible case!");
                return;
            }
            break;
        }
        default:
            return;
        }

        SetNextState(State::Failed);
    }

    bool AtomicSwapTransaction::CheckExpired()
    {
        TxFailureReason reason = TxFailureReason::Unknown;
        if (GetParameter(TxParameterID::InternalFailureReason, reason))
        {
            return false;
        }

        TxStatus s = TxStatus::Failed;
        if (GetParameter(TxParameterID::Status, s)
            && (s == TxStatus::Failed
                || s == TxStatus::Canceled
                || s == TxStatus::Completed))
        {
            return false;
        }

        Height lockTxMaxHeight = MaxHeight;
        if (!GetParameter(TxParameterID::MaxHeight, lockTxMaxHeight, SubTxIndex::BEAM_LOCK_TX)
            && !GetParameter(TxParameterID::PeerResponseHeight, lockTxMaxHeight))
        {
            return false;
        }

        uint8_t nRegistered = proto::TxStatus::Unspecified;
        Merkle::Hash kernelID;
        if (!GetParameter(TxParameterID::TransactionRegistered, nRegistered, SubTxIndex::BEAM_LOCK_TX)
            || !GetParameter(TxParameterID::KernelID, kernelID, SubTxIndex::BEAM_LOCK_TX))
        {
            Block::SystemState::Full state;
            if (GetTip(state) && state.m_Height > lockTxMaxHeight)
            {
                LOG_INFO() << GetTxID() << " Transaction expired. Current height: " << state.m_Height << ", max kernel height: " << lockTxMaxHeight;
                OnFailed(TxFailureReason::TransactionExpired, false);
                return true;
            }
        }
        else
        {
            Height lastUnconfirmedHeight = 0;
            if (GetParameter(TxParameterID::KernelUnconfirmedHeight, lastUnconfirmedHeight, SubTxIndex::BEAM_LOCK_TX) && lastUnconfirmedHeight > 0)
            {
                if (lastUnconfirmedHeight >= lockTxMaxHeight)
                {
                    LOG_INFO() << GetTxID() << " Transaction expired. Last unconfirmed height: " << lastUnconfirmedHeight << ", max kernel height: " << lockTxMaxHeight;
                    OnFailed(TxFailureReason::TransactionExpired, false);
                    return true;
                }
            }
        }
        return false;
    }

    bool AtomicSwapTransaction::CheckExternalFailures()
    {
        TxFailureReason reason = TxFailureReason::Unknown;
        if (GetParameter(TxParameterID::FailureReason, reason))
        {
            State state = GetState(kDefaultSubTxID);

            switch (state)
            {
            case State::Initial:
            {
                SetState(State::Failed);
                break;
            }
            case State::BuildingBeamLockTX:
            case State::BuildingBeamRedeemTX:
            case State::BuildingBeamRefundTX:
            {
                RollbackTx();
                SetState(State::Failed);
                break;
            }
            case State::HandlingContractTX:
            {
                if (IsBeamSide())
                {
                    RollbackTx();
                    SetState(State::Failed);
                }

                break;
            }
            case State::SendingBeamLockTX:
            {
                if (!IsBeamSide() && m_secondSide->IsQuickRefundAvailable())
                {
                    SetState(State::SendingRefundTX);
                }

                break;
            }
            case State::SendingBeamRedeemTX:
            case State::SendingRedeemTX:
            {
                // nothing
                break;
            }
            default:
                break;
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
        Amount fee = 0;
        // Receiver must get fee along with LockTX invitation, beam owner should have fee
        if (!GetParameter<Amount>(TxParameterID::Fee, fee, SubTxIndex::BEAM_LOCK_TX))
        {
            if (isBeamOwner)
            {
                OnSubTxFailed(TxFailureReason::FailedToGetParameter, SubTxIndex::BEAM_LOCK_TX, true);
            }
            // else receiver don't have invitation from Beam side
            return lockTxState;
        }
        auto lockTxBuilder = std::make_shared<LockTxBuilder>(*this, GetAmount(), fee);

        if (!lockTxBuilder->GetInitialTxParams() && lockTxState == SubTxState::Initial)
        {
            UpdateTxDescription(TxStatus::InProgress);

            if (isBeamOwner)
            {
                lockTxBuilder->SelectInputs();
                lockTxBuilder->AddChange();
            }

            lockTxBuilder->GenerateOffset();
        }

        lockTxBuilder->CreateInputs();
        if (isBeamOwner && lockTxBuilder->CreateOutputs())
        {
            return lockTxState;
        }

        lockTxBuilder->GenerateNonce();
        lockTxBuilder->LoadSharedParameters();

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

        assert(fee);
        lockTxBuilder->CreateKernel();
        lockTxBuilder->SignPartial();

        if (lockTxState == SubTxState::Initial || lockTxState == SubTxState::Invitation)
        {
            if (!lockTxBuilder->CreateSharedUTXOProofPart2(isBeamOwner))
            {
                OnSubTxFailed(TxFailureReason::FailedToCreateMultiSig, SubTxIndex::BEAM_LOCK_TX, true);
                return lockTxState;
            }

            if (!lockTxBuilder->CreateSharedUTXOProofPart3(isBeamOwner))
            {
                OnSubTxFailed(TxFailureReason::FailedToCreateMultiSig, SubTxIndex::BEAM_LOCK_TX, true);
                return lockTxState;
            }

            SetState(SubTxState::Constructed, SubTxIndex::BEAM_LOCK_TX);
            lockTxState = SubTxState::Constructed;

            if (!isBeamOwner)
            {
                // send part2/part3!
                SendLockTxConfirmation(*lockTxBuilder);
                return lockTxState;
            }
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

        if (isBeamOwner)
        {
            assert(lockTxState == SubTxState::Constructed);
            // Create TX
            auto transaction = lockTxBuilder->CreateTransaction();
            TxBase::Context::Params pars;
            TxBase::Context context(pars);
            context.m_Height.m_Min = lockTxBuilder->GetMinHeight();
            if (!transaction->IsValid(context))
            {
                OnSubTxFailed(TxFailureReason::InvalidTransaction, SubTxIndex::BEAM_LOCK_TX, true);
                return lockTxState;
            }

            m_LockTx = transaction;
        }

        return lockTxState;
    }

    AtomicSwapTransaction::SubTxState AtomicSwapTransaction::BuildBeamWithdrawTx(SubTxID subTxID, Transaction::Ptr& resultTx)
    {
        SubTxState subTxState = GetSubTxState(subTxID);
        bool isTxOwner = (IsBeamSide() && (SubTxIndex::BEAM_REFUND_TX == subTxID)) || (!IsBeamSide() && (SubTxIndex::BEAM_REDEEM_TX == subTxID));

        Amount withdrawAmount = 0;
        Amount withdrawFee = 0;
        // Peer must get fee and amount along with WithdrawTX invitation, txOwner should have fee
        if (!GetParameter(TxParameterID::Fee, withdrawFee, subTxID))
        {
            if (isTxOwner)
            {
                OnSubTxFailed(TxFailureReason::FailedToGetParameter, subTxID, true);
            }
            return subTxState;
        }

        if (!GetParameter(TxParameterID::Amount, withdrawAmount, subTxID))
        {
            if (!isTxOwner)
            {
                // we don't have invitation from other side
                return subTxState;
            }
            // initialize withdrawAmount
            withdrawAmount = GetAmount() - withdrawFee;
            SetParameter(TxParameterID::Amount, withdrawAmount, subTxID);
        }

        SharedTxBuilder builder{ *this, subTxID, withdrawAmount, withdrawFee };

        if (!builder.GetSharedParameters())
        {
            return subTxState;
        }

        // send invite to get 
        if (!builder.GetInitialTxParams() && subTxState == SubTxState::Initial)
        {
            builder.InitTx(isTxOwner);
            {
                // validate minHeight
                auto minHeightLockTx = GetMandatoryParameter<Height>(TxParameterID::MinHeight, SubTxIndex::BEAM_LOCK_TX);
                auto minHeight = builder.GetMinHeight();
                if ((SubTxIndex::BEAM_REFUND_TX == subTxID && minHeight != minHeightLockTx + kBeamLockTimeInBlocks) ||
                    (SubTxIndex::BEAM_REDEEM_TX == subTxID && minHeight != minHeightLockTx))
                {
                    OnSubTxFailed(TxFailureReason::MinHeightIsUnacceptable, subTxID, true);
                    return subTxState;
                }
            }
        }

        builder.GenerateNonce();
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
                ConfirmSharedTxInvitation(builder);

                if (subTxID == SubTxIndex::BEAM_REFUND_TX)
                {
                    SetState(SubTxState::Constructed, subTxID);
                    subTxState = SubTxState::Constructed;
                }
            }
            return subTxState;
        }

        if (subTxID == SubTxIndex::BEAM_REDEEM_TX)
        {
            if (IsBeamSide())
            {
                // save SecretPublicKey
                {
                    auto peerPublicNonce = GetMandatoryParameter<Point::Native>(TxParameterID::PeerPublicNonce, subTxID);
                    Scalar::Native challenge;
                    {
                        Point::Native publicNonceNative = builder.GetPublicNonce() + peerPublicNonce;
                        Point publicNonce;
                        publicNonceNative.Export(publicNonce);

                        // Signature::get_Challenge(e, m_NoncePub, msg);
                        uintBig message;
                        builder.GetKernel().get_Hash(message);

                        Oracle() << publicNonce << message >> challenge;
                    }

                    Scalar::Native peerSignature = GetMandatoryParameter<Scalar::Native>(TxParameterID::PeerSignature, subTxID);
                    auto peerPublicExcess = GetMandatoryParameter<Point::Native>(TxParameterID::PeerPublicExcess, subTxID);

                    Point::Native pt = Context::get().G * peerSignature;

                    pt += peerPublicExcess * challenge;
                    pt += peerPublicNonce;
                    assert(!(pt == Zero));

                    Point secretPublicKey;
                    pt.Export(secretPublicKey);

                    SetParameter(TxParameterID::AtomicSwapSecretPublicKey, secretPublicKey, subTxID);
                }

                SetState(SubTxState::Constructed, subTxID);
                return SubTxState::Constructed;
            }
            else
            {
                // Send BTC side partial sign with secret
                auto partialSign = builder.GetPartialSignature();
                Scalar secretPrivateKey;
                GetParameter(TxParameterID::AtomicSwapSecretPrivateKey, secretPrivateKey.m_Value, SubTxIndex::BEAM_REDEEM_TX);
                partialSign += secretPrivateKey;

                SetTxParameter msg;
                msg.AddParameter(TxParameterID::SubTxIndex, builder.GetSubTxID())
                    .AddParameter(TxParameterID::PeerSignature, partialSign);

                if (!SendTxParameters(std::move(msg)))
                {
                    OnFailed(TxFailureReason::FailedToSendParameters, false);
                    return subTxState;
                }
            }
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
            context.m_Height.m_Min = builder.GetMinHeight();
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
            GetGateway().register_tx(GetTxID(), transaction, subTxID);
            return false;
        }

        if (proto::TxStatus::InvalidContext == nRegistered) // we have to ensure that this transaction hasn't already added to blockchain)
        {
            Height lastUnconfirmedHeight = 0;
            if (GetParameter(TxParameterID::KernelUnconfirmedHeight, lastUnconfirmedHeight, subTxID) && lastUnconfirmedHeight > 0)
            {
                OnSubTxFailed(TxFailureReason::FailedToRegister, subTxID, subTxID == SubTxIndex::BEAM_LOCK_TX);
                return false;
            }
        }
        else if (proto::TxStatus::Ok != nRegistered)
        {
            OnSubTxFailed(TxFailureReason::FailedToRegister, subTxID, subTxID == SubTxIndex::BEAM_LOCK_TX);
            return false;
        }

        return true;
    }

    bool AtomicSwapTransaction::IsBeamLockTimeExpired() const
    {
        Height refundMinHeight = MaxHeight;
        GetParameter(TxParameterID::MinHeight, refundMinHeight, SubTxIndex::BEAM_REFUND_TX);

        Block::SystemState::Full state;

        return GetTip(state) && state.m_Height > refundMinHeight;
    }

    bool AtomicSwapTransaction::IsBeamRedeemTxRegistered() const
    {
        uint8_t nRegistered = proto::TxStatus::Unspecified;
        return GetParameter(TxParameterID::TransactionRegistered, nRegistered, SubTxIndex::BEAM_REDEEM_TX);
    }

    bool AtomicSwapTransaction::IsSafeToSendBeamRedeemTx() const
    {
        Height minHeight = MaxHeight;
        GetParameter(TxParameterID::MinHeight, minHeight, SubTxIndex::BEAM_LOCK_TX);

        Block::SystemState::Full state;

        return GetTip(state) && state.m_Height < (minHeight + kMaxSentTimeOfBeamRedeemInBlocks);
    }

    bool AtomicSwapTransaction::CompleteSubTx(SubTxID subTxID)
    {
        Height hProof = 0;
        GetParameter(TxParameterID::KernelProofHeight, hProof, subTxID);
        if (!hProof)
        {
            Merkle::Hash kernelID = GetMandatoryParameter<Merkle::Hash>(TxParameterID::KernelID, subTxID);
            GetGateway().confirm_kernel(GetTxID(), kernelID, subTxID);
            return false;
        }

        if ((SubTxIndex::BEAM_REDEEM_TX == subTxID) || (SubTxIndex::BEAM_REFUND_TX == subTxID))
        {
            // store Coin in DB
            auto amount = GetMandatoryParameter<Amount>(TxParameterID::Amount, subTxID);
            Coin withdrawUtxo(amount);

            withdrawUtxo.m_createTxId = GetTxID();
            withdrawUtxo.m_ID = GetMandatoryParameter<Coin::ID>(TxParameterID::SharedCoinID, subTxID);

            GetWalletDB()->saveCoin(withdrawUtxo);
        }

        SetCompletedTxCoinStatuses(hProof);

        return true;
    }

    bool AtomicSwapTransaction::GetKernelFromChain(SubTxID subTxID) const
    {
        Height hProof = 0;
        GetParameter(TxParameterID::KernelProofHeight, hProof, subTxID);

        if (!hProof)
        {
            Merkle::Hash kernelID = GetMandatoryParameter<Merkle::Hash>(TxParameterID::KernelID, SubTxIndex::BEAM_REDEEM_TX);
            GetGateway().get_kernel(GetTxID(), kernelID, subTxID);
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
        auto swapPublicKey = GetMandatoryParameter<std::string>(TxParameterID::AtomicSwapPublicKey);
        auto swapLockTime = GetMandatoryParameter<Timestamp>(TxParameterID::AtomicSwapExternalLockTime);
        Height beamLockTxMinHeight = GetMandatoryParameter<Height>(TxParameterID::MinHeight, SubTxIndex::BEAM_LOCK_TX);

        // send invitation
        SetTxParameter msg;
        msg.AddParameter(TxParameterID::PeerProtoVersion, s_ProtoVersion)
            .AddParameter(TxParameterID::AtomicSwapPeerPublicKey, swapPublicKey)
            .AddParameter(TxParameterID::AtomicSwapExternalLockTime, swapLockTime)
            .AddParameter(TxParameterID::SubTxIndex, SubTxIndex::BEAM_LOCK_TX)
            .AddParameter(TxParameterID::MinHeight, beamLockTxMinHeight);

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
        auto swapPublicKey = GetMandatoryParameter<std::string>(TxParameterID::AtomicSwapPublicKey);

        SetTxParameter msg;
        msg.AddParameter(TxParameterID::PeerProtoVersion, s_ProtoVersion)
            .AddParameter(TxParameterID::AtomicSwapPeerPublicKey, swapPublicKey)
            .AddParameter(TxParameterID::SubTxIndex, SubTxIndex::BEAM_LOCK_TX)
            .AddParameter(TxParameterID::Fee, lockBuilder.GetFee())
            .AddParameter(TxParameterID::PeerPublicExcess, lockBuilder.GetPublicExcess())
            .AddParameter(TxParameterID::PeerPublicNonce, lockBuilder.GetPublicNonce())
            .AddParameter(TxParameterID::PeerSharedBulletProofPart2, lockBuilder.GetRangeProofInitialPart2())
            .AddParameter(TxParameterID::PeerPublicSharedBlindingFactor, lockBuilder.GetPublicSharedBlindingFactor());

        if (!SendTxParameters(std::move(msg)))
        {
            OnFailed(TxFailureReason::FailedToSendParameters, false);
        }
    }

    void AtomicSwapTransaction::SendLockTxConfirmation(const LockTxBuilder& lockBuilder)
    {
        auto bulletProof = lockBuilder.GetSharedProof();

        SetTxParameter msg;
        msg.AddParameter(TxParameterID::PeerProtoVersion, s_ProtoVersion)
            .AddParameter(TxParameterID::SubTxIndex, SubTxIndex::BEAM_LOCK_TX)
            .AddParameter(TxParameterID::PeerPublicExcess, lockBuilder.GetPublicExcess())
            .AddParameter(TxParameterID::PeerPublicNonce, lockBuilder.GetPublicNonce())
            .AddParameter(TxParameterID::PeerSignature, lockBuilder.GetPartialSignature())
            .AddParameter(TxParameterID::PeerOffset, lockBuilder.GetOffset())
            .AddParameter(TxParameterID::PeerSharedBulletProofPart2, lockBuilder.GetRangeProofInitialPart2())
            .AddParameter(TxParameterID::PeerSharedBulletProofPart3, bulletProof.m_Part3)
            .AddParameter(TxParameterID::PeerPublicSharedBlindingFactor, lockBuilder.GetPublicSharedBlindingFactor());

        if (!SendTxParameters(std::move(msg)))
        {
            OnFailed(TxFailureReason::FailedToSendParameters, false);
        }
    }

    void AtomicSwapTransaction::SendSharedTxInvitation(const BaseTxBuilder& builder)
    {
        SetTxParameter msg;
        msg.AddParameter(TxParameterID::SubTxIndex, builder.GetSubTxID())
            .AddParameter(TxParameterID::Amount, builder.GetAmount())
            .AddParameter(TxParameterID::Fee, builder.GetFee())
            .AddParameter(TxParameterID::MinHeight, builder.GetMinHeight())
            .AddParameter(TxParameterID::PeerPublicExcess, builder.GetPublicExcess())
            .AddParameter(TxParameterID::PeerPublicNonce, builder.GetPublicNonce());
    
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

    void AtomicSwapTransaction::SendQuickRefundPrivateKey()
    {
        NoLeak<uintBig> secretPrivateKey;

        if (GetParameter(TxParameterID::AtomicSwapPrivateKey, secretPrivateKey.V))
        {
            LOG_DEBUG() << GetTxID() << " send additional info for quick refund";

            SetTxParameter msg;

            // send our private key of redeem tx. we are good :)
            msg.AddParameter(TxParameterID::AtomicSwapPeerPrivateKey, secretPrivateKey.V);
            SendTxParameters(std::move(msg));
        }
    }

    void AtomicSwapTransaction::OnSubTxFailed(TxFailureReason reason, SubTxID subTxID, bool notify)
    {
        TxFailureReason previousReason;

        if (GetParameter(TxParameterID::InternalFailureReason, previousReason, subTxID) && previousReason == reason)
        {
            return;
        }

        LOG_ERROR() << GetTxID() << "[" << subTxID << "]" << " Failed. " << GetFailureMessage(reason);

        SetParameter(TxParameterID::InternalFailureReason, reason, false, subTxID);
        OnFailed(TxFailureReason::SubTxFailed, notify);
    }

    void AtomicSwapTransaction::CheckSubTxFailures()
    {
        State state = GetState(kDefaultSubTxID);
        TxFailureReason reason = TxFailureReason::Unknown;

        if ((state == State::Initial ||
            state == State::HandlingContractTX) && GetParameter(TxParameterID::InternalFailureReason, reason, SubTxIndex::LOCK_TX))
        {
            OnFailed(reason, true);
        }
    }

    void AtomicSwapTransaction::ExtractSecretPrivateKey()
    {
        auto subTxID = SubTxIndex::BEAM_REDEEM_TX;
        TxKernel::Ptr kernel = GetMandatoryParameter<TxKernel::Ptr>(TxParameterID::Kernel, subTxID);

        SharedTxBuilder builder{ *this, subTxID };
        builder.GetSharedParameters();
        builder.GetInitialTxParams();
        builder.GetPeerPublicExcessAndNonce();
        builder.GenerateNonce();
        builder.CreateKernel();
        builder.SignPartial();

        Scalar::Native peerSignature = GetMandatoryParameter<Scalar::Native>(TxParameterID::PeerSignature, subTxID);
        Scalar::Native partialSignature = builder.GetPartialSignature();

        Scalar::Native fullSignature;
        fullSignature.Import(kernel->m_Signature.m_k);
        fullSignature = -fullSignature;
        Scalar::Native secretPrivateKeyNative = peerSignature + partialSignature;
        secretPrivateKeyNative += fullSignature;

        Scalar secretPrivateKey;
        secretPrivateKeyNative.Export(secretPrivateKey);

        SetParameter(TxParameterID::AtomicSwapSecretPrivateKey, secretPrivateKey.m_Value, false, BEAM_REDEEM_TX);
    }

} // namespace