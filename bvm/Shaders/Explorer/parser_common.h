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

#include "../common.h"
#include "../Float.h"
#include "../upgradable2/contract.h"
#include "../upgradable3/contract.h"

template <uint32_t nMaxLen>
inline void DocAddTextLen(const char* szID, const void* szValue, uint32_t nLen)
{
	char szBuf[nMaxLen + 1];
	nLen = std::min(nLen, nMaxLen);

	Env::Memcpy(szBuf, szValue, nLen);
	szBuf[nLen] = 0;

	Env::DocAddText(szID, szBuf);
}

inline void DocSetType(const char* sz)
{
	Env::DocAddText("type", sz);
}

inline void DocAddTableHeader(const char* sz)
{
	Env::DocGroup gr("");
	DocSetType("th");
	Env::DocAddText("value", sz);
}

inline void DocAddAmount(const char* sz, Amount x)
{
	Env::DocGroup gr(sz);
	DocSetType("amount");
	Env::DocAddNum("value", x);
}

inline void DocAddAmountSigned(const char* sz, Amount val, bool bPos)
{
	Env::DocGroup gr(sz);
	DocSetType("amount");

	char szBuf[Utils::String::Decimal::DigitsMax<Amount>::N + 2];
	szBuf[0] = bPos ? '+' : '-';
	Utils::String::Decimal::Print(szBuf + 1, val);

	Env::DocAddText("value", szBuf);
}

inline void DocAddAmountSigned(const char* sz, int64_t x)
{
	if (x > 0)
		DocAddAmountSigned(sz, x, true);
	else
		if (x < 0)
			DocAddAmountSigned(sz, -x, false);
		else
			DocAddAmount(sz, 0);
}

inline void DocAddHeight(const char* sz, Height h)
{
	Env::DocGroup gr(sz);
	DocSetType("height");
	Env::DocAddNum("value", h);
}

inline void DocAddHeight1(const char* sz, Height h)
{
	DocAddHeight(sz, h + 1);
}

template <typename T>
inline void DocAddMonoblob(const char* sz, const T& x)
{
	Env::DocGroup gr(sz);
	DocSetType("blob");
	Env::DocAddBlob_T("value", x);
}

inline void DocAddPk(const char* sz, const PubKey& pk)
{
	DocAddMonoblob(sz, pk);
}

inline void DocAddAmountBig(const char* sz, Amount valLo, Amount valHi)
{
	if (valHi)
	{
		MultiPrecision::UInt<4> val;
		val.Set<2>(valHi);
		val += MultiPrecision::UInt<2>(valLo);

		MultiPrecision::UInt<1> div1(1000000000ul);

		char szBuf[64]; // little extra
		char* szPos = szBuf + _countof(szBuf) - 1;
		szPos[0] = 0;

		while (true)
		{
			MultiPrecision::UInt<4> quot;
			quot.SetDivResid(val, div1);

			szPos -= 9;
			Utils::String::Decimal::PrintNoZTerm(szPos, val.get_Val<1>(), 9);

			if (quot.IsZero())
				break;
			val = quot;
		}

		while ('0' == *szPos)
			szPos++;

		Env::DocGroup gr(sz);
		DocSetType("amount");
		Env::DocAddText("value", szPos);

	}
	else
		DocAddAmount(sz, valLo);

}

inline void DocAddAid(const char* sz, AssetID aid)
{
	Env::DocGroup gr(sz);
	DocSetType("aid");
	Env::DocAddNum("value", aid);
}

inline void DocAddAidAmount(const char* sz, AssetID aid, Amount amount)
{
	Env::DocArray gr(sz);
	DocAddAid("", aid);
	DocAddAmount("", amount);
}

inline void DocAddCid(const char* sz, const ContractID& cid)
{
	Env::DocGroup gr(sz);
	DocSetType("cid");
	Env::DocAddBlob_T("value", cid);
}

inline void DocAddFloat(const char* sz, MultiPrecision::Float x)
{
	char szBuf[MultiPrecision::Float::DecimalForm::s_LenScientificMax + 1];
	x.get_Decimal().PrintAuto(szBuf);
	Env::DocAddText(sz, szBuf);
}

