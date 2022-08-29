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

	void ContractInvokeEntry::CreateKrnUnsigned(std::unique_ptr<TxKernelContractControl>& pKrn, ECC::Point::Native& ptFunds, const HeightRange& hr, Amount fee) const
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

		auto& krn = *pKrn;
		krn.m_Args = m_Args;

		if (IsAdvanced())
		{
			// ignore args, use our params
			krn.m_Fee = m_Adv.m_Fee;
			krn.m_Height = m_Adv.m_Height;
		}
		else
		{
			krn.m_Fee = fee;
			krn.m_Height = hr;
		}

		if (Flags::Dependent & m_Flags)
			krn.m_Dependent = true;

		FundsChangeMap fcm;
		for (auto it = m_Spend.begin(); m_Spend.end() != it; it++)
		{
			const auto& aid = it->first;
			const auto& val = it->second;

			bool bSpend = (val >= 0);
			Amount valUns(val);

			fcm.Add(bSpend ? valUns : (0 - valUns), aid, !bSpend);
		}

		fcm.ToCommitment(ptFunds);
	}

	void ContractInvokeEntry::GenerateAdv(Key::IKdf* pKdf, ECC::Scalar* pE, const ECC::Point& ptFullBlind, const ECC::Point& ptFullNonce, const ECC::Hash::Value* phvNonce, const ECC::Scalar* pForeignSig,
		const ECC::Point* pPks, uint32_t nPks, uint8_t nFlags, const ECC::Point* pForeign, uint32_t nForeign)
	{
		std::unique_ptr<TxKernelContractControl> pKrn;
		ECC::Point::Native ptFunds;
		CreateKrnUnsigned(pKrn, ptFunds, m_Adv.m_Height, m_Adv.m_Fee);

		auto& krn = *pKrn;

		if (Shaders::KernelFlag::FullCommitment & nFlags)
			krn.m_Commitment = ptFullBlind;
		else
		{
			ECC::Point::Native pt;
			pt.Import(ptFullBlind);
			pt += ptFunds;
			krn.m_Commitment = pt;
		}

		krn.UpdateMsg();

		ECC::Hash::Processor hp;
		krn.Prepare(hp, &m_ParentCtx.m_Hash);

		for (uint32_t i = 0; i < nPks; i++)
			hp << pPks[i];

		ECC::Hash::Value hv;
		hp
			<< krn.m_Commitment
			>> hv;

		ECC::Oracle oracle;
		m_Adv.m_Sig.m_NoncePub = ptFullNonce;
		m_Adv.m_Sig.Expose(oracle, hv);

		ECC::Scalar::Native skSig;

		for (uint32_t i = 0; ; i++)
		{
			oracle >> skSig;
			if (nPks == i)
				break;

			if (pE)
				pE[i] = skSig;
		}

		if (pKdf)
		{
			assert(phvNonce && pForeignSig);

			ECC::Scalar::Native skNonce, sk;

			pKdf->DeriveKey(skNonce, *phvNonce);
			pKdf->DeriveKey(sk, m_Adv.m_hvSk);

			skSig *= sk;
			skSig += skNonce;

			skNonce = *pForeignSig;
			skSig += skNonce;

			skSig = -skSig; // our formula is sig.ptNonce + Pk[i]*e[i] + G*sig.k == 0
			m_Adv.m_Sig.m_k = skSig;

			if (nForeign)
			{
				m_Flags |= Flags::HasPeers;
				m_Adv.m_vCosigners.assign(pForeign, pForeign + nForeign);

				if (Shaders::KernelFlag::CoSigner & nFlags)
					m_Flags |= Flags::RoleCosigner;
			}

			if (IsAdvanced())
			{
				m_Flags |= Flags::HasCommitment;
				m_Adv.m_Commitment = krn.m_Commitment;
			}
		}

	}

	void ContractInvokeEntry::Generate(Transaction& tx, Key::IKdf& kdf, const HeightRange& hr, Amount fee) const
	{
		ECC::Scalar::Native sk;
		std::unique_ptr<TxKernelContractControl> pKrn;
		ECC::Point::Native ptFunds;
		CreateKrnUnsigned(pKrn, ptFunds, hr, fee);

		auto& krn = *pKrn;

		if (IsAdvanced())
		{
			kdf.DeriveKey(sk, m_Adv.m_hvSk);

			if (Flags::HasCommitment & m_Flags)
				krn.m_Commitment = m_Adv.m_Commitment;
			else
			{
				// legacy
				ECC::Point::Native pt = ECC::Context::get().G * sk;
				pt += ptFunds;
				krn.m_Commitment = pt;
			}
			krn.m_Signature = m_Adv.m_Sig; // signed already

			krn.UpdateID();
		}
		else
		{
			std::vector<ECC::Scalar::Native> vSk;
			vSk.resize(m_vSig.size() + 1);

			for (uint32_t i = 0; i < m_vSig.size(); i++)
				kdf.DeriveKey(vSk[i], m_vSig[i]);

			ECC::Scalar::Native& skKrn = vSk.back();

			krn.m_Commitment = Zero;
			krn.UpdateMsg();

			ECC::Hash::Value hv;
			get_SigPreimage(hv, krn.m_Msg);

			kdf.DeriveKey(skKrn, hv);
			sk = skKrn;

			krn.Sign(&vSk.front(), static_cast<uint32_t>(vSk.size()), ptFunds, &m_ParentCtx.m_Hash);
		}

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
		if (IsAdvanced())
			return m_Adv.m_Fee;

		TxStats s;
		s.m_Kernels++;
		s.m_KernelsNonStd++;
		s.m_Contract++;
		s.m_ContractSizeExtra += static_cast<uint32_t>(m_Args.size() + m_Data.size());

		s.m_Outputs += (uint32_t) m_Spend.size();
		if (m_Spend.end() == m_Spend.find(0))
			s.m_Outputs++; // assume we have always beam output (either direct output or change).

		const auto& fs = Transaction::FeeSettings::get(h);

		Amount ret = fs.Calculate(s); // accounts for contract kernel size, but not charge
		std::setmax(ret, fs.get_DefaultStd()); // round up to std fee

		ret += fs.CalculateForBvm(s, m_Charge); // accounts for charge, adds at least min contract exec fee

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

	std::string ContractInvokeData::get_FullComment() const
    {
        std::string comment;
        for (size_t i = 0; i < m_vec.size(); i++)
        {
            if (i)
				comment += "; ";

            comment += m_vec[i].m_sComment;
        }
        return comment;
    }

    beam::Amount ContractInvokeData::get_FullFee(Height h) const
    {
        Amount fee = 0;
		for (const auto& cdata : m_vec)
		{
			fee += cdata.IsAdvanced() ?
				cdata.m_Adv.m_Fee : // can't change!
				cdata.get_FeeMin(h);
		}
        return fee;
    }

    bvm2::FundsMap ContractInvokeData::get_FullSpend() const
    {
        bvm2::FundsMap fm;
        for (const auto& cdata: m_vec)
            fm += cdata.m_Spend;

        return fm;
    }
}
