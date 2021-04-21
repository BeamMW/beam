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

    void ContractInvokeEntry::Generate(Transaction& tx, Key::IKdf& kdf, const HeightRange& hr, Amount fee) const
	{
		std::unique_ptr<TxKernelContractControl> pKrn;

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
		pKrn->m_Fee = fee;
		pKrn->m_Height = hr;
		pKrn->m_Commitment = Zero;
		pKrn->UpdateMsg();

		// seed everything to derive the blinding factor, add external random as well
		ECC::Hash::Value hv;
		ECC::GenRandom(hv);
		ECC::Hash::Processor hp;
		hp
			<< hv
			<< pKrn->m_Msg
			<< m_Spend.size()
			<< m_vSig.size();

		std::vector<ECC::Scalar::Native> vSk;
		vSk.resize(m_vSig.size() + 1);

		for (uint32_t i = 0; i < m_vSig.size(); i++)
		{
			const auto& hvSig = m_vSig[i];
			kdf.DeriveKey(vSk[i], hvSig);
			hp << hvSig;
		}

		FundsChangeMap fcm;
		for (auto it = m_Spend.begin(); m_Spend.end() != it; it++)
		{
			const auto& aid = it->first;
			const auto& val = it->second;
			bool bSpend = (val >= 0);

			fcm.Process(bSpend ? val : -val, aid, !bSpend);
			hp
				<< aid
				<< static_cast<Amount>(val);
		}

		ECC::Point::Native ptFunds;
		fcm.ToCommitment(ptFunds);

		ECC::Scalar::Native& skKrn = vSk.back();

		hp >> hv;
		kdf.DeriveKey(skKrn, hv);

		pKrn->Sign(&vSk.front(), static_cast<uint32_t>(vSk.size()), ptFunds);

		tx.m_vKernels.push_back(std::move(pKrn));

		ECC::Scalar::Native kOffs(tx.m_Offset);
		kOffs += -skKrn;
		tx.m_Offset = kOffs;
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
