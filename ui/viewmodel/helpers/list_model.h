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

#pragma once

#include <QAbstractListModel>

template <typename T>
class ListModel : public QAbstractListModel
{

public:
    ListModel(QObject* pObj = nullptr)
        : QAbstractListModel(pObj)
    {
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const
    {
        if (parent.isValid())
        {
            return 0;
        }
        return m_list.size();
    }

    void insert(const std::vector<T>& items)
    {
        if (items.size() == 0)
        {
            return;
        }
        int row = 0;
        beginInsertRows(QModelIndex(), row, row + int(items.size()) - 1);
        for (const auto& item : items)
        {
            m_list.insert(row, item);
        }
        endInsertRows();
    }

    void reset(const std::vector<T>& items)
    {
        int row = 0;
        beginResetModel();
        m_list.clear();
        for (const auto& item : items)
        {
            m_list.insert(row, item);
        }
        endResetModel();
    }

    T get(int index) const
    {
        return m_list.at(index);
    }

protected:
    QList<T> m_list;
};
