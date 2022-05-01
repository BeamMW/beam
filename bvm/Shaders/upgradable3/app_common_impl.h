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

namespace Upgradable3 {

struct Manager
{
	static void OnError(const char* sz)
	{
		Env::DocAddText("error", sz);
	}

	struct SettingsPlus
		:public Settings
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
			Env::Key_T<Key> key;
			_POD_(key.m_Prefix.m_Cid) = cid;
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

#define Upgradable3_deploy(macro) \
    macro(Height, hUpgradeDelay) \
    macro(uint32_t, nMinApprovers) \
	macro(uint32_t, bSkipVerifyVer)

#define Upgradable3_multisig_args(macro) \
    macro(uint32_t, iSender) \
    macro(uint32_t, approve_mask)

#define Upgradable3_schedule_upgrade(macro) \
    macro(ContractID, cid) \
    macro(Height, hTarget) \
	macro(uint32_t, bSkipVerifyVer) \
	Upgradable3_multisig_args(macro)

#define Upgradable3_replace_admin(macro) \
    macro(ContractID, cid) \
    macro(uint32_t, iAdmin) \
    macro(PubKey, pk) \
    Upgradable3_multisig_args(macro)

#define Upgradable3_set_min_approvers(macro) \
    macro(ContractID, cid) \
    macro(uint32_t, newVal) \
    Upgradable3_multisig_args(macro)


	static uint32_t get_ChargeDeploy()
	{
		return
			Env::Cost::CallFar +
			Env::Cost::SaveVar_For(sizeof(Settings)) +
			Env::Cost::MemOpPerByte * (sizeof(Settings)) +
			Env::Cost::Cycle * 150;
	}



	struct VerInfoBase
	{
		const ShaderID* m_pSid;
		uint32_t m_Versions;

		uint32_t FindVer(const ShaderID& sid) const
		{
			uint32_t iVer = 0;
			for ( ; iVer < m_Versions; iVer++)
				if (_POD_(sid) == m_pSid[iVer])
					break;

			return iVer;
		}

		static bool ShouldVerifyVersion()
		{
			uint32_t bSkipVerifyVer = 0;
			Env::DocGet("bSkipVerifyVer", bSkipVerifyVer);
			return !bSkipVerifyVer;
		}

