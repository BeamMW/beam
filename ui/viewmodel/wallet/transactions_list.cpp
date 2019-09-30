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
        { static_cast<int>(Roles::AmountGeneral), "amountGeneral" },
        { static_cast<int>(Roles::AmountGeneralSort), "amountGeneralSort" },
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
        { static_cast<int>(Roles::IsCompleted), "isCompleted" },
        { static_cast<int>(Roles::IsBeamSideSwap), "isBeamSideSwap" },
        { static_cast<int>(Roles::HasPaymentProof), "hasPaymentProof" },
        { static_cast<int>(Roles::SwapCoin), "swapCoin" },
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
            return value->timeCreated().toString(Qt::SystemLocaleShortDate);
        case Roles::TimeCreatedSort:
            return value->timeCreated();

        case Roles::AmountGeneral:
            return value->getAmount();
        case Roles::AmountGeneralSort:
            return value->getAmountValue();
            
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
            return value->getAddressFrom();

        case Roles::AddressTo:
        case Roles::AddressToSort:
            return value->getAddressTo();

        case Roles::Status:
        case Roles::StatusSort:
            return value->getStatus();

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

        case Roles::IsCompleted:
            return value->isCompleted();

        case Roles::IsBeamSideSwap:
            return value->isBeamSideSwap();

        case Roles::HasPaymentProof:
            return value->hasPaymentProof();

        case Roles::SwapCoin:
            return value->getSwapCoinName();

        case Roles::RawTxID:
            return QVariant::fromValue(value->getTxID());

        default:
            return QVariant();
    }
}

void TransactionsList::remove(const std::vector<std::shared_ptr<TxObject>>& items)
{
    for (const auto& item : items)
    {
        auto it = std::find_if(std::begin(m_list), std::end(m_list),
                            [&item](const auto& element) { return element->getTxID() == item->getTxID(); });
        
        if (it != std::end(m_list))
        {
            auto index = m_list.indexOf(*it);
            beginRemoveRows(QModelIndex(), index, index);
            m_list.removeAt(index);
            endRemoveRows();
        }
    }
}

void TransactionsList::update(const std::vector<std::shared_ptr<TxObject>>& items)
{
    for (const auto& item : items)
    {
        auto it = std::find_if(std::begin(m_list), std::end(m_list),
                            [&item](const auto& element) { return element->getTxID() == item->getTxID(); });
        
        // index to add item on last position by default
        int index = (m_list.count() == 0) ? 0 : m_list.count() - 1;

        if (it != std::end(m_list))
        {
            index = m_list.indexOf(*it);

            beginRemoveRows(QModelIndex(), index, index);
            m_list.removeAt(index);
            endRemoveRows();
        }

        beginInsertRows(QModelIndex(), index, index);
        m_list.insert(index, item);
        endInsertRows();
    }
}
