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

#include "utxo_item_list.h"

UtxoItemList::UtxoItemList()
{
}

QHash<int, QByteArray> UtxoItemList::roleNames() const
{
    static const auto roles = QHash<int, QByteArray>
    {
        { static_cast<int>(Roles::Amount), "amount" },
        { static_cast<int>(Roles::AmountSort), "amountSort" },
        { static_cast<int>(Roles::Maturity), "maturity" },
        { static_cast<int>(Roles::MaturitySort), "maturitySort" },
        { static_cast<int>(Roles::Status), "status" },
        { static_cast<int>(Roles::StatusSort), "statusSort" },
        { static_cast<int>(Roles::Type), "type" },
        { static_cast<int>(Roles::TypeSort), "typeSort" }
    };
    return roles;
}

auto UtxoItemList::data(const QModelIndex &index, int role) const -> QVariant
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_list.size())
    {
       return QVariant();
    }
    
    auto& value = m_list[index.row()];
    switch (static_cast<Roles>(role))
    {
        case Roles::Amount:
            return value->getAmountWithCurrency();
            
        case Roles::AmountSort:
            return static_cast<qulonglong>(value->rawAmount());

        case Roles::Maturity:
            return value->maturity();
        case Roles::MaturitySort:
            return value->rawMaturity();

        case Roles::Status:
        case Roles::StatusSort:
            return value->status();

        case Roles::Type:
        case Roles::TypeSort:
            return value->type();

        default:
            return QVariant();
    }
}

