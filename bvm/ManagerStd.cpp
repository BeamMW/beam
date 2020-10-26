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
			//m_UnfreezeEvt.start();
			OnUnfreezed();
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

	//void ManagerStd::UnfreezeEvt::OnSchedule()
	//{
	//	cancel();
	//	get_ParentObj().OnUnfreezed();
	//}

	void ManagerStd::AddSpend(Asset::ID aid, AmountSigned val)
	{
		if (!val)
			return;

		FundsMap::iterator it = m_Spend.find(aid);
		if (m_Spend.end() == it)
			m_Spend[aid] = val;
		else
		{
			it->second += val;
			if (!it->second)
				m_Spend.erase(it);
		}
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

	void ManagerStd::StartRun(uint32_t iMethod)
	{
		InitMem();
		m_Code = m_BodyManager;

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

	void ManagerStd::GenerateKernel(const bvm2::ContractID* pCid, uint32_t iMethod, const Blob& args, const Shaders::FundsChange* pFunds, uint32_t nFunds, const ECC::Hash::Value* pSig, uint32_t nSig)
	{
		Wasm::Test(m_pKdf != nullptr);

		std::unique_ptr<TxKernelContractControl> pKrn;

		if (iMethod)
		{
			assert(pCid);
			pKrn = std::make_unique<TxKernelContractInvoke>();
			auto& krn = Cast::Up<TxKernelContractInvoke>(*pKrn);
			krn.m_Cid = *pCid;
			krn.m_iMethod = iMethod;
		}
		else
		{
			pKrn = std::make_unique<TxKernelContractCreate>();
			auto& krn = Cast::Up<TxKernelContractCreate>(*pKrn);
			krn.m_Data = m_BodyContract;
		}

		args.Export(pKrn->m_Args);

		pKrn->m_Fee = m_Fee;
		pKrn->m_Height = m_Height;
		pKrn->m_Commitment = Zero;
		pKrn->UpdateMsg();

		ECC::Hash::Processor hp;
		hp
			<< pKrn->m_Msg
			<< nFunds
			<< nSig;

		std::vector<ECC::Scalar::Native> vSk;
		vSk.resize(nSig + 1);

		for (uint32_t i = 0; i < nSig; i++)
		{
			m_pKdf->DeriveKey(vSk[i], pSig[i]);
			hp << pSig[i];
		}

		bvm2::FundsChangeMap fcm;
		for (uint32_t i = 0; i < nFunds; i++)
		{
			const auto& x = pFunds[i];
			fcm.Process(x.m_Amount, x.m_Aid, !!x.m_Consume);

			AmountSigned val = x.m_Amount;
			if (!x.m_Consume)
				val = -val;
			AddSpend(x.m_Aid, val);

			hp
				<< x.m_Aid
				<< x.m_Amount
				<< x.m_Consume;
		}

		AddSpend(0, m_Fee);

		ECC::Point::Native ptFunds;
		fcm.ToCommitment(ptFunds);
		ptFunds = -ptFunds;

		ECC::Scalar::Native& skKrn = vSk.back();

		ECC::Hash::Value hv;
		hp >> hv;
		m_pKdf->DeriveKey(skKrn, hv);

		pKrn->Sign(&vSk.front(), static_cast<uint32_t>(vSk.size()), ptFunds);

		m_vKernels.push_back(std::move(pKrn));
		m_skOffset += -skKrn;
	}





















} // namespace bvm2
} // namespace beam
