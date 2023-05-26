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

#include "ManagerStd.h"
#include "../core/serialization_adapters.h"

namespace beam {
namespace bvm2 {

	ManagerStd::ManagerStd()
	{
		m_pOut = &m_Out;
	}


	bool ManagerStd::IsSuspended()
	{
		return m_Pending.m_pBlocker != nullptr;
	}

	void ManagerStd::Pending::OnDone(IBase& x)
	{
		if (&x == m_pBlocker)
		{
			m_pBlocker = nullptr;
			get_ParentObj().m_UnfreezeEvt.start();
		}
	}

	void ManagerStd::OnUnfreezed()
	{
		try {
			RunSync();
		}
		catch (const std::exception& exc) {
			OnDone(&exc);
		}
	}

	void ManagerStd::UnfreezeEvt::OnSchedule()
	{
		cancel();
		get_ParentObj().OnUnfreezed();
	}

	void ManagerStd::SetParentContext(std::unique_ptr<beam::Merkle::Hash>& pTrg) const
	{
		assert(!pTrg);
		if (m_Context.m_pParent)
			pTrg = std::make_unique<beam::Merkle::Hash>(*m_Context.m_pParent);
	}

	struct ManagerStd::RemoteRead
	{
		struct Handler
			:public Pending::IBase
			,public proto::FlyClient::Request::IHandler
		{
			ManagerStd& m_This;
			proto::FlyClient::Request::Ptr m_pRequest;

			Handler(ManagerStd& x)
				:m_This(x)
			{
			}

			~Handler()
			{
				if (m_pRequest)
				{
					m_pRequest->m_pTrg = nullptr;
					m_pRequest.reset();
				}
			}

			void Post()
			{
				assert(m_pRequest);
				Exc::Test(m_This.m_pNetwork != nullptr);
				m_This.m_pNetwork->PostRequest(*m_pRequest, *this);
			}

			bool CheckDone()
			{
				if (!m_pRequest->m_pTrg)
					return true;

				m_This.m_Pending.m_pBlocker = this;
				return false;
			}

			virtual void OnComplete(proto::FlyClient::Request&)
			{
				assert(m_pRequest && (this == m_pRequest->m_pTrg));
				m_pRequest->m_pTrg = nullptr;
				m_This.m_Pending.OnDone(*this);
			}
		};

		struct Vars
			:public Handler
			,public IReadVars
		{
			using Handler::Handler;

			size_t m_Consumed = 0;
			ByteBuffer m_Buf;

			virtual bool MoveNext() override
			{
				assert(m_pRequest);
				auto& r = Cast::Up<proto::FlyClient::RequestContractVars>(*m_pRequest);

				if (m_Consumed == m_Buf.size())
				{
					if (!CheckDone())
						return false;

					if (r.m_Res.m_Result.empty())
						return false;

					m_Consumed = 0;
					m_Buf = std::move(r.m_Res.m_Result);
				}

				auto* pBuf = &m_Buf.front();

				Deserializer der;
				der.reset(pBuf + m_Consumed, m_Buf.size() - m_Consumed);

				der
					& m_LastKey.n
					& m_LastVal.n;

				m_Consumed = m_Buf.size() - der.bytes_left();

				uint32_t nTotal = m_LastKey.n + m_LastVal.n;
				Exc::Test(nTotal >= m_LastKey.n); // no overflow
				Exc::Test(nTotal <= der.bytes_left());

				m_LastKey.p = pBuf + m_Consumed;
				m_Consumed += m_LastKey.n;

				m_LastVal.p = pBuf + m_Consumed;
				m_Consumed += m_LastVal.n;

				if ((m_Consumed == m_Buf.size()) && r.m_Res.m_bMore)
				{
					r.m_Res.m_bMore = false;

					// ask for more
					m_LastKey.Export(r.m_Msg.m_KeyMin);
					r.m_Msg.m_bSkipMin = true;

					Post();
				}

				return true;
			}
		};


