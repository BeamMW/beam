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
#include "../app_common_impl.h"
#include "../app_comm.h"
#include "contract.h"

struct ManagerUpgadable2
{
	static void OnError(const char* sz)
	{
		Env::DocAddText("error", sz);
	}

	struct Settings
		:public Upgradable2::Settings
	{
		uint32_t FindAdmin(const PubKey& pk) const
		{
			for (uint32_t i = 0; i < s_AdminsMax; i++)
				if (_POD_(m_pAdmin[i]) == pk)
					return i + 1;

			return 0;
		}

		bool Read(const ContractID& cid)
		{
			Env::Key_T<uint16_t> key;
			key.m_Prefix.m_Cid = cid;
			key.m_KeyInContract = s_Key;
			return Env::VarReader::Read_T(key, *this);
		}

		bool TestValid() const
		{
			uint32_t nActiveAdmins = 0;

			for (uint32_t i = 0; i < s_AdminsMax; i++)
				if (!_POD_(m_pAdmin[i]).IsZero())
					nActiveAdmins++;

			uint32_t val = m_MinApprovers - 1; // would overflow if zero
			if (val >= nActiveAdmins)
			{
				OnError("invalid admins/approvers");
				return false;
			}

			return true;
		}
	};

	static bool ReadState(Upgradable2::State& s, const ContractID& cid)
	{
		Env::Key_T<uint16_t> key;
		key.m_Prefix.m_Cid = cid;
		key.m_KeyInContract = Upgradable2::State::s_Key;

		return Env::VarReader::Read_T(key, s);
	}

#define Upgradable2_deploy(macro) \
    macro(ContractID, cidVersion) \
    macro(Height, hUpgradeDelay) \
    macro(uint32_t, nMinApprovers)

#define Upgradable2_multisig_args(macro) \
    macro(uint32_t, iSender) \
    macro(uint32_t, approve_mask)

#define Upgradable2_schedule_upgrade(macro) \
    macro(ContractID, cid) \
    macro(ContractID, cidVersion) \
    macro(Height, hTarget) \
    Upgradable2_multisig_args(macro)

