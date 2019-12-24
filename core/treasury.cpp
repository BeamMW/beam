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

	class Treasury::ThreadPool
	{
		std::vector<std::thread> m_vThreads;
	public:

		struct Context
		{
			virtual void Do(size_t iTask) = 0;

			void DoRange(size_t i0, size_t i1)
			{
				for (; i0 < i1; i0++)
					Do(i0);
			}

			void DoAll(size_t nTasks)
			{
				ThreadPool tp(*this, nTasks);
			}
		};

		struct Verifier
			:public Context
		{
			volatile bool m_bValid;
			Verifier() :m_bValid(true) {}

			virtual void Do(size_t iTask) override
			{
				typedef InnerProduct::BatchContextEx<4> MyBatch;

				std::unique_ptr<MyBatch> p(new MyBatch);
				MyBatch::Scope scope(*p);

				if (!Verify(iTask) || !p->Flush())
					m_bValid = false; // sync isn't required
			}

			virtual bool Verify(size_t iTask) = 0;

		};

		ThreadPool(Context& ctx, size_t nTasks)
		{
			size_t numCores = std::thread::hardware_concurrency();
			if (!numCores)
				numCores = 1; //?
			if (numCores > nTasks)
				numCores = nTasks;

			m_vThreads.resize(numCores);
			size_t iTask0 = 0;

			for (size_t i = 0; i < m_vThreads.size(); i++)
			{
				size_t iTask1 = nTasks * (i + 1) / numCores;
				assert(iTask1 > iTask0); // otherwise it means that redundant threads were created

				m_vThreads[i] = std::thread(&Context::DoRange, &ctx, iTask0, iTask1);

				iTask0 = iTask1;
			}

			assert(iTask0 == nTasks);
		}

		~ThreadPool()
		{
			for (size_t i = 0; i < m_vThreads.size(); i++)
				if (m_vThreads[i].joinable())
					m_vThreads[i].join();
		}
	};

	void Treasury::Request::Group::AddSubsidy(AmountBig::Type& res) const
	{
		for (size_t i = 0; i < m_vCoins.size(); i++)
			res += uintBigFrom(m_vCoins[i].m_Value);
	}

	void Treasury::Response::Group::Coin::get_SigMsg(Hash::Value& hv) const
	{
		Hash::Processor() << m_pOutput->m_Commitment >> hv;
	}

	void Treasury::get_ID(Key::IKdf& kdf, PeerID& pid, Scalar::Native& sk)
	{
		Key::ID kid(Zero);
		kid.m_Type = ECC::Key::Type::WalletID;

		kdf.DeriveKey(sk, kid);
		proto::Sk2Pk(pid, sk);
	}

	void Treasury::Response::Group::Create(const Request::Group& g, Key::IKdf& kdf, uint64_t& nIndex)
	{
		m_vCoins.resize(g.m_vCoins.size());

		Scalar::Native sk, offset = Zero;

		for (size_t iC = 0; iC < m_vCoins.size(); iC++)
		{
			const Request::Group::Coin& c0 = g.m_vCoins[iC];
			Coin& c = m_vCoins[iC];

			c.m_pOutput.reset(new Output);
			c.m_pOutput->m_Incubation = c0.m_Incubation;

			Key::IDV kidv(Zero);
			kidv.m_Idx = nIndex++;
			kidv.m_Type = Key::Type::Treasury;
			kidv.m_Value = c0.m_Value;

			c.m_pOutput->Create(Rules::HeightGenesis - 1, sk, kdf, kidv, kdf);
			offset += sk;

			Hash::Value hv;
			c.get_SigMsg(hv);
			c.m_Sig.Sign(hv, sk);
		}

		kdf.DeriveKey(sk, Key::ID(nIndex++, FOURCC_FROM(KeR3)));

		m_pKernel.reset(new TxKernelStd);
		Cast::Up<TxKernelStd>(*m_pKernel).Sign(sk);
		offset += sk;

		offset = -offset;
		m_Base.m_Offset = offset;

	}

	bool Treasury::Response::Group::IsValid(const Request::Group& g) const
	{
		if (m_vCoins.size() != g.m_vCoins.size())
			return false;

		Mode::Scope scope(Mode::Fast);

		if (!m_pKernel ||
			m_pKernel->m_Fee ||
			(m_pKernel->m_Height.m_Min > Rules::HeightGenesis) ||
			(m_pKernel->m_Height.m_Max != MaxHeight))
			return false;

		TxVectors::Full txv;
		TxVectors::Writer(txv, txv).Dump(Reader(*this));
		txv.Normalize();

		TxBase::Context::Params pars;
		TxBase::Context ctx(pars);
		ZeroObject(ctx.m_Height);
		if (!ctx.ValidateAndSummarize(m_Base, txv.get_Reader()))
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
		}

		return (ctx.m_Sigma == Zero);
	}

	void Treasury::Response::HashOutputs(Hash::Value& hv) const
	{
		Hash::Processor hp;
		for (size_t iG = 0; iG < m_vGroups.size(); iG++)
		{
			const Group& g = m_vGroups[iG];
			for (size_t iC = 0; iC < g.m_vCoins.size(); iC++)
				hp << g.m_vCoins[iC].m_pOutput->m_Commitment;
		}
		hp >> hv;
	}

	bool Treasury::Response::Create(const Request& r, Key::IKdf& kdf, uint64_t& nIndex)
	{
		PeerID pid;
		Scalar::Native sk;
		get_ID(kdf, pid, sk);

		if (pid != r.m_WalletID)
			return false;

		m_WalletID = r.m_WalletID;

		m_vGroups.resize(r.m_vGroups.size());

		struct Context
			:public ThreadPool::Context
		{
			const Request& m_Req;
			Response& m_Resp;
			Key::IKdf& m_Kdf;
			uint64_t m_Index0;

			Context(const Request& req, Response& resp, Key::IKdf& kdf, uint64_t nIndex)
				:m_Req(req)
				,m_Resp(resp)
				,m_Kdf(kdf)
				,m_Index0(nIndex)
			{}

			uint64_t get_IndexAt(size_t iG) const
			{
				uint64_t nIndex = m_Index0;
				for (size_t i = 0; i < iG; i++)
					nIndex += m_Req.m_vGroups[i].m_vCoins.size() + 1;

				return nIndex;
			}

			virtual void Do(size_t iTask) override
			{
				uint64_t nIndex = get_IndexAt(iTask);
				m_Resp.m_vGroups[iTask].Create(m_Req.m_vGroups[iTask], m_Kdf, nIndex);
				assert(get_IndexAt(iTask + 1) == nIndex);
			}

		} ctx(r, *this, kdf, nIndex);

		ctx.DoAll(m_vGroups.size());
		nIndex = ctx.get_IndexAt(m_vGroups.size());

		Hash::Value hv;
		HashOutputs(hv);

		m_Sig.Sign(hv, sk);
		return true;
	}

	bool Treasury::Response::IsValid(const Request& r) const
	{
		if ((m_vGroups.size() != r.m_vGroups.size()) ||
			(m_WalletID != r.m_WalletID))
			return false;

		struct Context
			:public ThreadPool::Verifier
		{
			const Request& m_Req;
			const Response& m_Resp;

			Context(const Request& req, const Response& resp)
				:m_Req(req)
				,m_Resp(resp)
			{}

			virtual bool Verify(size_t iTask) override
			{
				return m_Resp.m_vGroups[iTask].IsValid(m_Req.m_vGroups[iTask]);
			}

		} ctx(r, *this);

		ctx.DoAll(m_vGroups.size());
		if (!ctx.m_bValid)
			return false;

		// finally verify the signature
		Point::Native pk;
		if (!proto::ImportPeerID(pk, r.m_WalletID))
			return false;

		Hash::Value hv;
		HashOutputs(hv);

		return m_Sig.IsValid(hv, pk);
	}

	Treasury::Entry* Treasury::CreatePlan(const PeerID& pid, Amount nPerBlockAvg, const Parameters& pars)
	{
		EntryMap::iterator it = m_Entries.find(pid);
		if (m_Entries.end() != it)
			m_Entries.erase(it);

		Entry& e = m_Entries[pid];
		Request& r = e.m_Request;
		r.m_WalletID = pid;

		HeightRange hr;
		hr.m_Max = pars.m_Maturity0 + Rules::HeightGenesis - 1;

		for (uint32_t iBurst = 0; iBurst < pars.m_Bursts; iBurst++)
		{
			hr.m_Min = hr.m_Max + 1;
			hr.m_Max += pars.m_MaturityStep;

			AmountBig::Type valBig;
			Rules::get_Emission(valBig, hr, nPerBlockAvg);
			if (AmountBig::get_Hi(valBig))
				throw std::runtime_error("too large");

			Amount val = AmountBig::get_Lo(valBig);

			r.m_vGroups.emplace_back();
			Request::Group::Coin& c = r.m_vGroups.back().m_vCoins.emplace_back();
			c.m_Incubation = hr.m_Max;
			c.m_Value = val;
		}

		return &e;
	}

	bool Treasury::Data::Group::IsValid() const
	{
		Mode::Scope scope(Mode::Fast);

		TxBase::Context::Params pars;
		TxBase::Context ctx(pars);
		ZeroObject(ctx.m_Height); // current height is zero
		if (!ctx.ValidateAndSummarize(m_Data, m_Data.get_Reader()))
			return false;

		if (!(ctx.m_Stats.m_Fee == Zero))
			return false; // doesn't make sense for treasury

		ctx.m_Sigma = -ctx.m_Sigma;
		AmountBig::AddTo(ctx.m_Sigma, m_Value);

		return (ctx.m_Sigma == Zero);
	}

	bool Treasury::Data::IsValid() const
	{
		// finalize
		struct Context
			:public ThreadPool::Verifier
		{
			const Data& m_Data;
			Context(const Data& d) :m_Data(d) {}

			virtual bool Verify(size_t iTask) override
			{
				return m_Data.m_vGroups[iTask].IsValid();
			}

		} ctx(*this);

		ctx.DoAll(m_vGroups.size());
		return ctx.m_bValid;
	}

	std::vector<Treasury::Data::Burst> Treasury::Data::get_Bursts() const
	{
		std::vector<Treasury::Data::Burst> ret;
		ret.reserve(m_vGroups.size());

		for (size_t iG = 0; iG < m_vGroups.size(); iG++)
		{
			const Group& g = m_vGroups[iG];
			if (g.m_Value == Zero)
				continue;

			ret.emplace_back();
			Burst& b = ret.back();

			b.m_Value = AmountBig::get_Hi(g.m_Value) ?
				Amount(-1) :
				AmountBig::get_Lo(g.m_Value);

			b.m_Height = MaxHeight;

			for (size_t i = 0; i < g.m_Data.m_vOutputs.size(); i++)
				b.m_Height = std::min(b.m_Height, g.m_Data.m_vOutputs[i]->m_Incubation);
		}

		return ret;
	}

	void Treasury::Build(Data& d) const
	{
		// Current design: group only responses of the same height.
		typedef std::map<Height, Data::Group> GroupMap;
		GroupMap map;

		for (EntryMap::const_iterator itSrc = m_Entries.begin(); m_Entries.end() != itSrc; )
		{
			const Entry& e = (itSrc++)->second;
			if (!e.m_pResponse)
				continue;
			const Response& resp = *e.m_pResponse;

			for (size_t iG = 0; iG < resp.m_vGroups.size(); iG++)
			{
				const Response::Group& g = resp.m_vGroups[iG];

				Height h = MaxHeight;
				for (size_t i = 0; i < g.m_vCoins.size(); i++)
					h = std::min(h, g.m_vCoins[i].m_pOutput->m_Incubation);

				GroupMap::iterator it = map.find(h);
				bool bNew = (map.end() == it);
				if (bNew)
					it = map.emplace(h, Data::Group()).first;

				Data::Group& gOut = it->second;
				if (bNew)
				{
					ZeroObject(gOut.m_Value);
					gOut.m_Data.m_Offset = Zero;
				}

				Response::Group::Reader r(g);

				// merge
				TxVectors::Writer(gOut.m_Data, gOut.m_Data).Dump(std::move(r));
				e.m_Request.m_vGroups[iG].AddSubsidy(gOut.m_Value);

				Scalar::Native off = gOut.m_Data.m_Offset;
				off += g.m_Base.m_Offset;
				gOut.m_Data.m_Offset = off;
			}
		}

		d.m_vGroups.reserve(map.size());

		for (GroupMap::iterator it = map.begin(); map.end() != it; it++)
		{
			Data::Group& gSrc = it->second;
			if (!(gSrc.m_Value == Zero))
			{
				gSrc.m_Data.Normalize();
				d.m_vGroups.push_back(std::move(gSrc));
			}
		}

		// finalize
		if (!d.IsValid())
			throw std::runtime_error("Invalid block generated");
	}

	void Treasury::Data::Recover(Key::IPKdf& kdf, std::vector<Coin>& out) const
	{
		for (size_t iG = 0; iG < m_vGroups.size(); iG++)
		{
			const Group& g = m_vGroups[iG];
			for (size_t iO = 0; iO < g.m_Data.m_vOutputs.size(); iO++)
			{
				const Output& outp = *g.m_Data.m_vOutputs[iO];
				Key::IDV kidv;
				if (outp.Recover(Rules::HeightGenesis - 1, kdf, kidv))
				{
					out.emplace_back();
					out.back().m_Incubation = outp.m_Incubation;
					out.back().m_Kidv = kidv;
				}
			}
		}

		std::sort(out.begin(), out.end());
	}

	int Treasury::Data::Coin::cmp(const Coin& x) const
	{
		if (m_Incubation < x.m_Incubation)
			return -1;
		if (m_Incubation + x.m_Incubation)
			return 1;

		return m_Kidv.cmp(x.m_Kidv);
	}

} // namespace beam
