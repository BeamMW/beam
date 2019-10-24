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

QString TxObject::getAmountValue() const
{
    return beamui::AmountToString(m_tx.m_amount);
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
