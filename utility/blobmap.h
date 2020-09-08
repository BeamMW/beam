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
#include <boost/intrusive/set.hpp>

namespace beam {
namespace BlobMap {

	struct Entry
		:public boost::intrusive::set_base_hook<>
	{
		ByteBuffer m_Data;

		uint32_t m_Size;

#ifdef _MSC_VER
#	pragma warning (disable: 4200) // 0-sized array
#endif // _MSC_VER
		uint8_t m_pBuf[0]; // var size
#ifdef _MSC_VER
#	pragma warning (default: 4200)
#endif // _MSC_VER

		Blob ToBlob() const
		{
			Blob x;
			x.p = m_pBuf;
			x.n = m_Size;
			return x;
		}

		bool operator < (const Entry& x) const {
			return ToBlob() < x.ToBlob();
		}

		void* operator new(size_t n, uint32_t nExtra) {
			return new uint8_t[n + nExtra];
		}

		void operator delete(void* p) {
			delete[] reinterpret_cast<uint8_t*>(p);
		}

		void operator delete(void* p, uint32_t) {
			delete[] reinterpret_cast<uint8_t*>(p);
		}
	};

	struct Set
		:public boost::intrusive::multiset<Entry>
	{
		// The following allows to use Blob as a key
		struct Comparator
		{
			bool operator()(const Blob& a, const Entry& b) const { return a < b.ToBlob(); }
			bool operator()(const Entry& a, const Blob& b) const { return a.ToBlob() < b; }
		};

		~Set();
		void Clear();

		Entry* Find(const Blob&);
		Entry* Create(const Blob&);
		void Delete(Entry&);
	};

} // namespace BlobMap
} // namespace beam

