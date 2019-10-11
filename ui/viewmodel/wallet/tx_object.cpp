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
#include "tx_object.h"
#include "viewmodel/ui_helpers.h"
#include "wallet/common.h"
#include "wallet/swaps/common.h"

using namespace beam;
using namespace beam::wallet;
using namespace beamui;

TxObject::TxObject(QObject* parent)
        : QObject(parent)
{
}

TxObject::TxObject(const TxDescription& tx, QObject* parent/* = nullptr*/)
        : QObject(parent)
        , m_tx(tx)
        , m_type(*m_tx.GetParameter<TxType>(TxParameterID::TransactionType))
{
    auto kernelID = QString::fromStdString(to_hex(m_tx.m_kernelID.m_pData, m_tx.m_kernelID.nBytes));
    setKernelID(kernelID);
}

auto TxObject::timeCreated() const -> QDateTime
{
	QDateTime datetime;
	datetime.setTime_t(m_tx.m_createTime);
	return datetime;
}

auto TxObject::getTxID() const -> beam::wallet::TxID
{
    return m_tx.m_txId;
}

auto TxObject::isBeamSideSwap() const -> bool
{
    auto isBeamSide = m_tx.GetParameter<bool>(TxParameterID::AtomicSwapIsBeamSide);
    if (isBeamSide)
    {
        return isBeamSide.value();
    }
    else return false;    
}

auto TxObject::getSwapCoinName() const -> QString
{
    beam::wallet::AtomicSwapCoin coin;
    if (m_tx.GetParameter(TxParameterID::AtomicSwapCoin, coin))
    {
        switch (coin)
        {
            case AtomicSwapCoin::Bitcoin:   return toString(beamui::Currencies::Bitcoin);
            case AtomicSwapCoin::Litecoin:  return toString(beamui::Currencies::Litecoin);
            case AtomicSwapCoin::Qtum:      return toString(beamui::Currencies::Qtum);
            case AtomicSwapCoin::Unknown:   return toString(beamui::Currencies::Unknown);
        }
    }
    return QString("unknown");
}

bool TxObject::isIncome() const
{
    return m_tx.m_sender == false;
}

QString TxObject::getComment() const
{
    std::string str{ m_tx.m_message.begin(), m_tx.m_message.end() };
    return QString(str.c_str()).trimmed();
}

QString TxObject::getAmount() const
{
    return AmountToString(m_tx.m_amount, Currencies::Beam);
}

double TxObject::getAmountValue() const
{
    return m_tx.m_amount;
}

QString TxObject::getSentAmount() const
{
    if (m_type == TxType::AtomicSwap)
    {
        return getSwapAmount(true);
    }
    return m_tx.m_sender ? getAmount() : "";
}

double TxObject::getSentAmountValue() const
{
    if (m_type == TxType::AtomicSwap)
    {
        return getSwapAmountValue(true);
    }

    return m_tx.m_sender ? m_tx.m_amount : 0;
}

QString TxObject::getReceivedAmount() const
{
    if (m_type == TxType::AtomicSwap)
    {
        return getSwapAmount(false);
    }
    return !m_tx.m_sender ? getAmount() : "";
}

double TxObject::getReceivedAmountValue() const
{
    if (m_type == TxType::AtomicSwap)
    {
        return getSwapAmountValue(false);
    }

    return !m_tx.m_sender ? m_tx.m_amount : 0;
}

QString TxObject::getSwapAmount(bool sent) const
{
    auto isBeamSide = m_tx.GetParameter<bool>(TxParameterID::AtomicSwapIsBeamSide);
    if (!isBeamSide)
    {
        return "";
    }

    bool s = sent ? !*isBeamSide : *isBeamSide;
    if (s)
    {
        auto swapAmount = m_tx.GetParameter<Amount>(TxParameterID::AtomicSwapAmount);
        if (swapAmount)
        {
            auto swapCoin = m_tx.GetParameter<AtomicSwapCoin>(TxParameterID::AtomicSwapCoin);
            return AmountToString(*swapAmount, beamui::convertSwapCoinToCurrency(*swapCoin));
        }
        return "";
    }
    return getAmount();
}