	static bool FillDeployArgs(Upgradable2::Create& arg, const PubKey* pKeyMy)
	{
		if (!Env::DocGet("cidVersion", arg.m_Active.m_Cid))
		{
			OnError("cidVersion not specified");
			return false;
		}

		Env::DocGet("hUpgradeDelay", arg.m_Settings.m_hMinUpgadeDelay);
		Env::DocGet("nMinApprovers", arg.m_Settings.m_MinApprovers);

#define ARG_NAME_PREFIX "admin-"
		char szBuf[_countof(ARG_NAME_PREFIX) + Utils::String::Decimal::Digits<Settings::s_AdminsMax>::N] = ARG_NAME_PREFIX;

		uint32_t iFree = arg.m_Settings.s_AdminsMax;

		for (uint32_t i = 0; i < Settings::s_AdminsMax; i++)
		{
			Utils::String::Decimal::Print(szBuf + _countof(ARG_NAME_PREFIX) - 1, i);
			auto& pk = arg.m_Settings.m_pAdmin[i];

			if (Env::DocGet(szBuf, pk)) // sets zero if not specified
			{
				if (pKeyMy && (_POD_(pk) == *pKeyMy))
					pKeyMy = nullptr; // included
			}
			else
				iFree = std::min(iFree, i);
		}
#undef ARG_NAME_PREFIX

		if (pKeyMy)
		{
			if (iFree >= arg.m_Settings.s_AdminsMax)
			{
				OnError("cannot include self key");
				return false;
			}

			_POD_(arg.m_Settings.m_pAdmin[iFree]) = *pKeyMy;
		}

		if (!Cast::Up<Settings>(arg.m_Settings).TestValid())
			return false;

		return true;
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

				if (!ReadState(m_State, cid))
					continue;

				m_VerCurrent = m_VerInfo.Find(m_State.m_Active.m_Cid) + 1;
				m_VerNext = m_VerInfo.Find(m_State.m_Next.m_Cid) + 1;

				if (m_VerCurrent || m_VerNext)
					return true;
			}
			return false;

		}

		void DumpCurrent(const PubKey* pMy)
		{
			const auto& cid = m_Wlk.m_Key.m_KeyInContract.m_Cid;

			Env::DocAddBlob_T("cid", cid);
			Env::DocAddNum("Height", m_Wlk.m_Height);

			{
				Settings stg;
				if (stg.Read(cid))
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

					if (pMy)
					{
						uint32_t iAdmin = stg.FindAdmin(*pMy);
						if (iAdmin)
							Env::DocAddNum("iAdmin", iAdmin - 1);
					}
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

		void ViewAll(const Env::KeyID* pMyKey)
		{
			m_VerInfo.Init();
			m_VerInfo.Dump();

			Env::DocArray gr("contracts");

			PubKey pkAdmin;
			if (pMyKey)
				pMyKey->get_Pk(pkAdmin);

			for (Enum(); MoveNext(); )
			{
				Env::DocGroup root("");
				DumpCurrent(pMyKey ? &pkAdmin : nullptr);
			}
		}
	};


	struct MultiSigRitual
	{
	#pragma pack (push, 1)
		// <-
		struct Msg1
		{
			PubKey m_pkMyNonce;
		};
		// ->
		struct Msg2
		{
			Height m_hMin;
			PubKey m_pkKrnBlind;
			PubKey m_pkFullNonce;
		};
		// <-
		struct Msg3
		{
			Secp_scalar_data m_kMySig;
		};
	#pragma pack (pop)

		Msg2 m_Msg2;

		static const Height s_dh = 20;

		static const uint32_t s_iSlotNonceKey = 0;
		static const uint32_t s_iSlotKrnNonce = 1;
		static const uint32_t s_iSlotKrnBlind = 2;

		Env::KeyID m_Kid;
		const ContractID* m_pCid = nullptr;
		uint32_t m_iMethod;
		Upgradable2::Control::Signed* m_pArg;
		uint32_t m_nArg;
		const char* m_szComment;
		PubKey m_pPks[Upgradable2::Settings::s_AdminsMax];
		Secp_scalar_data m_pE[Upgradable2::Settings::s_AdminsMax];
		uint32_t m_nPks;

		void InvokeKrn(const Secp_scalar_data& kSig, Secp_scalar_data* pE)
		{
			Env::GenerateKernelAdvanced(
				m_pCid, m_iMethod, m_pArg, m_nArg, nullptr, 0, m_pPks, m_nPks, m_szComment, 0,
				m_Msg2.m_hMin, m_Msg2.m_hMin + s_dh, m_Msg2.m_pkKrnBlind, m_Msg2.m_pkFullNonce, kSig, s_iSlotKrnBlind, s_iSlotKrnNonce, pE);
		}

		void GetChallenges()
		{
			InvokeKrn(*m_pE, m_pE);
		}

		void Finalize(const Secp_scalar_data& kSig)
		{
			InvokeKrn(kSig, nullptr);
		}

		static bool InMask(uint32_t msk, uint32_t i)
		{
			return !!(1 & (msk >> i));
		}

		void Perform(const Settings& stg)
		{
			uint32_t msk = 0, iSender = 0;
			Env::DocGet("approve_mask", msk);
			Env::DocGet("iSender", iSender);

			if (iSender >= stg.s_AdminsMax)
				return OnError("iSender oob");
			if (!InMask(msk, iSender))
				return OnError("iSender isn't in mask");

			PubKey pkMy;
			m_Kid.get_Pk(pkMy);

			m_nPks = 0;
			uint32_t iAdmin = 0, iAdminKeyIdx = 0;

			m_pArg->m_ApproveMask = msk;

			for (uint32_t i = 0; i < stg.s_AdminsMax; i++)
			{
				if (InMask(msk, i))
				{
					const PubKey& pk = stg.m_pAdmin[i];
					if (_POD_(pk).IsZero())
						return OnError("some approvers aren't valid");

					if (_POD_(pk) == pkMy)
					{
						iAdminKeyIdx = m_nPks;
						iAdmin = i + 1;
					}

					_POD_(m_pPks[m_nPks++]) = pk;
				}
			}

			if (m_nPks < stg.m_MinApprovers)
				return OnError("not enough approvers");


			if (!iAdmin)
				return OnError("not admin");
			iAdmin--;

			HashProcessor::Sha256 hpContext;
			hpContext << 1u; // proto version
			if (m_pCid)
				hpContext << *m_pCid;

			hpContext.Write(m_pArg, m_nArg);
			hpContext << m_iMethod << msk << iSender;

			Secp::Point p0;
			p0.FromSlot(s_iSlotNonceKey);

			Height h = Env::get_Height();

			if (iAdmin == iSender)
			{
				m_Msg2.m_hMin = h + 1;

				Comm::Channel pPeers[stg.s_AdminsMax];

				// setup channels
				for (uint32_t iPeer = 0; iPeer < stg.s_AdminsMax; iPeer++)
				{
					if (!InMask(msk, iPeer))
						continue;
					if (iPeer == iAdmin)
						continue;

					auto& cc = pPeers[iPeer];

					cc.Init(m_Kid, stg.m_pAdmin[iPeer], &hpContext);
					cc.m_Context << iPeer;
					cc.RcvStart();
				}

				Secp::Point p1;

				for (uint32_t iPeer = 0; iPeer < stg.s_AdminsMax; iPeer++)
				{
					if (!InMask(msk, iPeer))
						continue;
					if (iPeer == iAdmin)
						p1.FromSlot(s_iSlotKrnNonce);
					else
					{
						auto& cc = pPeers[iPeer];
						Env::WriteStr("waiting for co-signer nonce", Stream::Out);
						auto& msg1 = cc.Rcv_T<Msg1>(false);


						if (!p1.Import(msg1.m_pkMyNonce))
							return OnError("bad nonce");
					}
					p0 += p1;
				}

				p0.Export(m_Msg2.m_pkFullNonce);

				p0.FromSlot(s_iSlotKrnBlind);
				p0.Export(m_Msg2.m_pkKrnBlind);

				GetChallenges();

				for (uint32_t iPeer = 0; iPeer < stg.s_AdminsMax; iPeer++)
				{
					if (!InMask(msk, iPeer))
						continue;
					if (iPeer == iAdmin)
						continue;

					auto& cc = pPeers[iPeer];

					cc.Send_T(m_Msg2);
					cc.RcvStart();
				}

				Secp::Scalar kSig, e;

				for (uint32_t iPeer = 0; iPeer < stg.s_AdminsMax; iPeer++)
				{
					if (!InMask(msk, iPeer))
						continue;

					if (iPeer == iAdmin)
					{
						e.Import(m_pE[iAdminKeyIdx]);
						m_Kid.get_Blind(e, e, s_iSlotNonceKey);
					}
					else
					{
						auto& cc = pPeers[iPeer];
						Env::WriteStr("waiting for co-signer sig", Stream::Out);
						auto& msg3 = cc.Rcv_T<Msg3>(false);

						if (!e.Import(msg3.m_kMySig))
							return OnError("bad sig");
					}

					kSig += e;
				}

				Secp_scalar_data kSigData;
				kSig.Export(kSigData);

				Finalize(kSigData);
			}
			else
			{
				Comm::Channel cc(m_Kid, stg.m_pAdmin[iSender], &hpContext);
				cc.m_Context << iAdmin;

				Msg1 msg1;
				p0.Export(msg1.m_pkMyNonce);
				cc.Send_T(msg1);

				Env::WriteStr("waiting for sender", Stream::Out);
				cc.Rcv_T(m_Msg2);

				if ((m_Msg2.m_hMin + 10 < h) || (m_Msg2.m_hMin >= h + 20))
					OnError("height insane");

				GetChallenges();

				Secp::Scalar kSig;
				kSig.Import(m_pE[iAdminKeyIdx]);
				m_Kid.get_Blind(kSig, kSig, s_iSlotNonceKey);

				Msg3 msg3;
				kSig.Export(msg3.m_kMySig);
				cc.Send_T(msg3);

				Env::DocAddText("", "Negotiation is over");
			}
		}

		void Perform()
		{
			Settings stg;
			if (!stg.Read(*m_pCid))
				return OnError("no settings");

			Perform(stg);
		}

		static void Perform_ScheduleUpgrade(const ContractID& cid, const Env::KeyID& kid, const ContractID& cidVersion, Height hTarget)
		{
			Upgradable2::Control::ScheduleUpgrade arg;
			_POD_(arg.m_Next.m_Cid) = cidVersion;
			arg.m_Next.m_hTarget = hTarget;

			MultiSigRitual msp;
			msp.m_szComment = "Upgradable2 schedule upgrade";
			msp.m_iMethod = Upgradable2::Control::s_iMethod;
			msp.m_pArg = &arg;
			msp.m_nArg = sizeof(arg);
			msp.m_pCid = &cid;
			msp.m_Kid = kid;

			msp.Perform();
		}

		static void Perform_ReplaceAdmin(const ContractID& cid, const Env::KeyID& kid, uint32_t iAdmin, const PubKey& pk)
		{
			Upgradable2::Control::ReplaceAdmin arg;
			arg.m_iAdmin = iAdmin;
			_POD_(arg.m_Pk) = pk;

			MultiSigRitual msp;
			msp.m_szComment = "Upgradable2 replace admin";
			msp.m_iMethod = Upgradable2::Control::s_iMethod;
			msp.m_pArg = &arg;
			msp.m_nArg = sizeof(arg);
			msp.m_pCid = &cid;
			msp.m_Kid = kid;

			msp.Perform();
		}

		static void Perform_SetApprovers(const ContractID& cid, const Env::KeyID& kid, uint32_t newVal)
		{
			Upgradable2::Control::SetApprovers arg;
			arg.m_NewVal = newVal;

			MultiSigRitual msp;
			msp.m_szComment = "Upgradable2 change num approvers";
			msp.m_iMethod = Upgradable2::Control::s_iMethod;
			msp.m_pArg = &arg;
			msp.m_nArg = sizeof(arg);
			msp.m_pCid = &cid;
			msp.m_Kid = kid;

			msp.Perform();
		}

		static void Perform_ExplicitUpgrade(const ContractID& cid)
		{
			Upgradable2::State s;
			if (!ReadState(s, cid))
				return OnError("no state");

			if (s.m_Next.m_hTarget > Env::get_Height())
				return OnError("too early");


			Upgradable2::Control::ExplicitUpgrade arg;
			Env::GenerateKernel(&cid, Upgradable2::Control::s_iMethod, &arg, sizeof(arg), nullptr, 0, nullptr, 0, "Upgradable2 explicit upgrade", 0);
		}
	};

};
