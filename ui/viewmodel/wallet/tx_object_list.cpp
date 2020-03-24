// Copyright 2019 The Beam Team
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

#include "tx_object_list.h"

namespace
{

QString getStatusTextTranslated(const QString& status)
{
    if (status == "pending")
    {
        //% "pending"
        return qtTrId("wallet-txs-status-pending");
    }
    else if (status == "waiting for sender")
    {
        //% "waiting for sender"
        return qtTrId("wallet-txs-status-waiting-sender");
    }
    else if (status == "waiting for receiver")
    {
        //% "waiting for receiver"
        return qtTrId("wallet-txs-status-waiting-receiver");
    }
    else if (status == "in progress")
    {
        //% "in progress"
        return qtTrId("wallet-txs-status-in-progress");
    }
    else if (status == "sent to own address")
    {
        //% "sent to own address"
        return qtTrId("wallet-txs-status-own-sent");
    }
    else if (status == "sending to own address")
    {
        //% "sending to own address"
        return qtTrId("wallet-txs-status-own-sending");
    }
    else if (status == "received")
    {
        //% "received"
        return qtTrId("wallet-txs-status-received");
    }
    else if (status == "sent")
    {
        //% "sent"
        return qtTrId("wallet-txs-status-sent");
    }
    else if (status == "cancelled")
    {
        //% "cancelled"
        return qtTrId("wallet-txs-status-cancelled");
    }
    else if (status == "expired")
    {
        //% "expired"
        return qtTrId("wallet-txs-status-expired");
    }
    else if (status == "failed")
    {
        //% "failed"
        return qtTrId("wallet-txs-status-failed");
    }

    else
    {
        //% "unknown"
        return qtTrId("wallet-txs-status-unknown");
    }
}

}  // namespace

TxObjectList::TxObjectList()
{
}

QHash<int, QByteArray> TxObjectList::roleNames() const
{
    static const auto roles = QHash<int, QByteArray>
    {
        { static_cast<int>(Roles::TimeCreated), "timeCreated" },
        { static_cast<int>(Roles::TimeCreatedSort), "timeCreatedSort" },
        { static_cast<int>(Roles::AmountGeneralWithCurrency), "amountGeneralWithCurrency" },
        { static_cast<int>(Roles::AmountGeneralWithCurrencySort), "amountGeneralWithCurrencySort" },
        { static_cast<int>(Roles::AmountGeneral), "amountGeneral" },
        { static_cast<int>(Roles::AmountGeneralSort), "amountGeneralSort" },
        { static_cast<int>(Roles::SecondCurrencyRate), "secondCurrencyRate" },
        { static_cast<int>(Roles::AddressFrom), "addressFrom" },
        { static_cast<int>(Roles::AddressFromSort), "addressFromSort" },
        { static_cast<int>(Roles::AddressTo), "addressTo" },
        { static_cast<int>(Roles::AddressToSort), "addressToSort" },
        { static_cast<int>(Roles::Status), "status" },
        { static_cast<int>(Roles::StatusSort), "statusSort" },
        { static_cast<int>(Roles::Fee), "fee" },
        { static_cast<int>(Roles::Comment), "comment" },
        { static_cast<int>(Roles::TxID), "txID" },
        { static_cast<int>(Roles::KernelID), "kernelID" },
        { static_cast<int>(Roles::FailureReason), "failureReason" },
        { static_cast<int>(Roles::IsCancelAvailable), "isCancelAvailable" },
        { static_cast<int>(Roles::IsDeleteAvailable), "isDeleteAvailable" },
        { static_cast<int>(Roles::IsSelfTransaction), "isSelfTransaction" },
        { static_cast<int>(Roles::IsIncome), "isIncome" },
        { static_cast<int>(Roles::IsInProgress), "isInProgress" },
        { static_cast<int>(Roles::IsPending), "isPending" },
        { static_cast<int>(Roles::IsCompleted), "isCompleted" },
        { static_cast<int>(Roles::IsCanceled), "isCanceled" },
        { static_cast<int>(Roles::IsFailed), "isFailed" },
        { static_cast<int>(Roles::IsExpired), "isExpired" },
        { static_cast<int>(Roles::HasPaymentProof), "hasPaymentProof" },
        { static_cast<int>(Roles::RawTxID), "rawTxID" },
        { static_cast<int>(Roles::Search), "search" },
        { static_cast<int>(Roles::StateDetails), "stateDetails" },
        { static_cast<int>(Roles::Token), "token" },
        { static_cast<int>(Roles::SenderIdentity), "senderIdentity"},
        { static_cast<int>(Roles::ReceiverIdentity), "receiverIdentity"},
    };
    return roles;
}

