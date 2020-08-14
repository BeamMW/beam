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

#pragma once

namespace beam::wallet
{

    template<typename T, typename KeyFunc>
    class ChangesCollector
    {
        using FlushFunc = std::function<void(ChangeAction, const std::vector<T>&)>;

        struct Comparator
        {
            bool operator()(const T& left, const T& right) const
            {
                return KeyFunc()(left) < KeyFunc()(right);
            }
        };

        using ItemsList = std::set<T, Comparator>;
    public:
        ChangesCollector(size_t bufferSize, io::Reactor::Ptr reactor, FlushFunc&& flushFunc)
            : m_BufferSize(bufferSize)
            , m_FlushTimer(io::Timer::create(*reactor))
            , m_FlushFunc(std::move(flushFunc))
        {
        }

        void CollectItems(ChangeAction action, const std::vector<T>& items)
        {
            if (action == ChangeAction::Reset)
            {
                m_NewItems.clear();
                m_UpdatedItems.clear();
                m_RemovedItems.clear();
                m_FlushFunc(action, items);
                return;
            }

            for (const auto& item : items)
            {
                CollectItem(action, item);
            }
        }

        void Flush()
        {
            Flush(ChangeAction::Added, m_NewItems);
            Flush(ChangeAction::Updated, m_UpdatedItems);
            Flush(ChangeAction::Removed, m_RemovedItems);
        }

    private:

        void CollectItem(ChangeAction action, const T& item)
        {
            m_FlushTimer->cancel();
            m_FlushTimer->start(100, false, [this]() { Flush(); });
            switch (action)
            {
            case ChangeAction::Added:
                AddItem(item);
                break;
            case ChangeAction::Updated:
                UpdateItem(item);
                break;
            case ChangeAction::Removed:
                RemoveItem(item);
                break;
            default:
                break;
            }

            if (GetChangesCount() > m_BufferSize)
            {
                Flush();
            }
        }

        void Flush(ChangeAction action, ItemsList& items)
        {
            if (!items.empty())
            {
                m_FlushFunc(action, std::vector<T>(items.begin(), items.end()));
                items.clear();
            }
        }

        size_t GetChangesCount() const
        {
            return m_NewItems.size() + m_UpdatedItems.size() + m_RemovedItems.size();
        }

        void AddItem(const T& item)
        {
            // if it was removed do nothing
            if (m_RemovedItems.find(item) != m_RemovedItems.end())
            {
                return;
            }
            m_NewItems.insert(item);
        }

        void UpdateItem(const T& item)
        {
            // if it was removed do nothing
            if (m_RemovedItems.find(item) != m_RemovedItems.end())
            {
                return;
            }
            // if it is not in new add it to updated
            auto it = m_NewItems.find(item);
            if (it == m_NewItems.end())
            {
                auto p = m_UpdatedItems.insert(item);
                if (p.second == false)
                {
                    UpdateItemValue(item, p.first, m_UpdatedItems);
                }
            }
            else
            {
                UpdateItemValue(item, it, m_NewItems);
            }
        }

        void RemoveItem(const T& item)
        {
            // if it is in new erase it
            if (auto it = m_NewItems.find(item); it != m_NewItems.end())
            {
                m_NewItems.erase(it);
                return;
            }
            // else if it is in updated remove and add to removed
            if (auto it = m_UpdatedItems.find(item); it != m_UpdatedItems.end())
            {
                m_UpdatedItems.erase(it);
            }
            // add to removed
            m_RemovedItems.insert(item);
        }

        void UpdateItemValue(const T& item, typename ItemsList::iterator& it, ItemsList& items)
        {
            // MacOS doesn't support 'extract'
            //auto node = items.extract(it);
            //node.value() = item;
            //items.insert(std::move(node));
            items.erase(it);
            items.insert(item);
        }

    private:
        size_t m_BufferSize;
        io::Timer::Ptr m_FlushTimer;
        FlushFunc m_FlushFunc;

        ItemsList m_NewItems;
        ItemsList m_UpdatedItems;
        ItemsList m_RemovedItems;
    };

} // namespace beam::wallet