double TxObject::getSwapAmountValue(bool sent) const
{
    auto isBeamSide = m_tx.GetParameter<bool>(TxParameterID::AtomicSwapIsBeamSide);
    if (!isBeamSide)
    {
        return 0.0;
    }

    bool s = sent ? !*isBeamSide : *isBeamSide;
    if (s)
    {
        auto swapAmount = m_tx.GetParameter<Amount>(TxParameterID::AtomicSwapAmount);
        if (swapAmount)
        {
            return *swapAmount;
        }
        return 0.0;
    }
    return m_tx.m_amount;
}

QString TxObject::getStatus() const
{
    return m_tx.getStatusString().c_str();
}

bool TxObject::isCancelAvailable() const
{
    return m_tx.canCancel();
}

bool TxObject::isDeleteAvailable() const
{
    return m_tx.canDelete();
}

QString TxObject::getAddressFrom() const
{
    return toString(m_tx.m_sender ? m_tx.m_myId : m_tx.m_peerId);
}

QString TxObject::getAddressTo() const
{
    return toString(!m_tx.m_sender ? m_tx.m_myId : m_tx.m_peerId);
}

QString TxObject::getFee() const
{
    if (m_tx.m_fee)
    {
        return AmountToString(m_tx.m_fee, Currencies::Beam);
    }
    return QString{};
}

const beam::wallet::TxDescription& TxObject::getTxDescription() const
{
    return m_tx;
}

void TxObject::setStatus(beam::wallet::TxStatus status)
{
    if (m_tx.m_status != status)
    {
        m_tx.m_status = status;
        emit statusChanged();
    }
}

QString TxObject::getKernelID() const
{
    return m_kernelID;
}

void TxObject::setKernelID(const QString& value)
{
    if (m_kernelID != value)
    {
        m_kernelID = value;
        emit kernelIDChanged();
    }
}

QString TxObject::getTransactionID() const
{
    return QString::fromStdString(to_hex(m_tx.m_txId.data(), m_tx.m_txId.size()));
}

QString TxObject::getFailureReason() const
{
    // TODO: add support for other transactions
    if (getTxDescription().m_status == TxStatus::Failed && getTxDescription().m_txType == beam::wallet::TxType::Simple)
    {
        QString Reasons[] =
                {
                        //% "Unexpected reason, please send wallet logs to Beam support"
                        qtTrId("tx-failture-undefined"),
                        //% "Transaction cancelled"
                        qtTrId("tx-failture-cancelled"),
                        //% "Receiver signature in not valid, please send wallet logs to Beam support"
                        qtTrId("tx-failture-receiver-signature-invalid"),
                        //% "Failed to register transaction with the blockchain, see node logs for details"
                        qtTrId("tx-failture-not-registered-in-blockchain"),
                        //% "Transaction is not valid, please send wallet logs to Beam support"
                        qtTrId("tx-failture-not-valid"),
                        //% "Invalid kernel proof provided"
                        qtTrId("tx-failture-kernel-invalid"),
                        //% "Failed to send Transaction parameters"
                        qtTrId("tx-failture-parameters-not-sended"),
                        //% "No inputs"
                        qtTrId("tx-failture-no-inputs"),
                        //% "Address is expired"
                        qtTrId("tx-failture-addr-expired"),
                        //% "Failed to get transaction parameters"
                        qtTrId("tx-failture-parameters-not-readed"),
                        //% "Transaction timed out"
                        qtTrId("tx-failture-time-out"),
                        //% "Payment not signed by the receiver, please send wallet logs to Beam support"
                        qtTrId("tx-failture-not-signed-by-receiver"),
                        //% "Kernel maximum height is too high"
                        qtTrId("tx-failture-max-height-to-high"),
                        //% "Transaction has invalid state"
                        qtTrId("tx-failture-invalid-state"),
                        //% "Subtransaction has failed"
                        qtTrId("tx-failture-subtx-failed"),
                        //% "Contract's amount is not valid"
                        qtTrId("tx-failture-invalid-contract-amount"),
                        //% "Side chain has invalid contract"
                        qtTrId("tx-failture-invalid-sidechain-contract"),
                        //% "Side chain bridge has internal error"
                        qtTrId("tx-failture-sidechain-internal-error"),
                        //% "Side chain bridge has network error"
                        qtTrId("tx-failture-sidechain-network-error"),
                        //% "Side chain bridge has response format error"
                        qtTrId("tx-failture-invalid-sidechain-response-format"),
                        //% "Invalid credentials of Side chain"
                        qtTrId("tx-failture-invalid-side-chain-credentials"),
                        //% "Not enough time to finish btc lock transaction"
                        qtTrId("tx-failture-not-enough-time-btc-lock"),
                        //% "Failed to create multi-signature"
                        qtTrId("tx-failture-create-multisig"),
                        //% "Fee is too small"
                        qtTrId("tx-failture-fee-too-small")
                };

        return Reasons[getTxDescription().m_failureReason];
    }

    return QString();
}