QVariant TxObjectList::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_list.size())
    {
       return QVariant();
    }
    
    auto& value = m_list[index.row()];
    switch (static_cast<Roles>(role))
    {
        case Roles::TimeCreated:
        {
            QDateTime datetime;
            datetime.setTime_t(value->timeCreated());
            return datetime.toString(Qt::SystemLocaleShortDate);
        }
            
        case Roles::TimeCreatedSort:
        {
            return static_cast<qulonglong>(value->timeCreated());
        }

        case Roles::AmountGeneralWithCurrency:
            return value->getAmountWithCurrency();
        case Roles::AmountGeneralWithCurrencySort:
            return static_cast<qulonglong>(value->getAmountValue());
        case Roles::AmountGeneral:
            return value->getAmount();
        case Roles::AmountGeneralSort:
            return static_cast<qulonglong>(value->getAmountValue());
        case Roles::SecondCurrencyRate:
            return value->getSecondCurrencyRate();
            
        case Roles::AddressFrom:
        case Roles::AddressFromSort:
            return value->getAddressFrom();

        case Roles::AddressTo:
        case Roles::AddressToSort:
            return value->getAddressTo();

        case Roles::Status:
        case Roles::StatusSort:
            return getStatusTextTranslated(value->getStatus());

        case Roles::Fee:
            return value->getFee();

        case Roles::Comment:
            return value->getComment();

        case Roles::TxID:
            return value->getTransactionID();

        case Roles::KernelID:
            return value->getKernelID();

        case Roles::FailureReason:
            return value->getFailureReason();

        case Roles::IsCancelAvailable:
            return value->isCancelAvailable();

        case Roles::IsDeleteAvailable:
            return value->isDeleteAvailable();

        case Roles::IsSelfTransaction:
            return value->isSelfTx();

        case Roles::IsIncome:
            return value->isIncome();

        case Roles::IsInProgress:
            return value->isInProgress();

        case Roles::IsPending:
            return value->isPending();

        case Roles::IsCompleted:
            return value->isCompleted();

        case Roles::IsCanceled:
            return value->isCanceled();

        case Roles::IsFailed:
            return value->isFailed();

        case Roles::IsExpired:
            return value->isExpired();

        case Roles::HasPaymentProof:
            return value->hasPaymentProof();

        case Roles::RawTxID:
            return QVariant::fromValue(value->getTxID());

        case Roles::Search: 
        {
            QString r = value->getTransactionID();
            r.append(" ");
            r.append(value->getKernelID());
            r.append(" ");
            r.append(value->getAddressFrom());
            r.append(" ");
            r.append(value->getAddressTo());
            r.append(" ");
            r.append(value->getComment());
            r.append(" ");
            r.append(value->getSenderIdentity());
            r.append(" ");
            r.append(value->getReceiverIdentity());
            return r;
        }
        case Roles::StateDetails:
            return value->getStateDetails();
        case Roles::Token:
            return value->getToken();
        case Roles::SenderIdentity:
            return value->getSenderIdentity();
        case Roles::ReceiverIdentity:
            return value->getReceiverIdentity();

        default:
            return QVariant();
    }
}