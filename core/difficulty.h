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
#include "uintBig.h"
#include "ecc.h"

namespace beam
{
	struct Difficulty
	{
		uint32_t m_Packed;
		static const uint32_t s_MantissaBits = 24;

		Difficulty(uint32_t d = 0) :m_Packed(d) {}

		typedef ECC::uintBig Raw;

		// maximum theoretical difficulty value, which corresponds to 'infinite' (only Zero hash value meet the target).
		// Corresponds to 0xffff...fff raw value.
		static const uint32_t s_MaxOrder = Raw::nBits - s_MantissaBits - 1;
		static const uint32_t s_Inf = (s_MaxOrder + 1) << s_MantissaBits;

		bool IsTargetReached(const ECC::uintBig&) const;

		void Unpack(Raw&) const;

		void Unpack(uint32_t& order, uint32_t& mantissa) const;
		void Pack(uint32_t order, uint32_t mantissa);

		void Calculate(const Raw& wrk, uint32_t dh, uint32_t dtTrg_s, uint32_t dtSrc_s);

		friend Raw operator + (const Raw&, const Difficulty&);
		friend Raw operator - (const Raw&, const Difficulty&);
		friend Raw& operator += (Raw&, const Difficulty&);
		friend Raw& operator -= (Raw&, const Difficulty&);

		double ToFloat() const;
		static double ToFloat(Raw&);

		struct BigFloat;

	};

	std::ostream& operator << (std::ostream&, const Difficulty&);
}
