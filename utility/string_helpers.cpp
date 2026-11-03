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

#include "string_helpers.h"
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <cassert>
#include <cstdint>
#include <cstring>
namespace string_helpers
{
	std::vector<std::string> split(const std::string& s, char delim, bool trimSpaces)
	{
		std::vector<std::string> result;
		boost::split(result, s, [&](char c){return c == delim;});
		if (trimSpaces) 
		{
			for (auto& p : result)
			{
				boost::trim(p);
			}
		}
		
		
		return result;
	}

	std::string trimCommas(const std::string& s)
	{
		std::string str = s;

		if (str.find(",") == std::string::npos) return str;

		str.erase(std::remove(str.begin(), str.end(), ','), str.end());
		return str;
	}

	// length of the well-formed UTF-8 sequence at p, 0 if there's none
	static size_t get_Utf8SeqLen(const char* p, size_t nLeft)
	{
		auto c = static_cast<uint8_t>(p[0]);
		if (c < 0x80)
			return 1;

		size_t len;
		uint32_t cp;
		if ((c & 0xE0) == 0xC0) { len = 2; cp = c & 0x1F; }
		else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0F; }
		else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07; }
		else
			return 0;

		if (len > nLeft)
			return 0;

		for (size_t k = 1; k < len; k++)
		{
			auto cNext = static_cast<uint8_t>(p[k]);
			if ((cNext & 0xC0) != 0x80)
				return 0;
			cp = (cp << 6) | (cNext & 0x3F);
		}

		// overlong encodings, surrogates and values above U+10FFFF are not valid UTF-8
		static const uint32_t kMin[] = { 0, 0, 0x80, 0x800, 0x10000 };
		if ((cp < kMin[len]) || (cp > 0x10FFFF) || ((cp >= 0xD800) && (cp <= 0xDFFF)))
			return 0;

		return len;
	}

	std::string replaceInvalidUtf8(std::string&& s)
	{
		size_t n = s.size();
		size_t nBad = 0;
		for (size_t i = 0; i < n; )
		{
			size_t len = get_Utf8SeqLen(s.data() + i, n - i);
			if (len)
				i += len;
			else
			{
				nBad++;
				i++;
			}
		}

		if (!nBad)
			return std::move(s); // usual case: no copy

		// each invalid byte becomes U+FFFD, 3 bytes. Move the original to the end of the grown buffer and rewrite it from the start.
		// The write position never passes the read position: the gap between them is 2 bytes per invalid byte not yet reached
		size_t nGap = nBad * 2;
		s.resize(n + nGap);
		char* p = &s.front();
		memmove(p + nGap, p, n);

		static const char kReplacement[] = "\xEF\xBF\xBD";

		size_t iDst = 0;
		for (size_t iSrc = nGap; iSrc < s.size(); )
		{
			size_t len = get_Utf8SeqLen(p + iSrc, s.size() - iSrc);
			if (len)
			{
				memmove(p + iDst, p + iSrc, len);
				iSrc += len;
				iDst += len;
			}
			else
			{
				memcpy(p + iDst, kReplacement, 3);
				iSrc++;
				iDst += 3;
			}
		}

		assert(iDst == s.size());
		return std::move(s);
	}
}
