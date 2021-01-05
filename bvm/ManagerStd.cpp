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

	void ManagerStd::VarsRead::Abort()
	{
		if (m_pRequest) {
			m_pRequest->m_pTrg = nullptr;
			m_pRequest.reset();
		}
	}

	void ManagerStd::VarsRead::OnComplete(proto::FlyClient::Request&)
	{
		assert(m_pRequest);
		auto& r = *m_pRequest;

		r.m_Consumed = 0;
		if (r.m_Res.m_Result.empty())
			r.m_Res.m_bMore = 0;

		get_ParentObj().Unfreeze();
	}

	void ManagerStd::VarsEnum(const Blob& kMin, const Blob& kMax)
	{
		Wasm::Test(m_pNetwork != nullptr);

		m_VarsRead.Abort();
		m_VarsRead.m_pRequest.reset(new VarsRead::Request);
		auto& r = *m_VarsRead.m_pRequest;

		kMin.Export(r.m_Msg.m_KeyMin);
		kMax.Export(r.m_Msg.m_KeyMax);

		m_Freeze++;
		m_pNetwork->PostRequest(*m_VarsRead.m_pRequest, m_VarsRead);
	}

	bool ManagerStd::VarsMoveNext(Blob& key, Blob& val)
	{
		if (!m_VarsRead.m_pRequest)
			return false; // enum was not called
		auto& r = *m_VarsRead.m_pRequest;

		if (r.m_Consumed == r.m_Res.m_Result.size())
		{
			m_VarsRead.Abort();
			return false;
		}

		auto* pBuf = &r.m_Res.m_Result.front();

		Deserializer der;
		der.reset(pBuf + r.m_Consumed, r.m_Res.m_Result.size() - r.m_Consumed);

		der
			& key.n
			& val.n;

		r.m_Consumed = r.m_Res.m_Result.size() - der.bytes_left();

		uint32_t nTotal = key.n + val.n;
		Wasm::Test(nTotal >= key.n); // no overflow
		Wasm::Test(nTotal <= der.bytes_left());

		key.p = pBuf + r.m_Consumed;
		r.m_Consumed += key.n;

		val.p = pBuf + r.m_Consumed;
		r.m_Consumed += val.n;

		if ((r.m_Consumed == r.m_Res.m_Result.size()) && r.m_Res.m_bMore)
		{
			r.m_Res.m_bMore = false;
			r.m_Res.m_Result.clear();

			// ask for more
			key.Export(r.m_Msg.m_KeyMin);
			r.m_Msg.m_bSkipMin = true;

			m_Freeze++;
			m_pNetwork->PostRequest(*m_VarsRead.m_pRequest, m_VarsRead);
		}

		return true;
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

	void ManagerStd::StartRun(uint32_t iMethod)
	{
		InitMem();
		m_Code = m_BodyManager;
		m_Out.str("");
		m_Out.clear();
		decltype(m_vInvokeData)().swap(m_vInvokeData);

		try {
			CallMethod(iMethod);
			RunSync();
		}
		catch (const std::exception& exc) {
			OnDone(&exc);
		}
	}


	void ManagerStd::RunSync()
	{
		while (m_LocalDepth)
		{
			if (m_Freeze)
				return;

			RunOnce();
		}
		OnDone(nullptr);
	}

	void ManagerStd::DerivePk(ECC::Point& pubKey, const ECC::Hash::Value& hv)
	{
		Wasm::Test(m_pPKdf != nullptr);

		ECC::Point::Native pt;
		m_pPKdf->DerivePKeyG(pt, hv);
		pubKey = pt;
	}

	void ManagerStd::GenerateKernel(const ContractID* pCid, uint32_t iMethod, const Blob& args, const Shaders::FundsChange* pFunds, uint32_t nFunds, const ECC::Hash::Value* pSig, uint32_t nSig, const char* szComment, Amount nFee)
	{
		ContractInvokeData& v = m_vInvokeData.emplace_back();

		if (iMethod)
		{
			assert(pCid);
			v.m_Cid = *pCid;
		}
		else
		{
			v.m_Cid = Zero;
			v.m_Data = m_BodyContract;
		}

		v.m_iMethod = iMethod;
		args.Export(v.m_Args);
		v.m_vSig.assign(pSig, pSig + nSig);
		v.m_sComment = szComment;
		v.m_Fee = nFee;

		for (uint32_t i = 0; i < nFunds; i++)
		{
			const auto& x = pFunds[i];
			AmountSigned val = x.m_Amount;
			if (!x.m_Consume)
				val = -val;

			v.m_Spend.AddSpend(x.m_Aid, val);
		}
	}

	void ContractInvokeData::Generate(Transaction& tx, Key::IKdf& kdf, const HeightRange& hr) const
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
		pKrn->m_Fee = m_Fee;
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

	void FundsMap::AddSpend(Asset::ID aid, AmountSigned val)
	{
		// don't care about overflow.
		// In case of the overflow the spendmap would be incorrect, and the wallet would fail to build a balanced tx.
		// No threat of illegal inflation or unauthorized funds spend.
		if (!val)
			return;

		FundsMap::iterator it = find(aid);
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

	void FundsMap::operator += (const ContractInvokeData& x)
	{
		*this += x.m_Spend;
		(*this)[0] += x.m_Fee;
	}

	void FundsMap::operator += (const std::vector<ContractInvokeData>& v)
	{
		for (size_t i = 0; i < v.size(); i++)
			*this += v[i];
	}



} // namespace bvm2
} // namespace beam
