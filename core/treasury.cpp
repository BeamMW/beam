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

#include "treasury.h"
#include "proto.h"

namespace beam
{
	using namespace ECC;

	struct Treasury::Response::Group::Reader
		:public TxBase::IReader
	{
		const Group& m_This;
		size_t m_iOut;
		size_t m_iKrn;

		Reader(const Group& g) :m_This(g) {}

		virtual void Clone(Ptr& p) override
		{
			p.reset(new Reader(m_This));
		}

		virtual void Reset() override
		{
			m_iOut = 0;
			m_iKrn = 0;

			m_pUtxoIn = NULL;
			NextUtxoOut();
			NextKernel();
		}

		virtual void NextUtxoIn() override
		{
			assert(false);
		}

		virtual void NextUtxoOut() override
		{
			if (m_iOut < m_This.m_vCoins.size())
			{
				m_pUtxoOut = m_This.m_vCoins[m_iOut].m_pOutput.get();
				m_iOut++;
			}
			else
				m_pUtxoOut = NULL;
		}

		virtual void NextKernel() override
		{
			if (!m_iKrn)
			{
				m_pKernel = m_This.m_pKernel.get();
				m_iKrn++;
			}
			else
				m_pKernel = NULL;
		}
	};

	void Treasury::Request::Group::AddSubsidy(AmountBig& res)
	{
		for (size_t i = 0; i < m_vCoins.size(); i++)
			res += m_vCoins[i].m_Value;
	}

	void Treasury::Response::Group::Coin::get_SigMsg(Hash::Value& hv) const
	{
		Hash::Processor() << m_pOutput->m_Commitment >> hv;
	}

	void Treasury::get_ID(Key::IKdf& kdf, PeerID& pid, Scalar::Native& sk)
	{
		Key::ID kid;
		kid.m_Idx = 0;
		kid.m_Type = FOURCC_FROM(tRid);

		kdf.DeriveKey(sk, kid);
		proto::Sk2Pk(pid, sk);
	}

	void Treasury::Response::Group::Create(const Request::Group& g, Oracle& oracle, Key::IKdf& kdf, uint64_t& nIndex)
	{
		m_vCoins.resize(g.m_vCoins.size());

		Scalar::Native sk, offset = Zero;

		for (size_t iC = 0; iC < m_vCoins.size(); iC++)
		{
			const Request::Group::Coin& c0 = g.m_vCoins[iC];
			Coin& c = m_vCoins[iC];

			c.m_pOutput.reset(new Output);
			c.m_pOutput->m_Incubation = c0.m_Incubation;

			Key::IDV kidv;
			ZeroObject(kidv);
			kidv.m_Idx = nIndex++;
			kidv.m_Type = FOURCC_FROM(Tres);
			kidv.m_Value = c0.m_Value;

			c.m_pOutput->Create(sk, kdf, kidv);
			offset += sk;

			Hash::Value hv;
			c.get_SigMsg(hv);
			c.m_Sig.Sign(hv, sk);

			oracle << c.m_pOutput->m_Commitment;
		}

		Key::ID kid;
		kid.m_Idx = nIndex++;
		kid.m_Type = FOURCC_FROM(KeR3);
		kdf.DeriveKey(sk, kid);

		m_pKernel.reset(new TxKernel);
		m_pKernel->Sign(sk);
		offset += sk;

		offset = -offset;
		m_Base.m_Offset = offset;

	}

	bool Treasury::Response::Group::IsValid(const Request::Group& g, Oracle& oracle) const
	{
		if (m_vCoins.size() != g.m_vCoins.size())
			return false;

		Mode::Scope scope(Mode::Fast);

		if (!m_pKernel ||
			m_pKernel->m_Fee ||
			(m_pKernel->m_Height.m_Min > Rules::HeightGenesis) ||
			(m_pKernel->m_Height.m_Max != MaxHeight))
			return false;

		TxBase::Context ctx;
		ctx.m_bVerifyOrder = false;
		if (!ctx.ValidateAndSummarize(m_Base, Reader(*this)))
			return false;

		Point::Native comm, comm2;

		for (size_t iC = 0; iC < m_vCoins.size(); iC++)
		{
			const Request::Group::Coin& c0 = g.m_vCoins[iC];
			const Coin& c = m_vCoins[iC];

			if (!c.m_pOutput ||
				c.m_pOutput->m_pPublic ||
				c.m_pOutput->m_Coinbase ||
				(c.m_pOutput->m_Incubation != c0.m_Incubation) ||
				!comm.Import(c.m_pOutput->m_Commitment))
				return false;

			// verify the value
			comm2 = Context::get().H * c0.m_Value;
			comm2 = -comm2;

			ctx.m_Sigma += comm2;
			comm += comm2;

			Hash::Value hv;
			c.get_SigMsg(hv);
			if (!c.m_Sig.IsValid(hv, comm))
				return false;

			oracle << c.m_pOutput->m_Commitment;
		}

		return (ctx.m_Sigma == Zero);
	}

	void Treasury::Response::Group::Dump(TxBase::IWriter& w, TxBase& txb) const
	{
		w.Dump(Reader(*this));

		Scalar::Native off = txb.m_Offset;
		off += m_Base.m_Offset;
		txb.m_Offset = off;
		
	}

	bool Treasury::Response::Create(const Request& r, Key::IKdf& kdf, uint64_t& nIndex)
	{
		PeerID pid;
		Scalar::Native sk;
		get_ID(kdf, pid, sk);

		if (pid != r.m_WalletID)
			return false;

		m_vGroups.resize(r.m_vGroups.size());

		Oracle oracle;

		for (size_t iG = 0; iG < m_vGroups.size(); iG++)
			m_vGroups[iG].Create(r.m_vGroups[iG], oracle, kdf, nIndex);

		Hash::Value hv;
		oracle >> hv;

		m_Sig.Sign(hv, sk);
		return true;
	}

	bool Treasury::Response::IsValid(const Request& r) const
	{
		if (m_vGroups.size() != r.m_vGroups.size())
			return false;

		Oracle oracle;

		for (size_t iG = 0; iG < m_vGroups.size(); iG++)
			if (!m_vGroups[iG].IsValid(r.m_vGroups[iG], oracle))
				return false;

		// finally verify the signature
		Point::Native pk;
		if (!proto::ImportPeerID(pk, r.m_WalletID))
			return false;

		Hash::Value hv;
		oracle >> hv;

		return m_Sig.IsValid(hv, pk);
	}

} // namespace beam
