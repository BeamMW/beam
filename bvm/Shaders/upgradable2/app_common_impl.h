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
#include "../app_comm.h"

struct ManagerUpgadable2
{
	static uint32_t FindAdmin(const Upgradable2::Settings& stg, const PubKey& pk)
	{
		for (uint32_t i = 0; i < stg.s_AdminsMax; i++)
			if (_POD_(stg.m_pAdmin[i]) == pk)
				return i + 1;

		return 0;
	}

	static bool ReadSettings(Upgradable2::Settings& stg, const ContractID& cid)
	{
		Env::Key_T<uint16_t> key;
		key.m_Prefix.m_Cid = cid;
		key.m_KeyInContract = Upgradable2::Settings::s_Key;
		return Env::VarReader::Read_T(key, stg);
	}

	static void OnError(const char* sz)
	{
		Env::DocAddText("error", sz);
	}

	struct Walker
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

					HashProcessor::Sha256()
						<< "bvm.cid"
						<< sid
						<< static_cast<uint32_t>(0)
						>> cid;

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

		Upgradable2::State m_State;
		uint32_t m_VerCurrent;
		uint32_t m_VerNext;

		void Enum()
		{
			m_Wlk.Enum(Upgradable2::s_SID);
		}

		bool MoveNext()
		{
			while (m_Wlk.MoveNext())
			{
				const auto& cid = m_Wlk.m_Key.m_KeyInContract.m_Cid; // alias

				Env::Key_T<uint16_t> key;
				key.m_Prefix.m_Cid = cid;
				key.m_KeyInContract = Upgradable2::State::s_Key;

				if (!Env::VarReader::Read_T(key, m_State))
					continue;

				m_VerCurrent = m_VerInfo.Find(m_State.m_Active.m_Cid) + 1;
				m_VerNext = m_VerInfo.Find(m_State.m_Next.m_Cid) + 1;

				if (m_VerCurrent || m_VerNext)
					return true;
			}
			return false;

		}

		void DumpCurrent(const PubKey& pkMy)
		{
			const auto& cid = m_Wlk.m_Key.m_KeyInContract.m_Cid;

			Env::DocAddBlob_T("cid", cid);
			Env::DocAddNum("Height", m_Wlk.m_Height);

			{
				Upgradable2::Settings stg;
				if (ReadSettings(stg, cid))
				{
					Env::DocAddNum("min_upgrade_delay", stg.m_hMinUpgadeDelay);
					Env::DocAddNum("min_approvers", stg.m_MinApprovers);

					{
						Env::DocArray gr0("admins");

						uint32_t iAdminMy = 0;
						for (uint32_t i = 0; i < stg.s_AdminsMax; i++)
						{
							const auto& pk = stg.m_pAdmin[i];
							if (!_POD_(pk).IsZero())
							{
								Env::DocGroup gr1("");
								Env::DocAddNum("id", i);
								Env::DocAddBlob_T("pk", pk);
							}
						}
					}

					uint32_t iAdmin = FindAdmin(stg, pkMy);
					if (iAdmin)
						Env::DocAddNum("iAdmin", iAdmin - 1);
				}
			}

			if (m_VerCurrent)
				Env::DocAddNum("current_version", m_VerCurrent - 1);

			if (m_State.m_Next.m_hTarget != static_cast<Height>(-1))
			{
				if (m_VerNext)
					Env::DocAddNum("next_version", m_VerNext - 1);
				Env::DocAddNum("next_height", m_State.m_Next.m_hTarget);
			}
		}
	};

};