		struct Logs
			:public Handler
			,public IReadLogs
		{
			using Handler::Handler;

			size_t m_Consumed = 0;
			ByteBuffer m_Buf;

			virtual bool MoveNext() override
			{
				assert(m_pRequest);
				auto& r = Cast::Up<proto::FlyClient::RequestContractLogs>(*m_pRequest);

				if (m_Consumed == m_Buf.size())
				{
					if (!CheckDone())
						return false;
					if (r.m_Res.m_Result.empty())
						return false;

					m_Consumed = 0;
					m_Buf = std::move(r.m_Res.m_Result);
				}

				auto* pBuf = &m_Buf.front();

				Deserializer der;
				der.reset(pBuf + m_Consumed, m_Buf.size() - m_Consumed);

				der
					& m_LastPos
					& m_LastKey.n
					& m_LastVal.n;

				if (m_LastPos.m_Height)
				{
					r.m_Msg.m_PosMin.m_Height += m_LastPos.m_Height;
					r.m_Msg.m_PosMin.m_Pos = 0;
				}

				r.m_Msg.m_PosMin.m_Pos += m_LastPos.m_Pos;
				m_LastPos = r.m_Msg.m_PosMin;

				m_Consumed = m_Buf.size() - der.bytes_left();

				uint32_t nTotal = m_LastKey.n + m_LastVal.n;
				Exc::Test(nTotal >= m_LastKey.n); // no overflow
				Exc::Test(nTotal <= der.bytes_left());

				m_LastKey.p = pBuf + m_Consumed;
				m_Consumed += m_LastKey.n;

				m_LastVal.p = pBuf + m_Consumed;
				m_Consumed += m_LastVal.n;

				if ((m_Consumed == m_Buf.size()) && r.m_Res.m_bMore)
				{
					// ask for more
					r.m_Res.m_bMore = false;
					r.m_Msg.m_PosMin.m_Pos++;

					Post();
				}

				return true;
			}
		};


		struct Assets
			:public Handler
			,public IReadAssets
		{
			using Handler::Handler;

			size_t m_iPos = 0;
			std::vector<Asset::Full> m_Res;

			virtual bool MoveNext() override
			{
				assert(m_pRequest);
				auto& r = Cast::Up<proto::FlyClient::RequestAssetsListAt>(*m_pRequest);

				if (m_Res.empty())
				{
					if (!CheckDone())
						return false;
					if (r.m_Res.empty())
						return false;

					m_iPos = 0;
					m_Res = std::move(r.m_Res);
				}

				if (m_iPos >= m_Res.size())
					return false;

				m_aiLast = std::move(m_Res[m_iPos++]);
				return true;
			}
		};

	};

	void ManagerStd::PerformSingleRequest(proto::FlyClient::Request& r)
	{
		auto p = std::make_unique<RemoteRead::Handler>(*this);
		p->m_pRequest = &r;
		p->Post();

		p->CheckDone();
		m_Pending.m_pSingleRequest = std::move(p);
	}

	proto::FlyClient::Request::Ptr ManagerStd::GetResSingleRequest()
	{
		assert(m_Pending.m_pSingleRequest);
		auto& h = Cast::Up<RemoteRead::Handler>(*m_Pending.m_pSingleRequest);

		assert(h.m_pRequest);
		auto pRet = h.m_pRequest; // for more safety don't use std::move(), let the Handler cancel the Request if it's still pending (though that shouldn't happen)

		m_Pending.m_pSingleRequest.reset();
		return pRet;
	}

	void ManagerStd::VarsEnum(const Blob& kMin, const Blob& kMax, IReadVars::Ptr& pOut)
	{
		auto p = std::make_unique<RemoteRead::Vars>(*this);

		boost::intrusive_ptr<proto::FlyClient::RequestContractVars> pReq(new proto::FlyClient::RequestContractVars);
		auto& r = *pReq;

		kMin.Export(r.m_Msg.m_KeyMin);
		kMax.Export(r.m_Msg.m_KeyMax);

		SetParentContext(r.m_pCtx);
		p->m_pRequest = std::move(pReq);
		p->Post();

		pOut = std::move(p);
	}

