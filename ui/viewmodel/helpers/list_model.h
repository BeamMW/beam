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

    //QVariant data(const QModelIndex &index, int role) const override
    //{
    //    if (!index.isValid() || index.row() < 0 || index.row() >= m_list.size())
    //    {
    //        return QVariant();
    //    }
    //    return (role == Qt::DisplayRole || role == Qt::EditRole) ? QVariant::fromValue<T>(m_list.at(index.row())) : QVariant();
    //}

    //bool setData(const QModelIndex &index, const QVariant &value, int role) override
    //{
    //    if (index.isValid() && role == Qt::EditRole)
    //    {
    //        m_list.replace(index.row(), value.value<T>());
    //        emit dataChanged(index, index);
    //        return true;
    //    }
    //    return false;
    //}

    void insert(const std::vector<T>& items)
    {
        int row = 0;
        beginInsertRows(QModelIndex(), row, row + int(items.size()) - 1);
        for (const auto& item : items)
        {
            m_list.insert(row, item);
        }
        endInsertRows();
    }

    void update(const std::vector<T>& items)
    {

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

protected:
    QList<T> m_list;
};
