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

	void ManagerStd::Unfreeze()
	{
		assert(m_Freeze);
		if (!--m_Freeze)
			m_UnfreezeEvt.start();
			//OnUnfreezed();
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


	struct ManagerStd::RemoteRead
	{
		struct Base
			:public proto::FlyClient::Request::IHandler
		{
			ManagerStd* m_pThis;
			proto::FlyClient::Request::Ptr m_pRequest;

			~Base() { Abort(); }

			void Post()
			{
				assert(m_pRequest);
				Wasm::Test(m_pThis->m_pNetwork != nullptr);

				m_pThis->m_Freeze++;
				m_pThis->m_pNetwork->PostRequest(*m_pRequest, *this);
			}

			void Abort()
			{
				if (m_pRequest) {
					m_pRequest->m_pTrg = nullptr;
					m_pRequest.reset();
				}
			}

			virtual void OnComplete(proto::FlyClient::Request&)
			{
				assert(m_pRequest && m_pRequest->m_pTrg);
				m_pThis->Unfreeze();
			}
		};

		struct Vars
			:public Base
			,public IReadVars
		{
			size_t m_Consumed;
			ByteBuffer m_Buf;

			virtual bool MoveNext() override
			{
				assert(m_pRequest);
				auto& r = Cast::Up<proto::FlyClient::RequestContractVars>(*m_pRequest);

				if (r.m_pTrg)
				{
					r.m_pTrg = nullptr;
					m_Consumed = 0;
					m_Buf = std::move(r.m_Res.m_Result);
					if (m_Buf.empty())
						r.m_Res.m_bMore = false;
				}

				if (m_Consumed == m_Buf.size())
					return false;

				auto* pBuf = &m_Buf.front();

				Deserializer der;
				der.reset(pBuf + m_Consumed, m_Buf.size() - m_Consumed);

				der
					& m_LastKey.n
					& m_LastVal.n;

				m_Consumed = m_Buf.size() - der.bytes_left();

				uint32_t nTotal = m_LastKey.n + m_LastVal.n;
				Wasm::Test(nTotal >= m_LastKey.n); // no overflow
				Wasm::Test(nTotal <= der.bytes_left());

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
			:public Base
			,public IReadLogs
		{
			size_t m_Consumed;
			ByteBuffer m_Buf;

			virtual bool MoveNext() override
			{
				assert(m_pRequest);
				auto& r = Cast::Up<proto::FlyClient::RequestContractLogs>(*m_pRequest);

				if (r.m_pTrg)
				{
					r.m_pTrg = nullptr;
					m_Consumed = 0;
					m_Buf = std::move(r.m_Res.m_Result);
					if (m_Buf.empty())
						r.m_Res.m_bMore = false;
				}

				if (m_Consumed == m_Buf.size())
					return false;

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
				Wasm::Test(nTotal >= m_LastKey.n); // no overflow
				Wasm::Test(nTotal <= der.bytes_left());

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

	};


	void ManagerStd::VarsEnum(const Blob& kMin, const Blob& kMax, IReadVars::Ptr& pOut)
	{
		auto p = std::make_unique<RemoteRead::Vars>();
		p->m_pThis = this;

		boost::intrusive_ptr<proto::FlyClient::RequestContractVars> pReq(new proto::FlyClient::RequestContractVars);
		auto& r = *pReq;

		kMin.Export(r.m_Msg.m_KeyMin);
		kMax.Export(r.m_Msg.m_KeyMax);

		p->m_pRequest = std::move(pReq);
		p->Post();

		pOut = std::move(p);
	}

	void ManagerStd::LogsEnum(const Blob& kMin, const Blob& kMax, const HeightPos* pPosMin, const HeightPos* pPosMax, IReadLogs::Ptr& pOut)
	{
		auto p = std::make_unique<RemoteRead::Logs>();
		p->m_pThis = this;

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

		p->m_pRequest = std::move(pReq);
		p->Post();

		pOut = std::move(p);
	}


	Height ManagerStd::get_Height()
	{
		Wasm::Test(m_pHist);

		Block::SystemState::Full s;
		m_pHist->get_Tip(s); // zero-inits if no tip
		return s.m_Height;
	}

	bool ManagerStd::PerformRequestSync(proto::FlyClient::Request& r)
	{
		Wasm::Test(m_pNetwork != nullptr);

		struct MyHandler
			:public proto::FlyClient::Request::IHandler
		{
			proto::FlyClient::Request* m_pReq = nullptr;

			~MyHandler() {
				m_pReq->m_pTrg = nullptr;
			}

			virtual void OnComplete(proto::FlyClient::Request&) override
			{
				m_pReq->m_pTrg = nullptr;
				io::Reactor::get_Current().stop();
			}

		} myHandler;

		myHandler.m_pReq = &r;
		m_pNetwork->PostRequest(r, myHandler);

		io::Reactor::get_Current().run();

		return !r.m_pTrg;
	}

	bool ManagerStd::get_HdrAt(Block::SystemState::Full& s)
	{
		Wasm::Test(m_pHist);

		Height h = s.m_Height;
		if (m_pHist->get_At(s, h))
			return true;

		proto::FlyClient::RequestEnumHdrs::Ptr pReq(new proto::FlyClient::RequestEnumHdrs);
		auto& r = *pReq;
		r.m_Msg.m_Height = h;

		if (!PerformRequestSync(r))
			return false;

		if (1 != r.m_vStates.size())
			return false;

		s = std::move(r.m_vStates.front());
		return true;
	}

	bool ManagerStd::VarGetProof(Blob& key, ByteBuffer& val, beam::Merkle::Proof& proof)
	{
		proto::FlyClient::RequestContractVar::Ptr pReq(new proto::FlyClient::RequestContractVar);
		auto& r = *pReq;
		key.Export(r.m_Msg.m_Key);

		if (!PerformRequestSync(r))
			return false;

		if (r.m_Res.m_Proof.empty())
			return false;

		r.m_Res.m_Value.swap(val);
		r.m_Res.m_Proof.swap(proof);
		return true;
	}

	bool ManagerStd::LogGetProof(const HeightPos& hp, beam::Merkle::Proof& proof)
	{
		proto::FlyClient::RequestContractLogProof::Ptr pReq(new proto::FlyClient::RequestContractLogProof);
		auto& r = *pReq;
		r.m_Msg.m_Pos = hp;

		if (!PerformRequestSync(r))
			return false;

		if (r.m_Res.m_Proof.empty())
			return false;

		r.m_Res.m_Proof.swap(proof);
		return true;
	}

	void ManagerStd::OnReset()
	{
		InitMem();
		m_Code = m_BodyManager;
		m_Out.str("");
		m_Out.clear();
		decltype(m_vInvokeData)().swap(m_vInvokeData);
		m_Comms.Clear();

		m_mapReadVars.Clear();
		m_mapReadLogs.Clear();

		m_Freeze = 0;
		m_WaitingMsg = false;
	}

	void ManagerStd::StartRun(uint32_t iMethod)
	{
		OnReset();

		try {
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

		if (m_WaitingMsg)
			return; // shouldn't happen, but anyway

		m_Freeze++;
		m_WaitingMsg = true;

		if (static_cast<uint32_t>(-1) != nTimeout_ms)
		{
			if (!m_pOnMsgTimer)
				m_pOnMsgTimer = io::Timer::create(io::Reactor::get_Current());

			m_pOnMsgTimer->start(nTimeout_ms, false, [this]() { Comm_OnNewMsg(); });
		}

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
		if (m_WaitingMsg)
		{
			m_WaitingMsg = false;
			Unfreeze();

			if (m_pOnMsgTimer)
				m_pOnMsgTimer->cancel();
		}
	}

	void ManagerStd::RunSync()
	{
		while (!IsDone())
		{
			if (m_Freeze)
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
