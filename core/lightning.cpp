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
	bool m_Initiator;
};

Channel::State::Enum Channel::get_State() const
{
	if (!m_pOpen)
		return m_pNegCtx ? State::Opening1 : State::None;

	if (!m_pOpen->m_hOpened)
		return (m_State.m_hQueryLast <= m_pOpen->m_hrLimit.m_Max) ? State::Opening2 : State::OpenFailed;

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

void Channel::UpdateNegotiation(Storage::Map& dataInOut)
{
	Storage::Map dataIn;
	dataIn.swap(dataInOut);

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

	Gateway::Direct gw(dataInOut);

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
				m_pOpen->m_hrLimit; // ?!

				x.m_Tx0.Get(m_pOpen->m_hvKernel0, MultiTx::Codes::KernelID);

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

			x.Setup(false, nullptr, nullptr, &m_pOpen->m_ms0, pA, pB, nullptr, nullptr);
		}
		break;

	case NegotiationCtx::Update:
		{
			ChannelUpdate& x = Cast::Up<ChannelUpdate>(*pBase);
			ChannelUpdate::Worker wrk(x);

			uint8_t nDone0 = x.get_DoneParts();
			status = x.Update();
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

			x.Setup(false, nullptr, nullptr, pA, pB, nullptr, nullptr, nullptr, nullptr, nullptr);
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

	if (m_State.m_Close.m_hPhase2)
		return; // all finished

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

		m_State.m_hQueryLast = hTip;

		MuSigLocator::Ptr pReq(new MuSigLocator);
		pReq->m_iIndex = m_vUpdates.size();
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
				assert(!m_vUpdates.empty());
				const DataUpdate& d = *m_vUpdates.back();

				SendTxNoSpam(d.m_tx1, hTip);
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
		if (r.m_iIndex == m_vUpdates.size())
		{
			ZeroObject(m_State.m_Close); // msig0 is still intact
		}
		else
		{
			// withdrawal detected
			State::Close c;
			c.m_iPath = r.m_iIndex;
			c.m_Initiator = r.m_Initiator;
			c.m_hPhase1 = r.m_Res.m_Proofs.front().m_State.m_Maturity;
			c.m_hPhase2 = 0;

			if (memcmp(&c, &m_State.m_Close, sizeof(c)))
			{
				m_State.m_Close = c;

				m_State.m_Terminate = true; // either we're closing it, or peer attempted
				m_pNegCtx.reset(); // nore more negotiations!

				m_State.m_hQueryLast = 0;
				m_State.m_hTxSentLast = 0;

				if (IsUnfairPeerClosed())
				{
					// punish it! Create an immediate withdraw tx, put it instead of m_txPeer2
					// TODO!
				}
			}
		}

		Update();
	}
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
	m_State.m_hQueryLast = h;

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


} // namespace Lightning
} // namespace beam
