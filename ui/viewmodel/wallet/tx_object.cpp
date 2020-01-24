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
#include "wallet/core/common.h"
#include "wallet/core/simple_transaction.h"
#include "model/app_model.h"

using namespace beam;
using namespace beam::wallet;
using namespace beamui;

namespace
{
    QString getWaitingPeerStr(const beam::wallet::TxParameters& txParameters, bool isSender)
    {
        auto minHeight = txParameters.GetParameter<beam::Height>(TxParameterID::MinHeight);
        auto responseTime = txParameters.GetParameter<beam::Height>(TxParameterID::PeerResponseTime);
        QString time = "";
        if (minHeight && responseTime)
        {
            time = convertBeamHeightDiffToTime(*minHeight + *responseTime - AppModel::getInstance().getWallet()->getCurrentHeight());
        }
        if (isSender)
        {
            //% "If the receiver won't get online in %1, the transaction will be canceled automatically"
            return qtTrId("tx-state-initial-sender").arg(time);
        }
        //% "If the sender won't get online in %1, the transaction will be canceled automatically"
        return qtTrId("tx-state-initial-receiver").arg(time);
    }

    QString getInProgressStr(const beam::wallet::TxParameters& txParameters)
    {
        const Height kNormalTxConfirmationDelay = 10;
        auto maxHeight = txParameters.GetParameter<beam::Height>(TxParameterID::MaxHeight);
        QString time = "";
        if (!maxHeight)
        {
            return "";
        }

        auto currentHeight = AppModel::getInstance().getWallet()->getCurrentHeight();
        if (currentHeight >= *maxHeight)
        {
            return "";
        }

        Height delta =  *maxHeight - currentHeight;
        auto lifetime = txParameters.GetParameter<beam::Height>(TxParameterID::Lifetime);
        if (!lifetime || *lifetime < delta)
        {
            return "";
        }

        if (*lifetime - delta <= kNormalTxConfirmationDelay)
        {
            //% "The transaction is usually expected to complete in a few minutes."
            return qtTrId("tx-state-in-progress-normal");
        }

        time = beamui::convertBeamHeightDiffToTime(delta);
        if (time.isEmpty())
        {
            return "";
        }
        //% "It is taking longer than usual. In case the transaction could not be completed it will be canceled automatically in %1."
        return qtTrId("tx-state-in-progress-long").arg(time);
    }
}

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

bool TxObject::operator==(const TxObject& other) const
{
    return getTxID() == other.getTxID();
}

auto TxObject::timeCreated() const -> beam::Timestamp
{
    return m_tx.m_createTime;
}

