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
}