void TxObject::setFailureReason(beam::wallet::TxFailureReason reason)
{
    if (m_tx.m_failureReason != reason)
    {
        m_tx.m_failureReason = reason;
        emit failureReasonChanged();
    }
}

bool TxObject::hasPaymentProof() const
{
    return !isIncome() && m_tx.m_status == TxStatus::Completed;
}

void TxObject::update(const beam::wallet::TxDescription& tx)
{
    setStatus(tx.m_status);
    auto kernelID = QString::fromStdString(to_hex(tx.m_kernelID.m_pData, tx.m_kernelID.nBytes));
    setKernelID(kernelID);
    setFailureReason(tx.m_failureReason);
}

bool TxObject::isInProgress() const
{
    switch (m_tx.m_status)
    {
        case TxStatus::Pending:
        case TxStatus::InProgress:
        case TxStatus::Registering:
            return true;
        default:
            return false;
    }
}

bool TxObject::isPending() const
{
    return m_tx.m_status == TxStatus::Pending;
}

bool TxObject::isCompleted() const
{
    return m_tx.m_status == TxStatus::Completed;
}

bool TxObject::isSelfTx() const
{
    return m_tx.m_selfTx;
}

bool TxObject::isCanceled() const
{
    return m_tx.m_status == TxStatus::Canceled;
}

bool TxObject::isFailed() const
{
    return m_tx.m_status == TxStatus::Failed;
}

bool TxObject::isExpired() const
{
    return isFailed() && m_tx.m_failureReason == TxFailureReason::TransactionExpired;
}

namespace
{
    template<typename T>
    void copyParameter(TxParameterID id, const TxParameters& source, TxParameters& dest)
    {
        if (auto p = source.GetParameter<T>(id); p)
        {
            dest.SetParameter(id, *p);
        }
    }

    void copyParameter(TxParameterID id, const TxParameters& source, TxParameters& dest, bool inverse = false)
    {
        if (auto p = source.GetParameter<bool>(id); p)
        {
            dest.SetParameter(id, inverse ? !*p : *p);
        }
    }

    template<size_t V>
    QString getSwapCoinTxId(const TxParameters& source)
    {
        if (auto res = source.GetParameter<std::string>(TxParameterID::AtomicSwapExternalTxID, V))
        {
            return QString(res->c_str());
        }
        else return QString();
    }
    
    template<size_t V>
    QString getSwapCoinTxConfirmations(const TxParameters& source)
    {
        if (auto res = source.GetParameter<uint32_t>(TxParameterID::Confirmations, V))
        {
            auto n = std::to_string(*res);
            return QString::fromStdString(n);
        }
        else return QString();
    }