	void ManagerStd::LogsEnum(const Blob& kMin, const Blob& kMax, const HeightPos* pPosMin, const HeightPos* pPosMax, IReadLogs::Ptr& pOut)
	{
		auto p = std::make_unique<RemoteRead::Logs>(*this);

		boost::intrusive_ptr<proto::FlyClient::RequestContractLogs> pReq(new proto::FlyClient::RequestContractLogs);
		auto& r = *pReq;

		kMin.Export(r.m_Msg.m_KeyMin);
		kMax.Export(r.m_Msg.m_KeyMax);

		if (pPosMin)
			r.m_Msg.m_PosMin = *pPosMin;

		if (pPosMax)
			r.m_Msg.m_PosMax = *pPosMax;
		else
			r.m_Msg.m_PosMax.m_Height = MaxHeight;

		SetParentContext(r.m_pCtx);
		p->m_pRequest = std::move(pReq);
		p->Post();

		pOut = std::move(p);
	}

	void ManagerStd::AssetsEnum(Asset::ID aid0, Height h, IReadAssets::Ptr& pOut)
	{
		auto p = std::make_unique<RemoteRead::Assets>(*this);

		boost::intrusive_ptr<proto::FlyClient::RequestAssetsListAt> pReq(new proto::FlyClient::RequestAssetsListAt);
		auto& r = *pReq;

		r.m_Msg.m_Aid0 = aid0;
		r.m_Msg.m_Height = h;

		p->m_pRequest = std::move(pReq);
		p->Post();

		pOut = std::move(p);
	}

	void ManagerStd::SelectContext(bool bDependent, uint32_t /* nChargeNeeded */)
	{
		if (!m_Pending.m_pSingleRequest)
		{
			if (m_EnforceDependent)
				bDependent = true;

			proto::FlyClient::RequestEnsureSync::Ptr pReq(new proto::FlyClient::RequestEnsureSync);
			pReq->m_IsDependent = bDependent;

			PerformSingleRequest(*pReq);
		}

		if (!IsSuspended())
		{
			auto pReq = GetResSingleRequest();
			auto& r = pReq->As<proto::FlyClient::RequestEnsureSync>();

			Exc::Test(m_pHist);

			Block::SystemState::Full s;
			m_pHist->get_Tip(s); // zero-inits if no tip

			m_Context.m_Height = s.m_Height;

			if (r.m_IsDependent)
			{
				uint32_t n = 0;
				const auto* pV = m_pNetwork->get_DependentState(n);
				const auto& hvCtx = n ? pV[n - 1] : s.m_Prev;
				m_Context.m_pParent = std::make_unique<beam::Merkle::Hash>(hvCtx);
			}
		}
	}

	bool ManagerStd::get_HdrAt(Block::SystemState::Full& s)
	{
		if (!m_Pending.m_pSingleRequest)
		{
			Exc::Test(m_pHist);

			Height h = s.m_Height;
			if (m_pHist->get_At(s, h))
				return true;

			proto::FlyClient::RequestEnumHdrs::Ptr pReq(new proto::FlyClient::RequestEnumHdrs);
			pReq->m_Msg.m_Height = h;

			PerformSingleRequest(*pReq);
		}

		if (!IsSuspended())
		{
			auto pReq = GetResSingleRequest();
			auto& r = pReq->As<proto::FlyClient::RequestEnumHdrs>();

			if (1 == r.m_vStates.size())
			{
				s = std::move(r.m_vStates.front());
				return true;
			}
		}

		return false;
	}

	bool ManagerStd::VarGetProof(Blob& key, ByteBuffer& val, beam::Merkle::Proof& proof)
	{
		if (!m_Pending.m_pSingleRequest)
		{
			proto::FlyClient::RequestContractVar::Ptr pReq(new proto::FlyClient::RequestContractVar);
			key.Export(pReq->m_Msg.m_Key);

			PerformSingleRequest(*pReq);
		}

		if (!IsSuspended())
		{
			auto pReq = GetResSingleRequest();
			auto& r = pReq->As<proto::FlyClient::RequestContractVar>();

			if (!r.m_Res.m_Proof.empty())
			{
				r.m_Res.m_Value.swap(val);
				r.m_Res.m_Proof.swap(proof);
				return true;
			}
		}

		return false;
	}

