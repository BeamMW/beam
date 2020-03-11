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

#pragma once

#include "notification_item.h"
#include "viewmodel/helpers/list_model.h"

class NotificationsList : public ListModel<std::shared_ptr<NotificationItem>>
{
    Q_OBJECT

public:
    enum class Roles
    {
        TimeCreated = Qt::UserRole + 1,
        TimeCreatedSort,
        Title,
        Message,
        Type,
        State,
        RawID,
        DateCreated
    };

    NotificationsList();

    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
};
