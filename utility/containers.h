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
#include "common.h"
#include <boost/intrusive/list.hpp>
#include <boost/intrusive/set.hpp>

namespace beam {
namespace intrusive
{
	template <typename TEntry>
	struct multiset
		:public boost::intrusive::multiset<TEntry>
	{
		void Delete(TEntry& x)
		{
			erase(s_iterator_to(x));
			delete& x;
		}

		void Clear()
		{
			while (!empty())
				Delete(*begin());
		}

		template <typename TKey>
		TEntry* Create(TKey&& key)
		{
			TEntry* p = new TEntry;
			p->m_Key = std::move(key);
			insert(*p);
		}
	};

	template <typename TEntry>
	struct multiset_autoclear
		:public multiset<TEntry>
	{
		~multiset_autoclear() { Clear(); }
	};

	template <typename TEntry>
	struct list
		:public boost::intrusive::list<TEntry>
	{
		void Delete(TEntry& x)
		{
			erase(s_iterator_to(x));
			delete& x;
		}

		void Clear()
		{
			while (!empty())
				Delete(*begin());
		}

		TEntry* Create_front()
		{
			TEntry* p = new TEntry;
			push_front(*p);
			return p;
		}

		TEntry* Create_back()
		{
			TEntry* p = new TEntry;
			push_back(*p);
			return p;
		}
	};

	template <typename TEntry>
	struct list_autoclear
		:public list<TEntry>
	{
		~list_autoclear() { Clear(); }
	};

} // namespace intrusive
} // namespace beam

