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

#include "swap_offers_list.h"

QHash<int, QByteArray> SwapOffersList::roleNames() const
{
    static const auto roles = QHash<int, QByteArray>
    {
        { static_cast<int>(Roles::TimeRole), "time" },
        { static_cast<int>(Roles::TimeSortRole), "timeSort" },
        { static_cast<int>(Roles::IdRole), "id" },
        { static_cast<int>(Roles::IdSortRole), "idSort" },
        { static_cast<int>(Roles::AmountRole), "amount" },
        { static_cast<int>(Roles::AmountSortRole), "amountSort" },
        { static_cast<int>(Roles::StatusRole), "status" },
        { static_cast<int>(Roles::StatusSortRole), "statusSort" },
        { static_cast<int>(Roles::MessageRole), "message" },
        { static_cast<int>(Roles::MessageSortRole), "messageSort" }
    };
    return roles;
}

QVariant SwapOffersList::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_list.size())
    {
       return QVariant();
    }
    auto& value = m_list[index.row()];
    switch (static_cast<Roles>(role))
    {
    case Roles::TimeRole:
    case Roles::TimeSortRole:
        return value->time();
    case Roles::IdRole:
    case Roles::IdSortRole:
        return value->id();
	case Roles::AmountRole:
        return value->amount();
    case Roles::AmountSortRole:
        return static_cast<uint>(value->rawAmount());
    case Roles::StatusRole:
    case Roles::StatusSortRole:
        return value->status();
    case Roles::MessageRole:
    case Roles::MessageSortRole:
        return value->message();
    default:
        return QVariant();
    }
}