inline void DocAddFloatDbg(const char* sz, MultiPrecision::Float x)
{
	// convenient for debugging, to try the exact values on host
	char szBuf[Utils::String::Hex::DigitsMax<uint64_t>::N + Utils::String::Decimal::DigitsMax<uint32_t>::N + 10];
	Utils::String::Hex::Print(szBuf, x.m_Num, Utils::String::Hex::DigitsMax<uint64_t>::N);
	uint32_t n = Utils::String::Hex::DigitsMax<uint64_t>::N;
	szBuf[n++] = ' ';

	if (x.m_Order >= 0)
		szBuf[n++] = '+';
	else
	{
		szBuf[n++] = '-';
		x.m_Order = -x.m_Order;
	}

	n += Utils::String::Decimal::Print(szBuf + n, x.m_Order);
	szBuf[n] = 0;

	Env::DocAddText(sz, szBuf);
}

inline void DocAddPerc(const char* sz, MultiPrecision::Float x, uint32_t nDigsAfterDot = 3)
{
	MultiPrecision::Float::DecimalForm df;
	df.Assign(x);
	df.m_Order10 += 2; // to perc

	MultiPrecision::Float::DecimalForm::PrintOptions po;
	po.m_DigitsAfterDot = nDigsAfterDot;

	// remove unnecessary extra precision
	auto df2 = df;
	int32_t nExtra = -(po.m_DigitsAfterDot + df2.m_Order10);
	if (nExtra > 0)
	{
		// loose extra precision
		if (df2.m_NumDigits > (uint32_t)nExtra)
			df2.LimitPrecision(df2.m_NumDigits - nExtra);
		else
		{
			// loose all, make it 0
			df2.m_Num = 0;
			df2.m_Order10 = 0;
			df2.m_NumDigits = 1;
		}
	}

	char szBuf[MultiPrecision::Float::DecimalForm::s_LenScientificMax + 1];
	if (df2.get_TextLenStd(po) < _countof(szBuf))
		df2.PrintStd(szBuf, po);
	else
	{
		df.LimitPrecision(nDigsAfterDot + 2);
		po.m_DigitsAfterDot = -1;
		df.PrintScientific(szBuf, po);
	}

	Env::DocAddText(sz, szBuf);
}

inline void DocAddFixedPoint(const char* sz, uint64_t val, uint64_t one, uint32_t nDigsAfterDot)
{
	char szVal[Utils::String::Decimal::DigitsMax<uint64_t>::N + 10];
	auto n1 = Utils::String::Decimal::Print(szVal, val / one);

	if (nDigsAfterDot)
	{
		szVal[n1++] = '.';

		while (true)
		{
			val %= one;
			val *= 10;
			szVal[n1++] = Utils::String::Decimal::ToChar(val / one);

			if (!--nDigsAfterDot)
				break;
		}

		szVal[n1] = 0;
	}

	Env::DocAddText(sz, szVal);
}

// Upgradable-wrapper helpers. Free functions; do not depend on ParserContext.
// Per-contract parser modules and the explorer host both call these to render
// the upgradable plumbing (admins mask, settings table) consistently.

inline void WriteUpgradeAdminsMask(uint32_t nApproveMask)
{
	Env::DocArray gr("Approvers");

	for (uint32_t i = 0; i < (sizeof(nApproveMask) << 3); i++)
	{
		uint32_t msk = 1u << i;
		if (!(nApproveMask & msk))
			continue;

		Env::DocAddNum("", i);
	}
}

inline void WriteUpgradeSettingsInternal(const Upgradable3::Settings& stg)
{
	Env::DocAddNum("Delay", stg.m_hMinUpgradeDelay);
	Env::DocAddNum("Min approvers", stg.m_MinApprovers);

	{
		Env::DocGroup gr1("Admins");
		DocSetType("table");
		Env::DocArray gr2("value");

		{
			Env::DocArray gr3("");
			DocAddTableHeader("Index");
			DocAddTableHeader("Key");
		}

		for (uint32_t i = 0; i < _countof(stg.m_pAdmin); i++)
		{
			const auto& pk = stg.m_pAdmin[i];
			if (_POD_(pk).IsZero())
				continue;

			Env::DocArray gr3("");

			Env::DocAddNum("", i);
			DocAddPk("", pk);
		}

	}
}

inline void WriteUpgradeSettings(const Upgradable2::Settings& stg)
{
	WriteUpgradeSettingsInternal(Cast::Reinterpret<Upgradable3::Settings>(stg));
}

inline void WriteUpgradeSettings(const Upgradable3::Settings& stg)
{
	Env::DocGroup gr("Upgradable3");
	WriteUpgradeSettingsInternal(stg);
}
