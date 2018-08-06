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

#include "string_helpers.h"
#include <sstream>

namespace string_helpers
{
	template<typename Out>
	void split(const std::string& s, char delim, Out result)
	{
		std::stringstream ss(s);
		std::string item;
		while (std::getline(ss, item, delim))
			*(result++) = item;
	}

	std::vector<std::string> split(const std::string& s, char delim)
	{
		std::vector<std::string> elems;
		split(s, delim, std::back_inserter(elems));
		return elems;
	}
}
