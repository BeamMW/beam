// Copyright 2020 The Beam Team
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

#include "swap_tx_description.h"

#include "wallet/core/strings_resources.h"

namespace beam::wallet
{
std::string GetSwapTxStatusStr(const TxParameters& txParams)
{
    auto [status, failureReason, sender, selfTx] = ParseParamsForStatusInterpretation(txParams);

    switch (status)
    {
        case wallet::TxStatus::Pending: return "pending";
        case wallet::TxStatus::Registering: return "in progress";
        case wallet::TxStatus::InProgress:
        {
            auto refundConfirmations =
                txParams.GetParameter<uint32_t>(TxParameterID::Confirmations, SubTxIndex::REFUND_TX);
            return refundConfirmations ? "failing": "in progress";
        }
        case wallet::TxStatus::Completed: return "completed";
        case wallet::TxStatus::Canceled: return "canceled";
        case wallet::TxStatus::Failed:
        {
            auto swapFailureReason = txParams.GetParameter<TxFailureReason>(TxParameterID::InternalFailureReason);
            if (swapFailureReason && *swapFailureReason == TxFailureReason::TransactionExpired)
            {
                return "expired";
            }
            return "failed";
        }
        default:
            BOOST_ASSERT_MSG(false, kErrorUnknownTxStatus);
            return "unknown";
    }
}

SwapTxDescription::SwapTxDescription(const TxParameters& txParameters)
    : m_tx(txParameters)
    , m_isBeamSide(*m_tx.GetParameter<bool>(TxParameterID::AtomicSwapIsBeamSide))
    , m_status(TxStatus::Pending)
{
    auto status = m_tx.GetParameter<TxStatus>(TxParameterID::Status);
    if (status)
    {
        m_status = *status;
    }
    assert(*m_tx.GetParameter<TxType>(TxParameterID::TransactionType) == TxType::AtomicSwap);
}

AtomicSwapCoin SwapTxDescription::getSwapCoin() const
{
    auto coin = m_tx.GetParameter<AtomicSwapCoin>(TxParameterID::AtomicSwapCoin);
    assert(coin);
    return *coin;
}

Amount SwapTxDescription::getSwapAmount() const
{
    auto swapAmount = m_tx.GetParameter<Amount>(TxParameterID::AtomicSwapAmount);
    assert(swapAmount);
    return *swapAmount;
}

Height SwapTxDescription::getMinHeight() const
{
    auto minHeight = m_tx.GetParameter<beam::Height>(TxParameterID::MinHeight);
    assert(minHeight);
    return *minHeight;
}

Height SwapTxDescription::getResponseTime() const
{
    auto responseTime = m_tx.GetParameter<beam::Height>(TxParameterID::PeerResponseTime);
    assert(responseTime);
    return *responseTime;
}

boost::optional<std::string> SwapTxDescription::getToken() const
{
    auto isInitiator = m_tx.GetParameter<bool>(TxParameterID::IsInitiator);
    
    if(!isInitiator)
    {
        return boost::none;
    }

    if (*isInitiator == false) 
    {
        const auto& mirroredTxParams = MirrorSwapTxParams(m_tx);
        const auto& readyForTokenizeTxParams =
            PrepareSwapTxParamsForTokenization(mirroredTxParams);
        return std::to_string(readyForTokenizeTxParams);
    }

    const auto& readyForTokenizeTxParams =
            PrepareSwapTxParamsForTokenization(m_tx);
    return std::to_string(readyForTokenizeTxParams);
}

boost::optional<Amount> SwapTxDescription::getFee() const
{
    return m_tx.GetParameter<Amount>(
        TxParameterID::Fee,
        m_isBeamSide ? SubTxIndex::BEAM_LOCK_TX : SubTxIndex::BEAM_REDEEM_TX);
}

boost::optional<Amount> SwapTxDescription::getSwapCoinFeeRate() const
{
    return m_tx.GetParameter<Amount>(
        TxParameterID::Fee,
        m_isBeamSide ? SubTxIndex::REDEEM_TX : SubTxIndex::LOCK_TX);
}

boost::optional<TxFailureReason> SwapTxDescription::getFailureReason() const
{
    if (m_status == wallet::TxStatus::Failed)
    {
        auto failureReason = m_tx.GetParameter<TxFailureReason>(TxParameterID::InternalFailureReason);
        if (failureReason)
        {
            return *failureReason;
        }
        else
        {
            auto extFailureReason = m_tx.GetParameter<TxFailureReason>(TxParameterID::FailureReason);
            if (extFailureReason)
            {
                return *extFailureReason;
            }
        }
    }
    return boost::none;
}

boost::optional<Height> SwapTxDescription::getMinRefundTxHeight() const
{
    return m_tx.GetParameter<Height>(TxParameterID::MinHeight, BEAM_REFUND_TX);
}

boost::optional<Height> SwapTxDescription::getMaxLockTxHeight() const
{
    return m_tx.GetParameter<Height>(TxParameterID::MaxHeight, BEAM_LOCK_TX);
}

boost::optional<Height> SwapTxDescription::getExternalHeight() const
{
    return m_tx.GetParameter<Height>(TxParameterID::AtomicSwapExternalHeight);
}

boost::optional<Height> SwapTxDescription::getExternalLockTime() const
{
    return m_tx.GetParameter<Height>(TxParameterID::AtomicSwapExternalLockTime);
}

boost::optional<AtomicSwapTransaction::State> SwapTxDescription::getState() const
{
    return m_tx.GetParameter<AtomicSwapTransaction::State>(TxParameterID::State);
}

/**
 * Indicates on which side of atomic swap we are.
 * true - if sending Beams to get another coin.
 * false - if receiving Beams in trade for another coin.
 */
bool SwapTxDescription::isBeamSide() const
{
    return m_isBeamSide;
}

bool SwapTxDescription::isFailed() const
{
    auto failureReason = m_tx.GetParameter<TxFailureReason>(TxParameterID::InternalFailureReason);
    return  m_status == wallet::TxStatus::Failed &&
            !failureReason;
}

bool SwapTxDescription::isExpired() const
{
    auto failureReason = m_tx.GetParameter<TxFailureReason>(TxParameterID::InternalFailureReason);
    return  m_status == wallet::TxStatus::Failed &&
            failureReason &&
            *failureReason == TxFailureReason::TransactionExpired;
}

bool SwapTxDescription::isRefunded() const
{
    if (m_status == wallet::TxStatus::Failed)
    {
        auto failureReason = m_tx.GetParameter<TxFailureReason>(TxParameterID::InternalFailureReason);
        auto txState = getState();
        return !failureReason && txState && *txState == wallet::AtomicSwapTransaction::State::Refunded;
    }
    return false;
}

bool SwapTxDescription::isCancelAvailable() const
{
    auto txState = getState();
    if (txState)
    {
        switch (*txState)
        {
            case wallet::AtomicSwapTransaction::State::Initial:
            case wallet::AtomicSwapTransaction::State::BuildingBeamLockTX:
            case wallet::AtomicSwapTransaction::State::BuildingBeamRedeemTX:
            case wallet::AtomicSwapTransaction::State::BuildingBeamRefundTX:
            {
                return true;
            }
            case wallet::AtomicSwapTransaction::State::HandlingContractTX:
            {
                return m_isBeamSide;
            }
            default:
                break;
        }
    }
    return m_status == wallet::TxStatus::Pending;
}

bool SwapTxDescription::isRedeemTxRegistered() const
{
    uint8_t registered;
    return m_tx.GetParameter(TxParameterID::TransactionRegistered, registered, m_isBeamSide ? REDEEM_TX : BEAM_REDEEM_TX);
}

bool SwapTxDescription::isRefundTxRegistered() const
{
    uint8_t registered;
    return m_tx.GetParameter(TxParameterID::TransactionRegistered, registered, m_isBeamSide ? BEAM_REFUND_TX : REFUND_TX);
}

bool SwapTxDescription::isLockTxProofReceived() const
{
    Height proofHeight;
    return m_tx.GetParameter(TxParameterID::KernelProofHeight, proofHeight, SubTxIndex::BEAM_LOCK_TX);
}

bool SwapTxDescription::isRefundTxProofReceived() const
{
    Height proofHeight;
    return m_tx.GetParameter(TxParameterID::KernelProofHeight, proofHeight, SubTxIndex::BEAM_REFUND_TX);
}

}   // namespace beam::wallet
