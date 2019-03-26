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

namespace std
{
	// Simple implementation of "double-vector". In addition to standard vector operation allows effective push/pop at head (like deque).
	template <typename T>
	class dvector
	{
		std::vector<T> m_Vec;
		size_t m_i0;

		void MaybeRearrange()
		{
			if ((m_Vec.size() > 10) && (m_Vec.size() > size() * 4))
				Rearrange();
		}

		void Rearrange()
		{
			size_t nSize = size();

			std::vector<T> v2;
			v2.reserve(std::max(nSize * 2, size_t(10)));

			size_t i0 = std::max(nSize / 2, size_t(3));
			v2.resize(i0 + nSize);

			std::copy(m_Vec.begin() + m_i0, m_Vec.end(), v2.begin() + i0);

			m_Vec.swap(v2);
			m_i0 = i0;
		}

	public:
		dvector() :m_i0(0) {}

		size_t size() const { return m_Vec.size() - m_i0; }

		void push_back(const T& x) {
			if (m_Vec.empty())
				Rearrange();
			m_Vec.push_back(x);
		}

		void pop_back() {
			m_Vec.pop_back();
			MaybeRearrange();
		}

		void push_front(const T& x) {
			if (!m_i0)
			{
				Rearrange();
				assert(m_i0);
			}

			m_Vec[--m_i0] = x;
		}

		void pop_front() {
			assert(size());
			m_i0++;
			MaybeRearrange();
		}

		T& at(size_t i)
		{
			assert(i < size());
			return m_Vec.at(m_i0 + i);
		}
	};
}
