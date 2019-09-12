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
        { static_cast<int>(Roles::TimeCreatedRole), "timeCreated" },
        { static_cast<int>(Roles::TimeCreatedSortRole), "timeCreatedSort" },
        { static_cast<int>(Roles::AmountSendRole), "amountSend" },
        { static_cast<int>(Roles::AmountSendSortRole), "amountSendSort" },
        { static_cast<int>(Roles::AmountReceiveRole), "amountReceive" },
        { static_cast<int>(Roles::AmountReceiveSortRole), "amountReceiveSort" },
        { static_cast<int>(Roles::AddressFromRole), "addressFrom" },
        { static_cast<int>(Roles::AddressFromSortRole), "addressFromSort" },
        { static_cast<int>(Roles::AddressToRole), "addressTo" },
        { static_cast<int>(Roles::AddressToSortRole), "addressToSort" },
        { static_cast<int>(Roles::StatusRole), "status" },
        { static_cast<int>(Roles::StatusSortRole), "statusSort" },
        { static_cast<int>(Roles::RawTxParametersRole), "rawTxParameters" }
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
        case Roles::TimeCreatedRole:
        case Roles::TimeCreatedSortRole:
            return value->timeCreated();
            
        case Roles::AmountSendRole:
            return value->getSentAmount();
        case Roles::AmountSendSortRole:
            return static_cast<double>(value->getSentAmountValue());

        case Roles::AmountReceiveRole:
            return value->getReceivedAmount();
        case Roles::AmountReceiveSortRole:
            return static_cast<double>(value->getReceivedAmountValue());

        case Roles::AddressFromRole:
        case Roles::AddressFromSortRole:
            return value->getSendingAddress();

        case Roles::AddressToRole:
        case Roles::AddressToSortRole:
            return value->getReceivingAddress();

        case Roles::StatusRole:
        case Roles::StatusSortRole:
            return value->status();

        case Roles::RawTxParametersRole:
            {
                // auto txParams = value->getTxParameters();
                // return QVariant::fromValue(txParams);
            }
        default:
            return QVariant();
    }
}
