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

#include "uintBig.h"

namespace beam {


	char ChFromHex(uint8_t v)
	{
		return v + ((v < 10) ? '0' : ('a' - 10));
	}

	std::ostream& operator << (std::ostream& s, const uintBig_t<256>& x)
	{
		const int nDigits = 8; // truncated
		//static_assert(nDigits <= x.nBytes);

		char sz[nDigits * 2 + 1];

		for (int i = 0; i < nDigits; i++)
		{
			sz[i * 2] = ChFromHex(x.m_pData[i] >> 4);
			sz[i * 2 + 1] = ChFromHex(x.m_pData[i] & 0xf);
		}

		sz[_countof(sz) - 1] = 0;
		s << sz;

		return s;
	}


} // namespace beam
