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
		Utils::ZeroObject(k0);
		k0.m_Prefix.m_Tag = KeyTag::SidCid;

		if (pSid)
			Utils::Copy(k0.m_KeyInContract.m_Sid, *pSid);

		Utils::Copy(k1, k0);
		Utils::SetObject(k1.m_KeyInContract.m_Cid, 0xff);
		if (!pSid)
			Utils::SetObject(k1.m_KeyInContract.m_Sid, 0xff);

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

