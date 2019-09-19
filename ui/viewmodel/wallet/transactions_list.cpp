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

#include "transactions_list.h"

TransactionsList::TransactionsList()
{
}

auto TransactionsList::roleNames() const -> QHash<int, QByteArray>
{
    static const auto roles = QHash<int, QByteArray>
    {
        { static_cast<int>(Roles::TimeCreated), "timeCreated" },
        { static_cast<int>(Roles::TimeCreatedSort), "timeCreatedSort" },
        { static_cast<int>(Roles::AmountSend), "amountSend" },
        { static_cast<int>(Roles::AmountSendSort), "amountSendSort" },
        { static_cast<int>(Roles::AmountReceive), "amountReceive" },
        { static_cast<int>(Roles::AmountReceiveSort), "amountReceiveSort" },
        { static_cast<int>(Roles::AddressFrom), "addressFrom" },
        { static_cast<int>(Roles::AddressFromSort), "addressFromSort" },
        { static_cast<int>(Roles::AddressTo), "addressTo" },
        { static_cast<int>(Roles::AddressToSort), "addressToSort" },
        { static_cast<int>(Roles::Status), "status" },
        { static_cast<int>(Roles::StatusSort), "statusSort" },
        { static_cast<int>(Roles::IsCancelAvailable), "isCancelAvailable" },
        { static_cast<int>(Roles::IsDeleteAvailable), "isDeleteAvailable" },
        { static_cast<int>(Roles::IsSelfTransaction), "isSelfTransaction" },
        { static_cast<int>(Roles::IsIncome), "isIncome" },
        { static_cast<int>(Roles::IsInProgress), "isInProgress" },
        { static_cast<int>(Roles::IsCompleted), "isCompleted" },
        { static_cast<int>(Roles::RawTxID), "rawTxID" }
    };
    return roles;
}

auto TransactionsList::data(const QModelIndex &index, int role) const -> QVariant
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_list.size())
    {
       return QVariant();
    }
    
    auto& value = m_list[index.row()];
    switch (static_cast<Roles>(role))
    {
        case Roles::TimeCreated:
        case Roles::TimeCreatedSort:
            return value->timeCreated();
            
        case Roles::AmountSend:
            return value->getSentAmount();
        case Roles::AmountSendSort:
            return static_cast<double>(value->getSentAmountValue());

        case Roles::AmountReceive:
            return value->getReceivedAmount();
        case Roles::AmountReceiveSort:
            return static_cast<double>(value->getReceivedAmountValue());

        case Roles::AddressFrom:
        case Roles::AddressFromSort:
            return value->getSendingAddress();

        case Roles::AddressTo:
        case Roles::AddressToSort:
            return value->getReceivingAddress();

        case Roles::Status:
        case Roles::StatusSort:
            return value->status();

        case Roles::IsCancelAvailable:
            return value->canCancel();

        case Roles::IsDeleteAvailable:
            return value->canDelete();

        case Roles::IsSelfTransaction:
            return value->isSelfTx();

        case Roles::IsIncome:
            return value->income();

        case Roles::IsInProgress:
            return value->inProgress();

        case Roles::IsCompleted:
            return value->isCompleted();

        case Roles::RawTxID:
            return QVariant::fromValue(value->getTxID());

        default:
            return QVariant();
    }
}