		bool VerifyVersion(const ShaderID& sid) const
		{
			auto iVer = FindVer(sid);
			if (iVer < m_Versions)
				return true;

			OnError("unrecognized version");
			return false;
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
		Method::Control::Signed* m_pArg;
		uint32_t m_nArg;
		const char* m_szComment;
		PubKey m_pPks[Settings::s_AdminsMax];
		Secp_scalar_data m_pE[Settings::s_AdminsMax];
		uint32_t m_nPks;

		uint32_t m_Charge =
			Env::Cost::CallFar +
			Env::Cost::LoadVar_For(sizeof(Settings)) +
			Env::Cost::Cycle * (300 + 20 * Settings::s_AdminsMax);

		void InvokeKrn(const Secp_scalar_data& kSig, Secp_scalar_data* pE)
		{
			Env::GenerateKernelAdvanced(
				m_pCid, m_iMethod, m_pArg, m_nArg, nullptr, 0, m_pPks, m_nPks, m_szComment, m_Charge,
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
					m_Charge += Env::Cost::AddSig;

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

						uint8_t ack = 1;
						cc.Send_T(ack); // send any ack to the peer

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
				Env::Comm_WaitMsg(10000);
			}
		}

		void Perform()
		{
			SettingsPlus stg;
			if (!stg.Read(*m_pCid))
				return OnError("no settings");

			Perform(stg);
		}

		static bool Perform_ScheduleUpgrade(const VerInfoBase& vi, const ContractID& cid, const Env::KeyID& kid, Height hTarget)
		{
			const char* szShaderVarName = "contract.shader";

			auto nShaderSize = Env::DocGetBlob(szShaderVarName, nullptr, 0);
			if (!nShaderSize)
			{
				OnError("next version shader not specified");
				return false;
			}

			uint32_t nSizeArgs = sizeof(Method::Control::ScheduleUpgrade) + nShaderSize;
			auto* pArgs = (Method::Control::ScheduleUpgrade*) Env::Heap_Alloc(nSizeArgs);
			auto& arg = *pArgs;
			arg.m_Type = arg.s_Type;

			Env::DocGetBlob(szShaderVarName, pArgs + 1, nShaderSize);

			if (VerInfo::ShouldVerifyVersion())
			{
				ShaderID sid;
				Utils::get_ShaderID(sid, pArgs + 1, nShaderSize);
				if (!vi.VerifyVersion(sid))
					return false;
			}


			arg.m_Next.m_hTarget = hTarget;
			arg.m_SizeShader = nShaderSize;

			MultiSigRitual msp;
			msp.m_szComment = "Upgradable3 schedule upgrade";
			msp.m_iMethod = Method::Control::s_iMethod;
			msp.m_pArg = &arg;
			msp.m_nArg = nSizeArgs;
			msp.m_pCid = &cid;
			msp.m_Kid = kid;

			msp.m_Charge +=
				Env::Cost::SaveVar_For(nShaderSize + sizeof(NextVersion));

			msp.Perform();

			Env::Heap_Free(pArgs);
			return true;
		}

		static void Perform_ReplaceAdmin(const ContractID& cid, const Env::KeyID& kid, uint32_t iAdmin, const PubKey& pk)
		{
			Method::Control::ReplaceAdmin arg;
			arg.m_iAdmin = iAdmin;
			_POD_(arg.m_Pk) = pk;

			MultiSigRitual msp;
			msp.m_szComment = "Upgradable3 replace admin";
			msp.m_iMethod = Method::Control::s_iMethod;
			msp.m_pArg = &arg;
			msp.m_nArg = sizeof(arg);
			msp.m_pCid = &cid;
			msp.m_Kid = kid;

			msp.m_Charge +=
				Env::Cost::SaveVar_For(sizeof(Settings));

			msp.Perform();
		}

		static void Perform_SetApprovers(const ContractID& cid, const Env::KeyID& kid, uint32_t newVal)
		{
			Method::Control::SetApprovers arg;
			arg.m_NewVal = newVal;

			MultiSigRitual msp;
			msp.m_szComment = "Upgradable3 change num approvers";
			msp.m_iMethod = Method::Control::s_iMethod;
			msp.m_pArg = &arg;
			msp.m_nArg = sizeof(arg);
			msp.m_pCid = &cid;
			msp.m_Kid = kid;

			msp.m_Charge +=
				Env::Cost::SaveVar_For(sizeof(Settings));

			msp.Perform();
		}

		static void Perform_ExplicitUpgrade(const ContractID& cid, uint32_t nChargeExtra = 0)
		{
			Env::Key_T<NextVersion::Key> nvk;
			_POD_(nvk.m_Prefix.m_Cid) = cid;

			NextVersion nv;
			Env::VarReader r(nvk, nvk);
			uint32_t nKey = sizeof(nvk), nVal = sizeof(nv);
			if (!r.MoveNext(&nvk, nKey, &nv, nVal, 0) || (nVal < sizeof(nv)))
				return OnError("no state");

			if (nv.m_hTarget > Env::get_Height())
				return OnError("too early");

			nChargeExtra +=
				Env::Cost::CallFar * 2 +
				Env::Cost::LoadVar +
				Env::Cost::LoadVar_For(nVal) +
				Env::Cost::HeapOp * 2 +
				Env::Cost::MemOpPerByte * nVal +
				Env::Cost::SaveVar +
				Env::Cost::UpdateShader_For(nVal - sizeof(nv)) +
				Env::Cost::Cycle * 1000; // other stuff

			Method::Control::ExplicitUpgrade arg;
			Env::GenerateKernel(&cid, Method::Control::s_iMethod, &arg, sizeof(arg), nullptr, 0, nullptr, 0, "Upgradable3 explicit upgrade", nChargeExtra);
		}
	};


	struct VerInfo :public VerInfoBase
	{
		void DocAddVer(const ShaderID& sid) const
		{
			uint32_t iVer = FindVer(sid);
			if (m_Versions == iVer)
				Env::DocAddBlob_T("version_sid", sid);
			else
				Env::DocAddNum("version", iVer);
		}