auto TxObject::getTxID() const -> beam::wallet::TxID
{
    return m_tx.m_txId;
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

QString TxObject::getAmountWithCurrency() const
{
    return AmountToUIString(m_tx.m_amount, Currencies::Beam);
}

QString TxObject::getAmount() const
{
    return AmountToUIString(m_tx.m_amount);
}

beam::Amount TxObject::getAmountValue() const
{
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
        return AmountInGrothToUIString(m_tx.m_fee);
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

QString TxObject::getReasonString(beam::wallet::TxFailureReason reason) const
{
    const std::array<QString,31> reasons = {
        //% "Unexpected reason, please send wallet logs to Beam support"
        qtTrId("tx-failure-undefined"),
        //% "Transaction cancelled"
        qtTrId("tx-failure-cancelled"),
        //% "Receiver signature in not valid, please send wallet logs to Beam support"
        qtTrId("tx-failure-receiver-signature-invalid"),
        //% "Failed to register transaction with the blockchain, see node logs for details"
        qtTrId("tx-failure-not-registered-in-blockchain"),
        //% "Transaction is not valid, please send wallet logs to Beam support"
        qtTrId("tx-failure-not-valid"),
        //% "Invalid kernel proof provided"
        qtTrId("tx-failure-kernel-invalid"),
        //% "Failed to send Transaction parameters"
        qtTrId("tx-failure-parameters-not-sended"),
        //% "No inputs"
        qtTrId("tx-failure-no-inputs"),
        //% "Address is expired"
        qtTrId("tx-failure-addr-expired"),
        //% "Failed to get transaction parameters"
        qtTrId("tx-failure-parameters-not-readed"),
        //% "Transaction timed out"
        qtTrId("tx-failure-time-out"),
        //% "Payment not signed by the receiver, please send wallet logs to Beam support"
        qtTrId("tx-failure-not-signed-by-receiver"),
        //% "Kernel maximum height is too high"
        qtTrId("tx-failure-max-height-to-high"),
        //% "Transaction has invalid state"
        qtTrId("tx-failure-invalid-state"),
        //% "Subtransaction has failed"
        qtTrId("tx-failure-subtx-failed"),
        //% "Contract's amount is not valid"
        qtTrId("tx-failure-invalid-contract-amount"),
        //% "Side chain has invalid contract"
        qtTrId("tx-failure-invalid-sidechain-contract"),
        //% "Side chain bridge has internal error"
        qtTrId("tx-failure-sidechain-internal-error"),
        //% "Side chain bridge has network error"
        qtTrId("tx-failure-sidechain-network-error"),
        //% "Side chain bridge has response format error"
        qtTrId("tx-failure-invalid-sidechain-response-format"),
        //% "Invalid credentials of Side chain"
        qtTrId("tx-failure-invalid-side-chain-credentials"),
        //% "Not enough time to finish btc lock transaction"
        qtTrId("tx-failure-not-enough-time-btc-lock"),
        //% "Failed to create multi-signature"
        qtTrId("tx-failure-create-multisig"),
        //% "Fee is too small"
        qtTrId("tx-failure-fee-too-small"),
        //% "Kernel's min height is unacceptable"
        qtTrId("tx-failure-kernel-min-height"),
        //% "Not a loopback transaction"
        qtTrId("tx-failure-loopback"),
        //% "Key keeper is not initialized"
        qtTrId("tx-failure-key-keeper-no-initialized"),
        //% "No valid asset id/asset idx"
        qtTrId("tx-failure-invalid-asset-id"),
        //% "Cannot consume more than MAX_INT64 asset groth in one transaction"
        qtTrId("tx-failure-invalid-asset-amount"),
        //% "Some mandatory data for payment proof is missing"
        qtTrId("tx-failure-invalid-data-for-payment-proof")
    };
    assert(reasons.size() > static_cast<size_t>(reason));
    return reasons[reason];
}

QString TxObject::getFailureReason() const
{
    // TODO: add support for other transactions
    if (getTxDescription().m_status == wallet::TxStatus::Failed && getTxDescription().m_txType == beam::wallet::TxType::Simple)
    {
        return getReasonString(getTxDescription().m_failureReason);
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

QString TxObject::getStateDetails() const
{
    auto& tx = getTxDescription();
    if (tx.m_txType == beam::wallet::TxType::Simple)
    {
        switch (tx.m_status)
        {
        case beam::wallet::TxStatus::Pending:
        case beam::wallet::TxStatus::InProgress:
        {
            auto state = getTxDescription().GetParameter<wallet::SimpleTransaction::State>(TxParameterID::State);
            if (state)
            {
                switch (*state)
                {
                case wallet::SimpleTransaction::Initial:
                case wallet::SimpleTransaction::Invitation:
                    return getWaitingPeerStr(tx, tx.m_sender);
                default:
                    break;
                }
            }
            return getWaitingPeerStr(tx, tx.m_sender);
        }
        case beam::wallet::TxStatus::Registering:
            return getInProgressStr(tx);
        default:
            break;
        }
    }
    return "";
}

bool TxObject::hasPaymentProof() const
{
    return !isIncome() && m_tx.m_status == wallet::TxStatus::Completed;
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
        case wallet::TxStatus::Pending:
        case wallet::TxStatus::InProgress:
        case wallet::TxStatus::Registering:
            return true;
        default:
            return false;
    }
}

bool TxObject::isPending() const
{
    return m_tx.m_status == wallet::TxStatus::Pending;
}

bool TxObject::isCompleted() const
{
    return m_tx.m_status == wallet::TxStatus::Completed;
}

bool TxObject::isSelfTx() const
{
    return m_tx.m_selfTx;
}

bool TxObject::isCanceled() const
{
    return m_tx.m_status == wallet::TxStatus::Canceled;
}

bool TxObject::isFailed() const
{
    return m_tx.m_status == wallet::TxStatus::Failed;
}

bool TxObject::isExpired() const
{
    return isFailed() && m_tx.m_failureReason == TxFailureReason::TransactionExpired;
}