	bool ManagerStd::get_AssetInfo(Asset::Full& ai)
	{
		if (!m_Pending.m_pSingleRequest)
		{
			proto::FlyClient::RequestAsset::Ptr pReq(new proto::FlyClient::RequestAsset);
			pReq->m_Msg.m_AssetID = ai.m_ID;
			PerformSingleRequest(*pReq);
		}

		if (!IsSuspended())
		{
			auto pReq = GetResSingleRequest();
			auto& r = pReq->As<proto::FlyClient::RequestAsset>();

			if (!r.m_Res.m_Proof.empty())
			{
				ai = std::move(r.m_Res.m_Info);
				return true;
			}
		}

		return false;
	}

	bool ManagerStd::LogGetProof(const HeightPos& hp, beam::Merkle::Proof& proof)
	{
		if (!m_Pending.m_pSingleRequest)
		{
			proto::FlyClient::RequestContractLogProof::Ptr pReq(new proto::FlyClient::RequestContractLogProof);
			pReq->m_Msg.m_Pos = hp;

			PerformSingleRequest(*pReq);
		}

		if (!IsSuspended())
		{
			auto pReq = GetResSingleRequest();
			auto& r = pReq->As<proto::FlyClient::RequestContractLogProof>();

			if (!r.m_Res.m_Proof.empty())
			{
				r.m_Res.m_Proof.swap(proof);
				return true;
			}
		}

		return false;
	}

	void ManagerStd::OnReset()
	{
		ProcessorManager::ResetBase();

		m_Code = m_BodyManager;
		m_Out.str("");
		m_Out.clear();

		m_UnfreezeEvt.cancel();

		m_Pending.m_pSingleRequest.reset();
		m_Pending.m_pCommMsg.reset();
		m_Pending.m_pBlocker = nullptr;
	}

	void ManagerStd::StartRun(uint32_t iMethod)
	{
		OnReset();

		try {

			if (m_EnforceDependent)
				EnsureContext();

			CallMethod(iMethod);
			RunSync();
		}
		catch (const std::exception& exc) {
			OnDone(&exc);
		}
	}

	void ManagerStd::Comm_Wait(uint32_t nTimeout_ms)
	{
		assert(m_Comms.m_Rcv.empty());

		struct PendingCommMsg
			:public Pending::IBase
		{
			virtual ~PendingCommMsg() {} // auto
			io::Timer::Ptr m_pTimer;
		};

		auto p = std::make_unique<PendingCommMsg>();

		if (static_cast<uint32_t>(-1) != nTimeout_ms)
		{
			p->m_pTimer = io::Timer::create(io::Reactor::get_Current());
			p->m_pTimer->start(nTimeout_ms, false, [this]() { Comm_OnNewMsg(); });
		}

		m_Pending.m_pCommMsg = std::move(p);
		m_Pending.m_pBlocker = m_Pending.m_pCommMsg.get();

	}

	void ManagerStd::Comm_OnNewMsg(const Blob& msg, Comm::Channel& c)
	{
		if (!msg.n)
			return; // ignore empty msgs

		auto* pItem = c.m_List.Create_back();
		pItem->m_pChannel = &c;

		m_Comms.m_Rcv.push_back(pItem->m_Global);
		pItem->m_Global.m_pList = &m_Comms.m_Rcv;
			
		msg.Export(pItem->m_Msg);

		Comm_OnNewMsg();
	}

	void ManagerStd::Comm_OnNewMsg()
	{
		if (m_Pending.m_pCommMsg)
		{
			m_Pending.OnDone(*m_Pending.m_pCommMsg);
			m_Pending.m_pCommMsg.reset();
		}
	}

	void ManagerStd::RunSync()
	{
		while (!IsDone())
		{
			if (IsSuspended())
				return;

			RunOnce();
		}
		OnDone(nullptr);
	}

	void ManagerStd::get_ContractShader(ByteBuffer& res)
	{
		res = m_BodyContract;
	}

	bool ManagerStd::get_SpecialParam(const char* sz, Blob& b)
	{
		if (!strcmp(sz, "contract.shader"))
		{
			b = m_BodyContract;
			return true;
		}

		if (!strcmp(sz, "app.shader"))
		{
			b = m_BodyManager; // not really useful for the app shader to get its own bytecode, but ok.
			return true;
		}

		return false;
	}

} // namespace bvm2
} // namespace beam