		void DumpAll(const Env::KeyID* pAdminKid) const
		{
			PubKey pkAdmin;
			if (pAdminKid)
				pAdminKid->get_Pk(pkAdmin);

			Env::DocArray gr0("contracts");

			for (uint32_t iVer = 0; iVer < m_Versions; iVer++)
			{
				WalkerContracts wlk;
				for (wlk.Enum(m_pSid[iVer]); wlk.MoveNext(); )
				{
					Env::DocGroup gr1("");

					const auto& cid = wlk.m_Key.m_KeyInContract.m_Cid;

					Env::DocAddBlob_T("cid", cid);
					Env::DocAddNum("Height", wlk.m_Height);
					Env::DocAddNum("version", iVer);

					{
						Env::DocArray gr2("version_history");

						Env::KeyPrefix kVer;
						_POD_(kVer.m_Cid) = cid;
						kVer.m_Tag = KeyTag::ShaderChange;
						ShaderID sid;

						for (Env::LogReader rVer(kVer, kVer); rVer.MoveNext_T(kVer, sid); )
						{
							Env::DocGroup gr3("");
							Env::DocAddNum("Height", rVer.m_Pos.m_Height);

							DocAddVer(sid);
						}
					}

					{
						SettingsPlus stg;
						if (stg.Read(cid))
						{
							Env::DocAddNum("min_upgrade_delay", stg.m_hMinUpgradeDelay);
							Env::DocAddNum("min_approvers", stg.m_MinApprovers);

							{
								Env::DocArray gr2("admins");

								for (uint32_t i = 0; i < stg.s_AdminsMax; i++)
								{
									const auto& pk = stg.m_pAdmin[i];
									if (!_POD_(pk).IsZero())
									{
										Env::DocGroup gr3("");
										Env::DocAddNum("id", i);
										Env::DocAddBlob_T("pk", pk);
									}
								}
							}

							if (pAdminKid)
							{
								uint32_t iAdmin = stg.FindAdmin(pkAdmin);
								if (iAdmin)
									Env::DocAddNum("iAdmin", iAdmin - 1);
							}
						}
					}

					{
						Env::Key_T<NextVersion::Key> k;
						_POD_(k.m_Prefix.m_Cid) = cid;

						Env::VarReader r(k, k);
						uint32_t nKey = sizeof(k), nVal = 0;
						if (r.MoveNext(&k, nKey, nullptr, nVal, 0) && (nVal >= sizeof(NextVersion)))
						{
							Env::DocGroup gr2("scheduled");

							auto* pVal = (NextVersion*) Env::Heap_Alloc(nVal);
							r.MoveNext(&k, nKey, pVal, nVal, 1);

							Env::DocAddNum("hTarget", pVal->m_hTarget);

							ShaderID sid;
							Utils::get_ShaderID(sid, pVal + 1, nVal - sizeof(NextVersion));

							Env::Heap_Free(pVal);

							DocAddVer(sid);
						}
					}
				}
			}
		}

		bool FillDeployArgs(Settings& arg, const PubKey* pKeyMy) const
		{
			if (ShouldVerifyVersion())
			{
				ShaderID sid;
				Utils::get_ShaderID_FromArg(sid);
				if (!VerifyVersion(sid))
					return false;
			}

			Env::DocGet("hUpgradeDelay", arg.m_hMinUpgradeDelay);
			Env::DocGet("nMinApprovers", arg.m_MinApprovers);

#define ARG_NAME_PREFIX "admin-"
			char szBuf[_countof(ARG_NAME_PREFIX) + Utils::String::Decimal::Digits<Settings::s_AdminsMax>::N] = ARG_NAME_PREFIX;

			uint32_t iFree = arg.s_AdminsMax;

			for (uint32_t i = 0; i < Settings::s_AdminsMax; i++)
			{
				Utils::String::Decimal::Print(szBuf + _countof(ARG_NAME_PREFIX) - 1, i);
				auto& pk = arg.m_pAdmin[i];

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
				if (iFree >= arg.s_AdminsMax)
				{
					OnError("cannot include self key");
					return false;
				}

				_POD_(arg.m_pAdmin[iFree]) = *pKeyMy;
			}

			if (!Cast::Up<SettingsPlus>(arg).TestValid())
				return false;

			return true;
		}

		bool ScheduleUpgrade(const ContractID& cid, const Env::KeyID& kid, Height hTarget) const
		{
			return Manager::MultiSigRitual::Perform_ScheduleUpgrade(*this, cid, kid, hTarget);
		}
	};

};

} // namespace Upgradable3