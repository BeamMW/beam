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

struct WalkerContracts
{

#pragma pack (push, 1)
	struct SidCid {
		ShaderID m_Sid;
		ContractID m_Cid;
	};
#pragma pack (pop)

	typedef Env::Key_T<SidCid> KeySidCid;

	const SidCid* m_pPos;
	Height m_Height;

	void Enum(const ShaderID* pSid)
	{
		KeySidCid k0, k1;
		_POD_(k0).SetZero();
		k0.m_Prefix.m_Tag = KeyTag::SidCid;

		if (pSid)
			_POD_(k0.m_KeyInContract.m_Sid) = *pSid;

		_POD_(k1) = k0;
		_POD_(k1.m_KeyInContract.m_Cid).SetObject(0xff);
		if (!pSid)
			_POD_(k1.m_KeyInContract.m_Sid).SetObject(0xff);

		Env::VarsEnum_T(k0, k1);
	}

	void Enum()
	{
		Enum(nullptr);
	}

	void Enum(const ShaderID& sid)
	{
		Enum(&sid);
	}

	bool MoveNext()
	{
		const KeySidCid* pKey;
		const Height* pHeight_be;

		if (!Env::VarsMoveNext_T(pKey, pHeight_be))
			return false;

		m_pPos = &pKey->m_KeyInContract;
		m_Height = Utils::FromBE(*pHeight_be);
		return true;
	}
};

inline void EnumAndDumpContracts(const ShaderID& sid)
{
	Env::DocArray gr("contracts");

	WalkerContracts wlk;
	for (wlk.Enum(sid); wlk.MoveNext(); )
	{
		Env::DocGroup root("");

		Env::DocAddBlob_T("cid", wlk.m_pPos->m_Cid);
		Env::DocAddNum("Height", wlk.m_Height);
	}
}

struct WalkerFunds
{
#pragma pack (push, 1)

	typedef Env::Key_T<AssetID> KeyFunds; // AssetID is in big-endian format

	struct ValueFunds
	{
		Amount m_Hi;
		Amount m_Lo;
	};

#pragma pack (pop)

	AssetID m_Aid;
	ValueFunds m_Val;

	void Enum(const ContractID& cid, const AssetID* pAid = nullptr)
	{
		KeyFunds k0;
		k0.m_Prefix.m_Cid = cid;
		k0.m_Prefix.m_Tag = KeyTag::LockedAmount;

		if (pAid)
		{
			k0.m_KeyInContract = Utils::FromBE(*pAid);
			Env::VarsEnum_T(k0, k0);
		}
		else
		{
			KeyFunds k1;
			_POD_(k1.m_Prefix) = k0.m_Prefix;

			k0.m_KeyInContract = 0;
			k1.m_KeyInContract = static_cast<AssetID>(-1);

			Env::VarsEnum_T(k0, k1);
		}
	}

	bool MoveNext()
	{
		const KeyFunds* pKey;
		const ValueFunds* pVal;

		if (!Env::VarsMoveNext_T(pKey, pVal))
			return false;

		m_Aid = Utils::FromBE(pKey->m_KeyInContract);
		m_Val.m_Lo = Utils::FromBE(pVal->m_Lo);
		m_Val.m_Hi = Utils::FromBE(pVal->m_Hi);

		return true;
	}
};
