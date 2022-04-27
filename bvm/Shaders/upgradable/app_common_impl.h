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

struct WalkerUpgradable
{
	struct VerInfo
	{
		uint32_t m_Count;
		const ShaderID* s_pSid;

		// output
		ContractID* m_pCid;
		Height* m_pHeight;

		void Init()
		{
			Env::Key_T<WalkerContracts::SidCid> key;
			_POD_(key.m_Prefix.m_Cid).SetZero();
			key.m_Prefix.m_Tag = KeyTag::SidCid;

			for (uint32_t i = 0; i < m_Count; i++)
			{
				auto& cid = m_pCid[i];
				const auto& sid = s_pSid[i];

				Utils::get_Cid(cid, sid, nullptr, 0);

				_POD_(key.m_KeyInContract.m_Sid) = sid;
				_POD_(key.m_KeyInContract.m_Cid) = cid;

				Height h_be;
				m_pHeight[i] = Env::VarReader::Read_T(key, h_be) ? Utils::FromBE(h_be) : 0;
			}
		}

		void Dump(bool bOnlyDeployed = true) const
		{
			Env::DocArray gr0("versions");

			for (uint32_t i = 0; i < m_Count; i++)
			{
				Height h = m_pHeight[i];
				if (bOnlyDeployed && !h)
					continue;

				Env::DocGroup gr1("");
				Env::DocAddNum("Number", i);
				Env::DocAddNum("Height", h);
				Env::DocAddBlob_T("cid", m_pCid[i]);
			}
		}

		uint32_t Find(const ContractID& cid) const
		{
			for (uint32_t i = 0; i < m_Count; i++)
				if (_POD_(cid) == m_pCid[i])
					return i;

			return static_cast<uint32_t>(-1);
		}

	} m_VerInfo;

	WalkerContracts m_Wlk;

	Upgradable::State m_State;
	uint32_t m_VerCurrent;
	uint32_t m_VerNext;

	void Enum()
	{
		m_Wlk.Enum(Upgradable::s_SID);
	}

	bool MoveNext()
	{
		while (m_Wlk.MoveNext())
		{
			const auto& cid = m_Wlk.m_Key.m_KeyInContract.m_Cid; // alias

			Env::Key_T<uint8_t> key;
			key.m_Prefix.m_Cid = cid;
			key.m_KeyInContract = Upgradable::State::s_Key;

			if (!Env::VarReader::Read_T(key, m_State))
				continue;

			m_VerCurrent = m_VerInfo.Find(m_State.m_Cid) + 1;
			m_VerNext = m_VerInfo.Find(m_State.m_cidNext) + 1;

			if (m_VerCurrent || m_VerNext)
				return true;
		}
		return false;

	}

	void DumpCurrent()
	{
		Env::DocAddBlob_T("cid", m_Wlk.m_Key.m_KeyInContract.m_Cid);
		Env::DocAddNum("Height", m_Wlk.m_Height);

		//Env::DocAddNum("owner", (uint32_t)((_POD_(m_State.m_Pk) == pk) ? 1 : 0));
		Env::DocAddNum("min_upgrade_delay", m_State.m_hMinUpgadeDelay);

		if (m_VerCurrent)
			Env::DocAddNum("current_version", m_VerCurrent - 1);

		if (m_State.m_hNextActivate != static_cast<Height>(-1))
		{
			if (m_VerNext)
				Env::DocAddNum("next_version", m_VerNext - 1);
			Env::DocAddNum("next_height", m_State.m_hNextActivate);
		}
	}
};