    template<size_t V>
    QString getBeamTxKernelId(const TxParameters& source)
    {
        if (auto res = source.GetParameter<Merkle::Hash>(TxParameterID::KernelID, V))
        {
            return QString::fromStdString(to_hex(res->m_pData, res->nBytes));
        }
        else return QString();
    }
}

QString TxObject::getToken() const
{
    if (m_type != TxType::AtomicSwap)
    {
        return "";
    }

    TxParameters tokenParams(m_tx.m_txId);

    auto isInitiator = m_tx.GetParameter<bool>(TxParameterID::IsInitiator);
    if (*isInitiator == false) 
    {
        if (auto p = m_tx.GetParameter<WalletID>(TxParameterID::MyID); p)
        {
            tokenParams.SetParameter(TxParameterID::PeerID, *p);
        }
    }
    else
    {
        copyParameter<WalletID>(TxParameterID::PeerID, m_tx, tokenParams);
    }

    tokenParams.SetParameter(TxParameterID::IsInitiator, true);

    copyParameter(TxParameterID::IsSender, m_tx, tokenParams, !*isInitiator);
    copyParameter(TxParameterID::AtomicSwapIsBeamSide, m_tx, tokenParams, !*isInitiator);

    tokenParams.SetParameter(beam::wallet::TxParameterID::TransactionType, m_type);
    copyParameter<Height>(TxParameterID::MinHeight, m_tx, tokenParams);
    copyParameter<Height>(TxParameterID::PeerResponseTime, m_tx, tokenParams);
    copyParameter<Timestamp>(TxParameterID::CreateTime, m_tx, tokenParams);
    copyParameter<Height>(TxParameterID::Lifetime, m_tx, tokenParams);

    copyParameter<Amount>(TxParameterID::Amount, m_tx, tokenParams);
    copyParameter<Amount>(TxParameterID::AtomicSwapAmount, m_tx, tokenParams);
    copyParameter<AtomicSwapCoin>(TxParameterID::AtomicSwapCoin, m_tx, tokenParams);

    return QString::fromStdString(std::to_string(tokenParams));
}

bool TxObject::isProofReceived() const
{
    Height proofHeight;
    if (m_tx.GetParameter(TxParameterID::KernelProofHeight, proofHeight, SubTxIndex::BEAM_LOCK_TX))
    {
        return true;
    }
    else return false;
}

QString TxObject::getSwapCoinLockTxId() const
{
    return getSwapCoinTxId<SubTxIndex::LOCK_TX>(m_tx);
}

QString TxObject::getSwapCoinRedeemTxId() const
{
    return getSwapCoinTxId<SubTxIndex::REDEEM_TX>(m_tx);
}

QString TxObject::getSwapCoinRefundTxId() const
{
    return getSwapCoinTxId<SubTxIndex::REFUND_TX>(m_tx);
}

QString TxObject::getSwapCoinLockTxConfirmations() const
{
    return getSwapCoinTxConfirmations<SubTxIndex::LOCK_TX>(m_tx);
}

QString TxObject::getSwapCoinRedeemTxConfirmations() const
{
    return getSwapCoinTxConfirmations<SubTxIndex::REDEEM_TX>(m_tx);
}

QString TxObject::getSwapCoinRefundTxConfirmations() const
{
    return getSwapCoinTxConfirmations<SubTxIndex::REFUND_TX>(m_tx);
}

QString TxObject::getBeamLockTxKernelId() const
{
    return getBeamTxKernelId<SubTxIndex::BEAM_LOCK_TX>(m_tx);
}

QString TxObject::getBeamRedeemTxKernelId() const
{
    return getBeamTxKernelId<SubTxIndex::REDEEM_TX>(m_tx);
}

QString TxObject::getBeamRefundTxKernelId() const
{
    return getBeamTxKernelId<SubTxIndex::REFUND_TX>(m_tx);
}
