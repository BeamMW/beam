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
#include "invoke_data.h"
#include "bvm2.h"

namespace beam::bvm2 {

    void ContractInvokeEntry::Generate(std::unique_ptr<TxKernelContractControl>& pKrn, ECC::Scalar::Native& sk, Key::IKdf& kdf, const HeightRange& hr, Amount fee, ECC::Scalar::Native* pE) const
	{
		if (m_iMethod)
		{
			pKrn = std::make_unique<TxKernelContractInvoke>();
			auto& krn = Cast::Up<TxKernelContractInvoke>(*pKrn);
			krn.m_Cid = m_Cid;
			krn.m_iMethod = m_iMethod;
		}
		else
		{
			pKrn = std::make_unique<TxKernelContractCreate>();
			auto& krn = Cast::Up<TxKernelContractCreate>(*pKrn);
			krn.m_Data = m_Data;
		}

		pKrn->m_Args = m_Args;

		if (IsAdvanced())
		{
			// ignore args, use our params
			pKrn->m_Fee = m_Adv.m_Fee;
			pKrn->m_Height = m_Adv.m_Height;
		}
		else
		{
			pKrn->m_Fee = fee;
			pKrn->m_Height = hr;
		}

		FundsChangeMap fcm;
		for (auto it = m_Spend.begin(); m_Spend.end() != it; it++)
		{
			const auto& aid = it->first;
			const auto& val = it->second;
			bool bSpend = (val >= 0);

			fcm.Add(bSpend ? val : -val, aid, !bSpend);
		}

		ECC::Point::Native ptFunds;
		fcm.ToCommitment(ptFunds);


		if (IsAdvanced())
		{
			ECC::Scalar::Native skNonce, skSig;
			kdf.DeriveKey(skNonce, m_Adv.m_hvNonce);
			kdf.DeriveKey(sk, m_Adv.m_hvBlind);

			ECC::Point::Native pt1, pt2;
			pt1 = ECC::Context::get().G * skNonce;
			pt2.Import(m_Adv.m_ptExtraNonce); // don't care
			pt1 += pt2;

			pKrn->m_Signature.m_NoncePub = pt1;

			pt1 = ECC::Context::get().G * sk;
			pt1 += ptFunds;
			pKrn->m_Commitment = pt1;

			pKrn->UpdateMsg();

			ECC::Hash::Processor hp;
			hp << pKrn->m_Msg;

			for (uint32_t i = 0; i < m_Adv.m_vPks.size(); i++)
				hp << m_Adv.m_vPks[i];

			ECC::Hash::Value hv;
			hp
				<< pKrn->m_Commitment
				>> hv;

			ECC::Oracle oracle;
			pKrn->m_Signature.Expose(oracle, hv);

			for (uint32_t i = 0; i < m_Adv.m_vPks.size() + 1; i++)
			{
				oracle >> skSig;
				if (pE)
					pE[i] = skSig;
			}

			skSig *= sk;
			skSig += skNonce;

			skNonce = m_Adv.m_kExtraSig;
			skSig += skNonce;

			skSig = -skSig; // our formula is sig.ptNonce + Pk[i]*e[i] + G*sig.k == 0
			pKrn->m_Signature.m_k = skSig;

			pKrn->MsgToID();
		}
		else
		{
			std::vector<ECC::Scalar::Native> vSk;
			vSk.resize(m_vSig.size() + 1);

			for (uint32_t i = 0; i < m_vSig.size(); i++)
				kdf.DeriveKey(vSk[i], m_vSig[i]);

			ECC::Scalar::Native& skKrn = vSk.back();

			pKrn->m_Commitment = Zero;
			pKrn->UpdateMsg();

			ECC::Hash::Value hv;
			get_SigPreimage(hv, pKrn->m_Msg);

			kdf.DeriveKey(skKrn, hv);
			sk = skKrn;

			pKrn->Sign(&vSk.front(), static_cast<uint32_t>(vSk.size()), ptFunds);
		}

	}

	void ContractInvokeEntry::Generate(Transaction& tx, Key::IKdf& kdf, const HeightRange& hr, Amount fee) const
	{
		std::unique_ptr<TxKernelContractControl> pKrn;
		ECC::Scalar::Native sk;
		Generate(pKrn, sk, kdf, hr, fee, nullptr);

		tx.m_vKernels.push_back(std::move(pKrn));

		ECC::Scalar::Native kOffs(tx.m_Offset);
		kOffs += -sk;
		tx.m_Offset = kOffs;
	}

	void ContractInvokeEntry::get_SigPreimage(ECC::Hash::Value& hv, const ECC::Hash::Value& krnMsg) const
	{
		// seed everything to derive the blinding factor, add external random as well
		ECC::GenRandom(hv);
		ECC::Hash::Processor hp;
		hp
			<< hv
			<< krnMsg
			<< m_Spend.size()
			<< m_vSig.size();

		for (uint32_t i = 0; i < m_vSig.size(); i++)
			hp << m_vSig[i];

		for (auto it = m_Spend.begin(); m_Spend.end() != it; it++)
		{
			hp
				<< it->first
				<< static_cast<Amount>(it->second);
		}

		hp >> hv;
	}

	Amount ContractInvokeEntry::get_FeeMin(Height h) const
	{
		const auto& fs = Transaction::FeeSettings::get(h);

		Amount ret = std::max(fs.m_Bvm.m_ChargeUnitPrice * m_Charge, fs.m_Bvm.m_Minimum);

		auto nSizeExtra = static_cast<uint32_t>(m_Args.size() + m_Data.size());
		if (nSizeExtra > fs.m_Bvm.m_ExtraSizeFree)
			ret += fs.m_Bvm.m_ExtraBytePrice * (nSizeExtra - fs.m_Bvm.m_ExtraSizeFree);

		return ret;
	}

	void FundsMap::AddSpend(Asset::ID aid, AmountSigned val)
	{
		// don't care about overflow.
		// In case of the overflow the spendmap would be incorrect, and the wallet would fail to build a balanced tx.
		// No threat of illegal inflation or unauthorized funds spend.
		if (!val)
			return;

		auto it = find(aid);
		if (end() == it)
			(*this)[aid] = val;
		else
		{
			it->second += val;
			if (!it->second)
				erase(it);
		}
	}

	void FundsMap::operator += (const FundsMap& x)
	{
		for (auto it = x.begin(); x.end() != it; it++)
			AddSpend(it->first, it->second);
	}

	std::string getFullComment(const ContractInvokeData& data)
    {
        std::string comment;
        for (size_t i = 0; i < data.size(); i++)
        {
            if (i) comment += "; ";
            comment += data[i].m_sComment;
        }
        return comment;
    }

    beam::Amount getFullFee(const ContractInvokeData& data, Height h)
    {
        auto fee = Transaction::FeeSettings::get(h).get_DefaultStd();
        for (const auto& cdata: data)
        {
            fee += cdata.get_FeeMin(h);
        }
        return fee;
    }

    bvm2::FundsMap getFullSpend(const ContractInvokeData& data)
    {
        bvm2::FundsMap fm;
        for (const auto& cdata: data)
        {
            fm += cdata.m_Spend;
        }
        return fm;
    }
}
