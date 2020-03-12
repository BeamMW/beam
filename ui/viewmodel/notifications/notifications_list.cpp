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

#include "notifications_list.h"

NotificationsList::NotificationsList()
{
}

QHash<int, QByteArray> NotificationsList::roleNames() const
{
    static const auto roles = QHash<int, QByteArray>
    {
        { static_cast<int>(Roles::TimeCreated), "timeCreated" },
        { static_cast<int>(Roles::TimeCreatedSort), "timeCreatedSort" },
        { static_cast<int>(Roles::Title), "title" },
        { static_cast<int>(Roles::Message), "message" },
        { static_cast<int>(Roles::Type), "type" },
        { static_cast<int>(Roles::State), "state" },
        { static_cast<int>(Roles::RawID), "rawID" },
        { static_cast<int>(Roles::DateCreated), "dateCreated" },
        
    };
    return roles;
}

QVariant NotificationsList::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_list.size())
    {
       return QVariant();
    }

    auto& value = m_list[index.row()];
    switch (static_cast<Roles>(role))
    {
        case Roles::TimeCreated:
            return value->timeCreated().time().toString(Qt::SystemLocaleShortDate);
        case Roles::DateCreated:
            return value->timeCreated().date().toString(Qt::SystemLocaleShortDate);
        case Roles::TimeCreatedSort:
            return value->state() + value->timeCreated().toString(Qt::TextDate);

        case Roles::Title:
            return value->title();

        case Roles::Message:
            return value->message();

        case Roles::Type:
            return value->type();

        case Roles::State:
            return value->state();

        case Roles::RawID:
            return QVariant::fromValue(value->getID());

        default:
            return QVariant();
    }
}
