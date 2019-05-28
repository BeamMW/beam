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

#include "lightning.h"

namespace beam {
namespace Lightning {

struct Channel::MuSigLocator
	:public proto::FlyClient::RequestUtxo
{
	typedef boost::intrusive_ptr<MuSigLocator> Ptr;

	virtual ~MuSigLocator() {}

	size_t m_iIndex;
	size_t m_nTotal; // size of m_vUpdates may change during lookup, this is to prevent the confuse
	bool m_Initiator;
};

Channel::State::Enum Channel::get_State() const
{
	if (!m_pOpen)
		return m_pNegCtx ? State::Opening0 : State::None;

	if (!m_pOpen->m_hOpened)
	{
		if (m_pNegCtx)
			// only 1 peer has the opening tx
			return m_pOpen->m_txOpen.m_vKernels.empty() ? State::Opening1 : State::Opening0;

		return (m_State.m_hQueryLast < m_pOpen->m_hrLimit.m_Max) ? State::Opening2 : State::OpenFailed;
	}

	if (!m_State.m_Close.m_hPhase1)
		return m_State.m_Terminate ? State::Closing1 : State::Open;

	return m_State.m_Close.m_hPhase2 ? State::Closed : State::Closing2;
}

bool Channel::IsUnfairPeerClosed() const
{
	assert(m_State.m_Close.m_hPhase1);
	assert(m_State.m_Close.m_iPath < m_vUpdates.size());

	return
		!m_State.m_Close.m_Initiator &&
		(m_State.m_Close.m_iPath + 1 < m_vUpdates.size()) &&
		m_vUpdates[m_State.m_Close.m_iPath + 1]->m_PeerKeyValid;

}

void Channel::UpdateNegotiation(Storage::Map& dataIn, Storage::Map& dataOut)
{
	if (!m_pNegCtx)
		return;

	assert(!m_State.m_Terminate);

	IBase* pBase = nullptr;

	switch (m_pNegCtx->m_eType)
	{
	case NegotiationCtx::Open:
		pBase = &Cast::Up<NegotiationCtx_Open>(*m_pNegCtx).m_Inst;
		break;

	case NegotiationCtx::Update:
		pBase = &Cast::Up<NegotiationCtx_Update>(*m_pNegCtx).m_Inst;
		break;

	default:
		assert(false);
		return;
	}

	// set the data (carefully!)
	for (Storage::Map::iterator it = dataIn.begin(); dataIn.end() != it; it++)
	{
		const uint32_t& code = it->first;

		const uint32_t msk = (uint32_t(1) << 16) - 1;
		if ((code & msk) >= Codes::Private)
			continue; // ignore

		if (m_pNegCtx->m_Data.end() != m_pNegCtx->m_Data.find(code))
			continue; // as well

		m_pNegCtx->m_Data[code] = std::move(it->second);
	}

	Gateway::Direct gw(dataOut);

	pBase->m_pStorage = &m_pNegCtx->m_Data;
	pBase->m_pGateway = &gw;
	assert(pBase->m_pKdf);

	uint32_t status = 0;

	switch (m_pNegCtx->m_eType)
	{
	case NegotiationCtx::Open:
		{
			ChannelOpen& x = Cast::Up<ChannelOpen>(*pBase);
			uint8_t nDone0;

			{
				ChannelOpen::Worker wrk(x);
				nDone0 = x.get_DoneParts();
			}

			status = x.Update();

			ChannelOpen::Worker wrk(x);
			uint8_t nDone1 = x.get_DoneParts();

			if (nDone0 == nDone1)
				break;

			if (!(ChannelOpen::DoneParts::Main & nDone1))
				break;

			ChannelOpen::Result res;
			x.get_Result(res);

			if (!(ChannelOpen::DoneParts::Main & nDone0))
			{
				assert(!m_pOpen);
				m_pOpen.reset(new DataOpen);
				m_pOpen->m_hOpened = 0; // not yet

				m_pOpen->m_Comm0 = res.m_Comm0;

				m_pOpen->m_txOpen = std::move(res.m_txOpen);

				x.m_Tx0.Get(m_pOpen->m_hvKernel0, MultiTx::Codes::KernelID);
				x.m_Tx0.Get(m_pOpen->m_hrLimit.m_Min, MultiTx::Codes::KrnH0);
				x.m_Tx0.Get(m_pOpen->m_hrLimit.m_Max, MultiTx::Codes::KrnH1);

				assert(m_vUpdates.empty());
				AddUpdatePoint();

			}

			assert(m_pOpen && (m_vUpdates.size() == 1));


			DataUpdate& d = *m_vUpdates.front();
			Cast::Down<ChannelWithdrawal::Result>(d) = std::move(res);

			d.m_RevealedSelfKey = false;
			d.m_PeerKeyValid = false;
			d.m_PeerKey = Zero;

			Key::IDV* pA = &d.m_msMy;
			Key::IDV* pB = &d.m_msPeer;
			SwapIfRole(x, pA, pB);

			x.Setup(false, nullptr, nullptr, &m_pOpen->m_ms0, MultiTx::KernelParam(), pA, pB, nullptr, WithdrawTx::CommonParam());
		}
		break;

	case NegotiationCtx::Update:
		{
			ChannelUpdate& x = Cast::Up<ChannelUpdate>(*pBase);
			uint8_t nDone0;

			{
				ChannelUpdate::Worker wrk(x);
				nDone0 = x.get_DoneParts();
			}

			status = x.Update();

			ChannelUpdate::Worker wrk(x);
			uint8_t nDone1 = x.get_DoneParts();

			if (nDone0 == nDone1)
				break;

			const uint8_t nFlags0 =
				ChannelUpdate::DoneParts::Main |
				ChannelUpdate::DoneParts::PeerWd;

			if (!(nDone1 & nFlags0))
				break;

			if (!(nDone0 & nFlags0))
			{
				AddUpdatePoint();
			}

			assert(m_vUpdates.size() > 1);
			DataUpdate& d = *m_vUpdates.back();

			x.get_Result(d);

			Key::IDV* pA = &d.m_msMy;
			Key::IDV* pB = &d.m_msPeer;
			SwapIfRole(x, pA, pB);

			x.Setup(false, nullptr, nullptr, pA, pB, nullptr, WithdrawTx::CommonParam(), nullptr, nullptr, nullptr);
		}
		break;

	default:
		assert(false);
	}

	if (status)
	{
		m_pNegCtx.reset(); // always reset the moment it's finished

		if (Status::Success != status)
		{
			// initiate channel closure
			m_State.m_Terminate = true;
		}
	}

	Update();
}

void Channel::Update()
{
	if (m_State.m_Close.m_hPhase2)
		return; // all finished

	if (!m_pOpen)
		return; // negotiation is still in progress

	Height hTip = get_Tip();

	if (!m_pOpen->m_hOpened)
	{
		if (hTip < m_pOpen->m_hrLimit.m_Min)
			return;

		if (!m_pOpen->m_txOpen.m_vKernels.empty() && (hTip < m_pOpen->m_hrLimit.m_Max))
			SendTxNoSpam(m_pOpen->m_txOpen, hTip);

		if (m_State.m_hQueryLast < m_pOpen->m_hrLimit.m_Max)
			ConfirmKernel(m_pOpen->m_hvKernel0, hTip); // ask again for confirmation

		return;
	}

	if (m_State.m_Close.m_hPhase1)
	{
		assert(m_State.m_Terminate);

		// msigN confirmed.
		assert(m_State.m_Close.m_iPath < m_vUpdates.size());

		if (IsUnfairPeerClosed() || (m_State.m_Close.m_hPhase1 + m_Params.m_hLockTime <= hTip + 1))
		{
			const DataUpdate& d = *m_vUpdates[m_State.m_Close.m_iPath];
			const Transaction& tx = d.get_TxPhase2(m_State.m_Close.m_Initiator);
			SendTxNoSpam(tx, hTip);

			if (hTip > m_State.m_hQueryLast)
			{
				Merkle::Hash hv;
				d.get_Phase2ID(hv, m_State.m_Close.m_Initiator);
				ConfirmKernel(hv, hTip); // ask again for confirmation
			}
		}

		return;
	}

	if (hTip > m_State.m_hQueryLast)
	{
		// Start the std locator logic
		if (m_pRequest)
		{
			if (proto::FlyClient::Request::Type::Utxo == m_pRequest->get_Type())
				return;
			CancelRequest();
		}

		MuSigLocator::Ptr pReq(new MuSigLocator);
		pReq->m_iIndex = pReq->m_nTotal = m_vUpdates.size();
		pReq->m_Initiator = false;
		pReq->m_Msg.m_Utxo = m_pOpen->m_Comm0;

		get_Net().PostRequest(*pReq, m_RequestHandler);
		m_pRequest = std::move(pReq);
	}
	else
	{
		if (!m_pRequest)
		{
			// Are we already in the channel-terminate phase?
			if (m_State.m_Terminate)
			{
				// Select the termination path. Note: it may not be the most recent m_vUpdates, because it may (theoretically) be not ready yet.
				// Go from back to front, until we encounter a valid path
				assert(!m_vUpdates.empty());
				size_t iPath = m_vUpdates.size() - 1;

				for (; ; iPath--)
				{
					const DataUpdate& d = *m_vUpdates[iPath];
					if (!d.m_tx1.m_vKernels.empty() && !d.m_tx2.m_vKernels.empty())
						break; // good enough

					// we must never reveal our key before we obtain the full withdrawal path
					assert(!d.m_RevealedSelfKey);
				}

				SendTxNoSpam(m_vUpdates[iPath]->m_tx1, hTip);
			}
		}
	}
}


void Channel::SwapIfRole(IBase& x, Key::IDV*& pA, Key::IDV*& pB)
{
	uint32_t iRole = 0;
	x.Get(iRole, Codes::Role);

	if (iRole)
		std::swap(pA, pB);
}

void Channel::AddUpdatePoint()
{
	m_vUpdates.push_back(std::unique_ptr<DataUpdate>(new DataUpdate));
}

void Channel::OnRolledBackH(Height h, Height& hVar)
{
	if (hVar > h)
		hVar = 0;
}

void Channel::CancelRequest()
{
	if (m_pRequest)
	{
		m_pRequest->m_pTrg = nullptr;
		m_pRequest.reset();
	}
}

void Channel::CancelTx()
{
	if (m_pPendingTx)
	{
		m_pPendingTx->m_pTrg = nullptr;
		m_pPendingTx.reset();
	}
}

Channel::~Channel()
{
	CancelRequest();
	CancelTx();
}

void Channel::OnRolledBack()
{
	Height h = get_Tip();
	if (m_pOpen)
	{
		OnRolledBackH(h, m_pOpen->m_hOpened);
		OnRolledBackH(h, m_State.m_hQueryLast);
		OnRolledBackH(h, m_State.m_hTxSentLast);
	}

	OnRolledBackH(h, m_State.m_Close.m_hPhase1);
	OnRolledBackH(h, m_State.m_Close.m_hPhase2);

	CancelRequest();
}

void Channel::RequestHandler::OnComplete(proto::FlyClient::Request& x)
{
	assert(get_ParentObj().m_pOpen);
	get_ParentObj().m_pRequest.reset();

	switch (x.get_Type())
	{
	case proto::FlyClient::Request::Type::Utxo:
		get_ParentObj().OnRequestComplete(Cast::Up<MuSigLocator>(x));
		break;

	case proto::FlyClient::Request::Type::Kernel:
		get_ParentObj().OnRequestComplete(Cast::Up<proto::FlyClient::RequestKernel>(x));
		break;

	case proto::FlyClient::Request::Type::Transaction:
		get_ParentObj().OnRequestComplete(Cast::Up<proto::FlyClient::RequestTransaction>(x));
		break;

	default: // suppress warning
		break;
	}
}

void Channel::OnRequestComplete(MuSigLocator& r)
{
	assert(!m_State.m_Close.m_hPhase1);
	assert(!m_State.m_Close.m_hPhase2);

	if (r.m_Res.m_Proofs.empty())
	{
		// continue search
		if (r.m_Initiator)
		{
			assert(r.m_iIndex < m_vUpdates.size());

			r.m_Initiator = false;
			r.m_Msg.m_Utxo = m_vUpdates[r.m_iIndex]->m_CommPeer1;
		}
		else
		{
			if (!r.m_iIndex)
			{
				// not found! (it's a problem)
				m_State.m_hQueryLast = get_Tip();
				Update();
				return;
			}

			r.m_iIndex--;
			r.m_Initiator = true;
			r.m_Msg.m_Utxo = m_vUpdates[r.m_iIndex]->m_Comm1;
		}

		r.m_pTrg = nullptr;
		get_Net().PostRequest(r, m_RequestHandler);
		assert(!m_pRequest);
		m_pRequest = &r;
	}
	else
	{
		// bingo!
		if (r.m_iIndex == r.m_nTotal)
		{
			m_State.m_hQueryLast = get_Tip();
			ZeroObject(m_State.m_Close); // msig0 is still intact
		}
		else
		{
			// withdrawal detected
			m_State.m_hQueryLast = 0;
			m_State.m_hTxSentLast = 0;

			m_State.m_Close.m_iPath = r.m_iIndex;
			m_State.m_Close.m_Initiator = r.m_Initiator;
			m_State.m_Close.m_hPhase1 = r.m_Res.m_Proofs.front().m_State.m_Maturity;
			m_State.m_Close.m_hPhase2 = 0;

			m_State.m_Terminate = true; // either we're closing it, or peer attempted
			m_pNegCtx.reset(); // nore more negotiations!

			if (IsUnfairPeerClosed())
			{
				DataUpdate& d = *m_vUpdates[m_State.m_Close.m_iPath];
				if (!d.IsPhase2UnfairPeerPunish())
					// punish it! Create an immediate withdraw tx, put it instead of m_txPeer2
					CreatePunishmentTx();
			}
		}

		Update();
	}
}

void Channel::CreatePunishmentTx()
{
	assert(m_State.m_Terminate && !m_State.m_Close.m_Initiator && (m_State.m_Close.m_iPath + 1 < m_vUpdates.size()));

	DataUpdate& d = *m_vUpdates[m_State.m_Close.m_iPath];
	assert(!d.IsPhase2UnfairPeerPunish()); // not yet

	DataUpdate& d1 = *m_vUpdates[m_State.m_Close.m_iPath + 1];
	assert(d1.m_PeerKeyValid);

	// Create an immediate withdraw tx, put it instead of m_txPeer2
	Key::IKdf::Ptr pKdf;
	get_Kdf(pKdf);

	Transaction& tx = d.m_txPeer2;

	assert(tx.m_vInputs.size() == 1); // msig0

	ECC::Scalar::Native offs, k;
	pKdf->DeriveKey(offs, d.m_msPeer); // our part
	offs += ECC::Scalar::Native(d1.m_PeerKey); // peer part

	Key::IDV kidvOut;
	kidvOut.m_Value = d.m_msPeer.m_Value - m_Params.m_Fee;
	kidvOut.m_Type = Key::Type::Regular;
	AllocTxoID(kidvOut);

	tx.m_vOutputs.resize(1);
	Output::Ptr& pOutp = tx.m_vOutputs.back();
	pOutp.reset(new Output);

	pOutp->Create(Rules::get().pForks[1].m_Height, k, *pKdf, kidvOut, *pKdf);

	k = -k;
	offs += k;

	tx.m_vKernels.resize(1);
	TxKernel::Ptr& pKrn = tx.m_vKernels.back();
	pKrn.reset(new TxKernel);

//	pKrn->m_Height.m_Min = m_State.m_Close.m_hPhase1; // not mandatory
	pKrn->m_Fee = m_Params.m_Fee;

	// extract (pseudo)random part into kernel. No need for extra-secure nonce generation
	ECC::Oracle() << offs >> k;
	offs += k;
	k = -k;

	pKrn->m_Commitment = ECC::Context::get().G * k;

	ECC::Hash::Value hv;
	pKrn->get_ID(hv);
	pKrn->m_Signature.Sign(hv, k);

	tx.m_Offset = offs;
/*
	{
		// test
		TxBase::Context::Params pars;
		TxBase::Context ctx(pars);
		ctx.m_Height.m_Min = m_State.m_Close.m_hPhase1;

		bool bb = tx.IsValid(ctx);
		bb = bb;
	}
*/
}

void Channel::OnRequestComplete(proto::FlyClient::RequestKernel& r)
{
	Height h = r.m_Res.m_Proof.m_State.m_Height;
	if (h)
	{
		m_State.m_hQueryLast = 0;
		m_State.m_hTxSentLast = 0;

		if (!m_pOpen->m_hOpened)
		{
			if (m_pOpen->m_hvKernel0 == r.m_Msg.m_ID)
				m_pOpen->m_hOpened = h;
		}

		if (m_State.m_Close.m_hPhase1)
		{
			const DataUpdate& d = *m_vUpdates[m_State.m_Close.m_iPath];
			Merkle::Hash hv;
			d.get_Phase2ID(hv, m_State.m_Close.m_Initiator);

			if (hv == r.m_Msg.m_ID)
				m_State.m_Close.m_hPhase2 = h;
		}

	}
	else
		m_State.m_hQueryLast = get_Tip();

	Update();
}

void Channel::OnRequestComplete(proto::FlyClient::RequestTransaction& r)
{
	m_pPendingTx.reset();

	if (proto::TxStatus::Ok == r.m_Res.m_Value)
	{
//		m_State.m_hTxSentLast = 0;
//		Update();
	}
	else
	{
		// oops
		bool bb = false;
		bb;
	}
}

void Channel::SendTx(const Transaction& tx)
{
	if (m_pPendingTx)
	{
		bool bCovers = false, bCovered = false;
		tx.get_Reader().Compare(m_pPendingTx->m_Msg.m_Transaction->get_Reader(), bCovers, bCovered);

		if (bCovers && bCovered)
			return; // same

		CancelTx();
	}

	m_pPendingTx.reset(new proto::FlyClient::RequestTransaction);

	// copy it
	m_pPendingTx->m_Msg.m_Transaction.reset(new Transaction);
	Transaction& txTrg = *m_pPendingTx->m_Msg.m_Transaction;
	txTrg.m_Offset = tx.m_Offset;
	TxVectors::Writer(txTrg, txTrg).Dump(tx.get_Reader());

	get_Net().PostRequest(*m_pPendingTx, m_RequestHandler);
}

void Channel::SendTxNoSpam(const Transaction& tx, Height h)
{
	assert(m_pOpen);
	if ((h - m_State.m_hTxSentLast >= 10) || !m_State.m_hTxSentLast)
	{
		m_State.m_hTxSentLast = h;
		SendTx(tx);
	}
}

void Channel::ConfirmKernel(const Merkle::Hash& hv, Height h)
{
	if (m_State.m_hQueryLast == h)
		return;

	if (m_pRequest)
	{
		if (proto::FlyClient::Request::Type::Kernel == m_pRequest->get_Type())
		{
			proto::FlyClient::RequestKernel& r = Cast::Up<proto::FlyClient::RequestKernel>(*m_pRequest);
			if (r.m_Msg.m_ID == hv)
				return; // already querying
		}

		CancelRequest();
	}

	proto::FlyClient::RequestKernel::Ptr pReq(new proto::FlyClient::RequestKernel);
	pReq->m_Msg.m_ID = hv;

	get_Net().PostRequest(*pReq, m_RequestHandler);
	m_pRequest = std::move(pReq);
}

const Transaction& Channel::DataUpdate::get_TxPhase2(bool bInitiator) const
{
	return bInitiator ? m_tx2 : m_txPeer2;
}

void Channel::DataUpdate::get_Phase2ID(Merkle::Hash& hv, bool bInitiator) const
{
	const Transaction& tx = get_TxPhase2(bInitiator);
	if (tx.m_vKernels.empty())
		hv = Zero; //?!
	else
		tx.m_vKernels.front()->get_ID(hv);
}

bool Channel::DataUpdate::IsPhase2UnfairPeerPunish() const
{
	return
		!m_txPeer2.m_vKernels.empty() &&
		!m_txPeer2.m_vKernels.front()->m_pRelativeLock;
}

bool Channel::Open(const std::vector<Key::IDV>& vIn, uint32_t iRole, Amount nMy, Amount nOther, const HeightRange& hr0)
{
	if (m_pOpen || m_pNegCtx)
		return false; // already in progress

	Amount nMyFee = m_Params.m_Fee / 2;
	if (iRole)
		nMyFee = m_Params.m_Fee - nMyFee; // in case it's odd

	Amount nChange = 0;
	for (size_t i = 0; i < vIn.size(); i++)
		nChange += vIn[i].m_Value;

	if (nChange < nMy + nMyFee * 3) // fee to create msig0, plus 2-stage withdrawal
		return false;
	nChange -= (nMy + nMyFee * 3);

	NegotiationCtx_Open* p = new NegotiationCtx_Open;
	m_pNegCtx.reset(p);
	m_pNegCtx->m_eType = NegotiationCtx::Open;

	p->m_Inst.m_pStorage = &p->m_Data;
	get_Kdf(p->m_Inst.m_pKdf);


	std::vector<ECC::Key::IDV> vChg, vOut;

	if (nChange)
	{
		Key::IDV& kidv = vChg.emplace_back();
		kidv.m_Value = nChange;
		kidv.m_Type = Key::Type::Change;
		AllocTxoID(kidv);
	}

	{
		Key::IDV& kidv = vOut.emplace_back();
		kidv.m_Value = nMy;
		kidv.m_Type = Key::Type::Regular;
		AllocTxoID(kidv);
	}

	ECC::Key::IDV ms0, msA, msB;
	ms0.m_Value = nMy + nOther + m_Params.m_Fee * 2;
	msA.m_Value = msB.m_Value = ms0.m_Value - m_Params.m_Fee;
	ms0.m_Type = msA.m_Type = msB.m_Type = FOURCC_FROM(MuSg);

	AllocTxoID(ms0);
	AllocTxoID(msA);
	AllocTxoID(msB);

	{
		ChannelOpen::Worker wrk(p->m_Inst);

		p->m_Inst.Set(iRole, Codes::Role);
		p->m_Inst.Set(Rules::get().pForks[1].m_Height, Codes::Scheme);

		MultiTx::KernelParam krn1;
		krn1.m_pFee = &m_Params.m_Fee;
		krn1.m_pH0 = &Cast::NotConst(hr0).m_Min;
		krn1.m_pH1 = &Cast::NotConst(hr0).m_Max;

		WithdrawTx::CommonParam cp;
		cp.m_Krn1.m_pFee = &m_Params.m_Fee;
		cp.m_Krn2.m_pFee = &m_Params.m_Fee;
		cp.m_Krn2.m_pLock = &m_Params.m_hLockTime;

		p->m_Inst.Setup(true, &Cast::NotConst(vIn), &vChg, &ms0, krn1, &msA, &msB, &vOut, cp);
	}

	return true;
}

bool Channel::Update(Amount nMyNew)
{
	if (m_pNegCtx || (State::Open != get_State()))
		return false;

	assert(m_pOpen && !m_State.m_Terminate && !m_vUpdates.empty());

	uint32_t iRole = !!m_pOpen->m_txOpen.m_vKernels.empty();

	Amount nMyFee = m_Params.m_Fee / 2;
	if (iRole)
		nMyFee = m_Params.m_Fee - nMyFee; // in case it's odd


	NegotiationCtx_Update* p = new NegotiationCtx_Update;
	m_pNegCtx.reset(p);
	m_pNegCtx->m_eType = NegotiationCtx::Update;

	p->m_Inst.m_pStorage = &p->m_Data;
	get_Kdf(p->m_Inst.m_pKdf);


	std::vector<ECC::Key::IDV> vOut;

	{
		Key::IDV& kidv = vOut.emplace_back();
		kidv.m_Value = nMyNew;
		kidv.m_Type = Key::Type::Regular;
		AllocTxoID(kidv);
	}

	ECC::Key::IDV msA, msB;
	msA.m_Value = msB.m_Value = m_pOpen->m_ms0.m_Value - m_Params.m_Fee;
	msA.m_Type = msB.m_Type = FOURCC_FROM(MuSg);

	AllocTxoID(msA);
	AllocTxoID(msB);

	DataUpdate& d0 = *m_vUpdates.back();

	{
		ChannelUpdate::Worker wrk(p->m_Inst);

		p->m_Inst.Set(iRole, Codes::Role);
		p->m_Inst.Set(Rules::get().pForks[1].m_Height, Codes::Scheme);

		WithdrawTx::CommonParam cp;
		cp.m_Krn1.m_pFee = &m_Params.m_Fee;
		cp.m_Krn2.m_pFee = &m_Params.m_Fee;
		cp.m_Krn2.m_pLock = &m_Params.m_hLockTime;

		p->m_Inst.Setup(true, &m_pOpen->m_ms0, &m_pOpen->m_Comm0, &msA, &msB, &Cast::NotConst(vOut), cp, &d0.m_msMy, &d0.m_msPeer, &d0.m_CommPeer1);
	}

	return true;
}

void Channel::Close()
{
	if (!m_State.m_Terminate)
	{
		m_State.m_Terminate = true;
		m_pNegCtx.reset();
		Update();
	}
}

} // namespace Lightning
} // namespace beam
