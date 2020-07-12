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
#include "lightning_codes.h"


namespace {
const beam::Height kPostLockReserveLag = 5;
}  // namespace
namespace beam {
namespace Lightning {

struct Channel::MuSigLocator
	:public proto::FlyClient::RequestUtxo
{
	typedef boost::intrusive_ptr<MuSigLocator> Ptr;

	virtual ~MuSigLocator() {}
};

struct Channel::KernelLocator
	:public proto::FlyClient::RequestKernel
{
	typedef boost::intrusive_ptr<KernelLocator> Ptr;

	DataUpdate* m_pIndex; // set to non-null if we're in the middle of a search
	uint32_t m_nRevision;
	bool m_Initiator;

	virtual ~KernelLocator() {}
};

Channel::State::Enum Channel::get_State() const
{
	if (!m_pOpen)
		return State::None;

	if (m_State.m_Close.m_hPhase2)
		return State::Closed;

	assert(!m_lstUpdates.empty());

	if (!m_pOpen->m_hOpened)
	{
		if (m_State.m_hQueryLast >= m_pOpen->m_hrLimit.m_Max)
			return State::OpenFailed;

		if (DataUpdate::Type::None == m_lstUpdates.front().m_Type)
			return State::Opening0;

		if (m_pNegCtx && (NegotiationCtx::Open == m_pNegCtx->m_eType))
			// still negotiating, however the barrier may already be crossed (if I don't have opening tx - then maybe other peer has).
			// More precisely - can check our Role. nevermind.
			return State::Opening1;

		return State::Opening2;
	}

	if (m_State.m_Close.m_hPhase1)
		return State::Closing2;

	if (m_State.m_Terminate || m_gracefulClose)
		return State::Closing1;

	auto& lastUpdate = m_lstUpdates.back();
	auto* hr = lastUpdate.get_HR();
	if (!hr)
		return State::Updating;

    if (get_Tip() >= hr->m_Max - m_Params.m_hPostLockReserve)
		return State::Expired;

	return m_pNegCtx ? State::Updating : State::Open;
}

Channel::DataUpdate* Channel::UpdateList::get_NextSafe(DataUpdate& d) const
{
	iterator it = s_iterator_to(d);
	return (++it == end()) ? nullptr : &(*it);
}

Channel::DataUpdate* Channel::UpdateList::get_PrevSafe(DataUpdate& d) const
{
	iterator it = s_iterator_to(d);
	if (begin() == it)
		return nullptr;
	return &(*--it);
}

void Channel::UpdateList::Clear()
{
	while (!empty())
		DeleteFront();
}

void Channel::UpdateList::DeleteFront()
{
	DataUpdate& d = front();
	pop_front();
	delete &d;
}

void Channel::UpdateList::DeleteBack()
{
	DataUpdate& d = back();
	pop_back();
	delete &d;
}

bool Channel::IsUnfairPeerClosed() const
{
	assert(m_State.m_Close.m_hPhase1);
	assert(m_State.m_Close.m_pPath);

	if (m_State.m_Close.m_Initiator)
		return false;

	DataUpdate* p = m_lstUpdates.get_NextSafe(*m_State.m_Close.m_pPath);
	return p && p->m_PeerKeyValid;
}

void Channel::DiscardLastRevision()
{
	if (m_nRevision && !m_lstUpdates.empty())
	{
		m_gracefulClose = false;
		m_lstUpdates.DeleteBack();
		--m_nRevision;
		m_pNegCtx.reset();
	}
}

void Channel::OnPeerData(Storage::Map& dataIn)
{
	if (m_State.m_Terminate)
		return; // no negotiations

	uint32_t nRev = 0;
	dataIn.Get(nRev, Codes::Revision);
	if (!nRev)
	{
		Close(); // by convention - it's a channel close request
		return;
	}

	if (nRev < m_nRevision)
		return;

	if (nRev > m_nRevision)
	{
		if (m_pNegCtx)
			return;

		if (nRev > m_nRevision + 1)
			return;

		// new negotiation?
		if (!m_nRevision)
		{
			// should be channel open request
			Amount nMy = 0, nPeer = 0;
			HeightRange hr0;

			bool bOk =
				dataIn.Get(m_Params.m_hLockTime, Codes::HLock) &&
				dataIn.Get(m_Params.m_hPostLockReserve, Codes::HPostLockReserve) &&
				dataIn.Get(m_Params.m_hRevisionMaxLifeTime, Codes::HLifeTime) &&
				dataIn.Get(m_Params.m_Fee, Codes::Fee) &&
				dataIn.Get(nPeer, Codes::ValueMy) &&
				dataIn.Get(nMy, Codes::ValueYours);

			if (!bOk) { // ompiler insists of parentheses
				return;
			}

			dataIn.Get(hr0.m_Min, Codes::H0);
			dataIn.Get(hr0.m_Max, Codes::H1);

			// TODO - ask permissions to open a channel with those conditions

			if (!OpenInternal(1, nMy, nPeer, hr0))
				return;

		}
		else
		{
			// value transfer?
			Amount val = 0;
			Height h = 0;
			if (!dataIn.Get(val, Codes::ValueTansfer) ||
				!dataIn.Get(h, Codes::H0))
				return;

			Amount valMy = m_lstUpdates.back().m_Outp.m_Value;
			val += valMy;

			if (val < valMy)
				return; // overflow attack!

			uint32_t iClose = 0;
			dataIn.Get(iClose, Codes::CloseGraceful);

			if (h + kMaxBlackoutTime <= get_Tip())
				return;

			if (!TransferInternal(val, 1, h, !!iClose))
				return;
		}
	}
	else if (nRev == m_nRevision && nRev > 1)
	{
		// double-sided graceful close
		uint32_t iClose = 0;
		if (dataIn.Get(iClose, Codes::CloseGraceful) && !!iClose)
		{
			Height h = 0;
			if (!dataIn.Get(h, Codes::H0))
				return;

			if (h + kMaxBlackoutTime <= get_Tip())
				return;

			if (m_pNegCtx && m_pNegCtx->m_eType == NegotiationCtx::Close)
			{
				DiscardLastRevision();
				if (!m_iRole)
					Transfer(0, true);

				return;
			}
		}
		else
		{
			Amount valueTransfer = 0;
			if (dataIn.Get(valueTransfer, Codes::ValueTansfer))
				return;
		}
	}
	else if(get_State() == State::Opening0 && !m_pNegCtx)
	{
		m_pOpen->m_hrLimit.m_Max = get_Tip();
		return;
	}

	if (!m_pNegCtx)
		return;

	Storage::Map dataOut;
	UpdateNegotiator(dataIn, dataOut);

	SendPeerInternal(dataOut);
}

void Channel::UpdateNegotiator(Storage::Map& dataIn, Storage::Map& dataOut)
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

	case NegotiationCtx::Close:
		pBase = &Cast::Up<NegotiationCtx_Close>(*m_pNegCtx).m_Inst;
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

			assert(m_pOpen && !m_pOpen->m_hOpened);

			ChannelOpen::Result res;
			x.get_Result(res);

			if (!(ChannelOpen::DoneParts::Main & nDone0))
			{
				m_pOpen->m_Comm0 = res.m_Comm0;
				m_pOpen->m_txOpen = std::move(res.m_txOpen);
				x.m_Tx0.Get(m_pOpen->m_hvKernel0, MultiTx::Codes::KernelID);
			}

			assert(m_nRevision == 1);

			DataUpdate& d = m_lstUpdates.back();
			Cast::Down<ChannelWithdrawal::Result>(d) = std::move(res);
			d.CheckStdType();
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

			assert(m_nRevision > 1);
			DataUpdate& d = m_lstUpdates.back();

			x.get_Result(d);
			d.CheckStdType();
		}
		break;


	case NegotiationCtx::Close:
		{
			MultiTx& x = Cast::Up<MultiTx>(*pBase);

			status = x.Update();

			if (Status::Success == status)
			{
				assert(m_nRevision > 1);
				DataUpdate& d = m_lstUpdates.back();

				x.Get(d.m_tx1, MultiTx::Codes::TxFinal);
				x.Get(d.m_PeerKey.m_Value, MultiTx::Codes::KernelID); // just use this variable

				d.m_Type = DataUpdate::Type::Direct;
			}
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

void Channel::ForgetOutdatedRevisions(Height hTip)
{
	// check if we can forget some oldest revisions. Don't do this while the request is in the progress (it may use it)
	while (m_lstUpdates.size() > 1)
	{

		DataUpdate& d = m_lstUpdates.front();
		assert(!m_State.m_Close.m_pPath); // no chance we're deleting the being-used revision

		const HeightRange* pHR = d.get_HR();
		if (!pHR)
			break; // not ready?

		Height h1 = pHR->m_Max + Rules::get().MaxRollback;
		if (h1 < pHR->m_Max)
			break; // overflow

		if (h1 >= hTip)
			break;

		// outdated!
		m_lstUpdates.DeleteFront();
		OnRevisionOutdated(m_nRevision - static_cast<uint32_t>(m_lstUpdates.size()));
	}
}

void Channel::Update()
{
	if (m_State.m_Close.m_hPhase2)
		return; // all finished

	if (!m_pOpen)
		return;
	assert(!m_lstUpdates.empty());

	Height hTip = get_Tip();

	if (!m_pOpen->m_hOpened)
	{
		if (DataUpdate::Type::None == m_lstUpdates.front().m_Type)
		{
			// early negotiation stage
			m_State.m_hQueryLast = hTip;
			return;
		}

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
		assert(m_State.m_Close.m_pPath);

		const DataUpdate& d = *m_State.m_Close.m_pPath;

		if ((DataUpdate::Type::Punishment == d.m_Type) || (m_State.m_Close.m_hPhase1 + m_Params.m_hLockTime <= hTip + 1))
		{
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

	const DataUpdate& d = m_lstUpdates.back();
	if (hTip > m_State.m_hQueryLast)
	{
		if (DataUpdate::Type::Direct == d.m_Type)
			ConfirmKernel(d.m_PeerKey.m_Value, hTip);
		else
			ConfirmMuSig();
	}

	// auto-close if the most recent revision is going to expire soon
	DataUpdate* pPath = SelectWithdrawalPath();
	if (!m_State.m_Terminate)
	{
		const HeightRange* pHR = pPath->get_HR();
		if (pHR && (hTip + (m_iRole ? kPostLockReserveLag : 0)) > (pHR->m_Max - m_Params.m_hPostLockReserve))
		{
			m_State.m_Terminate = true;
			m_pNegCtx.reset();
		}
	}


	if (m_State.m_Terminate)
	{
		if (pPath)
			SendTxNoSpam(pPath->m_tx1, hTip);
	}
	else
	{
		if ((DataUpdate::Type::Direct == d.m_Type) && !d.m_tx1.m_vKernels.empty())
			SendTxNoSpam(d.m_tx1, hTip);
	}
}

Channel::DataUpdate* Channel::SelectWithdrawalPath()
{
	// Select the termination path. Note: it may not be the most recent, because it may (theoretically) be not ready yet.
	// Go from back to front, until we encounter a valid path
	for (UpdateList::reverse_iterator it = m_lstUpdates.rbegin(); m_lstUpdates.rend() != it; it++)
	{
		DataUpdate& d = *it;
		if (DataUpdate::Type::None != d.m_Type)
			return &d; // good enough

		// we must never reveal our key before we obtain the full withdrawal path
		assert(!d.m_RevealedSelfKey);
	}

	return nullptr;
}

Channel::DataUpdate& Channel::CreateUpdatePoint(uint32_t iRole, const CoinID& msA, const CoinID& msB, const CoinID& outp)
{
	DataUpdate* pVal = new DataUpdate;
	DataUpdate& d = *pVal; // alias
	m_lstUpdates.push_back(d);
	m_nRevision++;

	const CoinID* pArr[] = { &msA, &msB };

	d.m_Type = DataUpdate::Type::None;
	d.m_msMy = *pArr[iRole];
	d.m_msPeer = *pArr[!iRole];
	d.m_Outp = outp;
	d.m_PeerKeyValid = false;
	d.m_RevealedSelfKey = false;

	return d;
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
	if (!m_pOpen)
		return;

	CancelRequest();
	Height h = get_Tip();

	if (m_State.m_Close.m_hPhase2)
	{
		if (h >= m_State.m_Close.m_hPhase2)
			return;

		OnCoin(m_State.m_Close.m_pPath->m_Outp, m_State.m_Close.m_hPhase2, CoinState::Confirmed, true); // remove confirmation

		m_State.m_Close.m_hPhase2 = 0;
		m_State.m_Close.m_pPath = nullptr;
		m_State.m_Close.m_nRevision = 0;
	}

	std::setmin(m_State.m_hQueryLast, h);

	if (m_State.m_hTxSentLast > h)
		m_State.m_hTxSentLast = 0; // could be lost

	if (m_State.m_Close.m_hPhase1)
	{
		if (h >= m_State.m_Close.m_hPhase1)
			return;
		m_State.m_Close.m_hPhase1 = 0;
	}

	if (m_pOpen->m_hOpened)
	{
		if (h >= m_pOpen->m_hOpened)
			return;
		m_pOpen->m_hOpened = 0;
		OnInpCoinsChanged();
	}
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
		get_ParentObj().OnRequestComplete(Cast::Up<KernelLocator>(x));
		break;

	case proto::FlyClient::Request::Type::Transaction:
		get_ParentObj().OnRequestComplete(Cast::Up<proto::FlyClient::RequestTransaction>(x));
		break;

	default: // suppress warning
		break;
	}
}

void Channel::OnRequestCompleteInSearch(KernelLocator& r)
{
	assert(!m_State.m_Close.m_hPhase1);
	assert(!m_State.m_Close.m_hPhase2);
	assert(r.m_pIndex);

	Height h = r.m_Res.m_Proof.m_State.m_Height;
	if (!h)
	{
		// continue search
		if (r.m_Initiator)
		{
			assert(r.m_pIndex);
			r.m_Initiator = false;
		}
		else
		{
			r.m_pIndex = m_lstUpdates.get_PrevSafe(*r.m_pIndex);
			r.m_nRevision--;

			if (!r.m_pIndex)
			{
				// not found! (it's a problem)
				m_State.m_hQueryLast = get_Tip();
				Update();
				return;
			}

			r.m_Initiator = true;
		}

		r.m_pIndex->get_Phase1ID(r.m_Msg.m_ID, r.m_Initiator);

		r.m_pTrg = nullptr;
		get_Net().PostRequest(r, m_RequestHandler);
		assert(!m_pRequest);
		m_pRequest = &r;
	}
	else
	{
		// withdrawal detected
		if (m_gracefulClose || get_State() == State::Updating)
			DiscardLastRevision();

		m_State.m_hQueryLast = 0;
		m_State.m_hTxSentLast = 0;

		m_State.m_Close.m_pPath = r.m_pIndex;
		m_State.m_Close.m_nRevision = r.m_nRevision;
		m_State.m_Close.m_Initiator = r.m_Initiator;
		m_State.m_Close.m_hPhase1 = h;

		m_State.m_Terminate = true; // either we're closing it, or peer attempted
		m_pNegCtx.reset(); // nore more negotiations!

		if (IsUnfairPeerClosed())
		{
			DataUpdate& d = *r.m_pIndex;
			if (DataUpdate::Type::TimeLocked == d.m_Type)
				// punish it! Create an immediate withdraw tx, put it instead of m_txPeer2
				CreatePunishmentTx();
		}

		Update();
	}
}


void Channel::OnRequestComplete(MuSigLocator& r)
{
	assert(!m_State.m_Close.m_hPhase1);
	assert(!m_State.m_Close.m_hPhase2);

	if (r.m_Res.m_Proofs.empty())
	{
		// MuSig disappeared. Search for appropriate withdrawal tx
		KernelLocator::Ptr pReq(new KernelLocator);
		KernelLocator& r2 = *pReq;
		r2.m_pIndex = &m_lstUpdates.back();
		r2.m_nRevision = m_nRevision;

		while (true)
		{
			if (!r2.m_pIndex)
			{
				// not found! (it's a problem)
				m_State.m_hQueryLast = get_Tip();
				Update();
				return;
			}

			const DataUpdate& d = *r2.m_pIndex;

			bool bIs2Phase = (DataUpdate::Type::TimeLocked == d.m_Type) || (DataUpdate::Type::Punishment == d.m_Type);
			if (bIs2Phase)
			{
				r2.m_Initiator = true;
				d.get_Phase1ID(r2.m_Msg.m_ID, r2.m_Initiator);
				break;
			}

			r2.m_pIndex = m_lstUpdates.get_PrevSafe(*r2.m_pIndex);
			r2.m_nRevision--;
		}

		get_Net().PostRequest(r2, m_RequestHandler);
		m_pRequest = std::move(pReq);
	}
	else
	{
		// bingo!
		m_State.m_hQueryLast = get_Tip();
		ZeroObject(m_State.m_Close); // msig0 is still intact

		ForgetOutdatedRevisions(m_State.m_hQueryLast);

		Update();
	}
}

void Channel::CreatePunishmentTx()
{
	assert(m_State.m_Terminate && !m_State.m_Close.m_Initiator && (m_State.m_Close.m_pPath));

	DataUpdate& d = *m_State.m_Close.m_pPath;
	assert(DataUpdate::Type::TimeLocked == d.m_Type);

	DataUpdate& d1 = *m_lstUpdates.get_NextSafe(d);
	assert(d1.m_PeerKeyValid);

	// Create an immediate withdraw tx, put it instead of m_txPeer2
	Key::IKdf::Ptr pKdf;
	get_Kdf(pKdf);

	Transaction& tx = d.m_txPeer2;

	assert(tx.m_vInputs.size() == 1); // msig0

	ECC::Hash::Value hv;
	d.m_msPeer.get_Hash(hv);

	ECC::Scalar::Native offs, k;
	pKdf->DeriveKey(offs, hv); // our part
	offs += ECC::Scalar::Native(d1.m_PeerKey); // peer part

	CoinID& cidOut = d.m_Outp;
	cidOut.m_Value = d.m_msPeer.m_Value - m_Params.m_Fee;
	cidOut.m_Type = Key::Type::Regular;
	AllocTxoID(cidOut);

	tx.m_vOutputs.resize(1);
	Output::Ptr& pOutp = tx.m_vOutputs.back();
	pOutp.reset(new Output);

	pOutp->Create(m_State.m_Close.m_hPhase1, k, *pKdf, cidOut, *pKdf);

	k = -k;
	offs += k;

	tx.m_vKernels.resize(1);
	TxKernel::Ptr& pKrn = tx.m_vKernels.back();
	pKrn.reset(new TxKernelStd);

	pKrn->m_Height.m_Min = m_State.m_Close.m_hPhase1; // mandatory
	pKrn->m_Fee = m_Params.m_Fee;

	// extract (pseudo)random part into kernel. No need for extra-secure nonce generation
	ECC::Oracle() << offs >> k;
	offs += k;
	k = -k;

	Cast::Up<TxKernelStd>(*pKrn).Sign(k);

	tx.m_Offset = offs;

	d.m_Type = DataUpdate::Type::Punishment;

	//{
	//	// test
	//	TxBase::Context::Params pars;
	//	TxBase::Context ctx(pars);
	//	ctx.m_Height.m_Min = m_State.m_Close.m_hPhase1;

	//	bool bb = tx.IsValid(ctx);
	//	bb = bb;
	//}

}

void Channel::OnRequestComplete(KernelLocator& r)
{
	if (r.m_pIndex)
	{
		OnRequestCompleteInSearch(r);
		return;
	}

	Height h = r.m_Res.m_Proof.m_State.m_Height;
	if (h)
	{
		m_State.m_hQueryLast = 0;
		m_State.m_hTxSentLast = 0;

		if (!m_pOpen->m_hOpened)
		{
			if (m_pOpen->m_hvKernel0 == r.m_Msg.m_ID)
			{
				m_pOpen->m_hOpened = h;
				OnInpCoinsChanged();

				if (m_pNegCtx)
				{
					// non-typical situation, the peer maybe malicious (doesn't complete the part that it needs, yet allows the opening).
					assert(NegotiationCtx::Open == m_pNegCtx->m_eType);
					m_pNegCtx.reset();
				}
			}
		}

		if (m_State.m_Close.m_hPhase1 && !m_State.m_Close.m_hPhase2)
		{
			const DataUpdate& d = *m_State.m_Close.m_pPath;
			Merkle::Hash hv;
			d.get_Phase2ID(hv, m_State.m_Close.m_Initiator);

			if (hv == r.m_Msg.m_ID)
			{
				m_State.m_Close.m_hPhase2 = h;
				OnCoin(d.m_Outp, h, CoinState::Confirmed, false); // confirmed
			}
		}

		DataUpdate& d = m_lstUpdates.back();
		if (!m_State.m_Close.m_hPhase1 && !m_State.m_Close.m_hPhase2 && (DataUpdate::Type::Direct == d.m_Type) && (r.m_Msg.m_ID == d.m_PeerKey.m_Value))
		{
			// graceful closure detected
			m_State.m_Close.m_hPhase2 = h;

			m_State.m_hQueryLast = 0;
			m_State.m_hTxSentLast = 0;

			m_State.m_Close.m_pPath = &d;
			m_State.m_Close.m_nRevision = m_nRevision;
			m_State.m_Close.m_Initiator = true; // doesn't matter

			OnCoin(d.m_Outp, h, CoinState::Confirmed, false);
		}

	}
	else
	{
		const DataUpdate& d = m_lstUpdates.back();
		if (DataUpdate::Type::Direct == d.m_Type)
			ConfirmMuSig(); // graceful close not confirmed, meanwhile re-check musig
		else
			m_State.m_hQueryLast = get_Tip();
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

	if (m_pRequest)
	{
		if (proto::FlyClient::Request::Type::Kernel == m_pRequest->get_Type())
		{
			KernelLocator& r = Cast::Up<KernelLocator>(*m_pRequest);
			if (r.m_Msg.m_ID == hv)
				return; // already querying
		}

		CancelRequest();
	}

	KernelLocator::Ptr pReq(new KernelLocator);
	pReq->m_Msg.m_ID = hv;
	pReq->m_pIndex = nullptr;

	get_Net().PostRequest(*pReq, m_RequestHandler);
	m_pRequest = std::move(pReq);
}

void Channel::ConfirmMuSig()
{
	// Start the std locator logic
	if (m_pRequest)
	{
		// don't interrupt current musig search
		switch (m_pRequest->get_Type())
		{
		case proto::FlyClient::Request::Type::Utxo:
			return;

		case proto::FlyClient::Request::Type::Kernel:
			if (Cast::Up<KernelLocator>(*m_pRequest).m_pIndex)
				return;

			// no break;

		default: // suppress warning
			break;
		}

		CancelRequest();
	}
	else
	{
		if (get_Tip() == m_State.m_hQueryLast)
			return; // as well
	}

	MuSigLocator::Ptr pReq(new MuSigLocator);
	pReq->m_Msg.m_Utxo = m_pOpen->m_Comm0;

	get_Net().PostRequest(*pReq, m_RequestHandler);
	m_pRequest = std::move(pReq);
}

const Transaction& Channel::DataUpdate::get_TxPhase2(bool bInitiator) const
{
	return bInitiator ? m_tx2 : m_txPeer2;
}

bool Channel::DataUpdate::get_KernelIDSafe(Merkle::Hash& hv, const Transaction& tx)
{
	if (tx.m_vKernels.empty())
	{
		hv = Zero;
		return false;
	}

	hv = tx.m_vKernels.front()->m_Internal.m_ID;
	return true;
}

void Channel::DataUpdate::get_Phase2ID(Merkle::Hash& hv, bool bInitiator) const
{
	get_KernelIDSafe(hv, get_TxPhase2(bInitiator));
}

void Channel::DataUpdate::get_Phase1ID(Merkle::Hash& hv, bool bInitiator) const
{
	if (bInitiator)
		get_KernelIDSafe(hv, m_tx1);
	else
		hv = m_hvTx1KernelID;
}

void Channel::DataUpdate::CheckStdType()
{
	if (Type::None == m_Type)
	{
		if (!(m_tx1.m_vKernels.empty() || m_tx2.m_vKernels.empty()))
			m_Type = DataUpdate::Type::TimeLocked;
	}
}

bool Channel::Open(Amount nMy, Amount nOther, Height hOpenTxDh)
{
	HeightRange hr0;
	hr0.m_Min = get_Tip();
	hr0.m_Max = hr0.m_Min + hOpenTxDh;

	if (!OpenInternal(0, nMy, nOther, hr0))
		return false;

	Storage::Map dataIn, dataOut;

	dataOut.Set(m_Params.m_hLockTime, Codes::HLock);
	dataOut.Set(m_Params.m_hPostLockReserve, Codes::HPostLockReserve);
	dataOut.Set(m_Params.m_hRevisionMaxLifeTime, Codes::HLifeTime);
	dataOut.Set(m_Params.m_Fee, Codes::Fee);
	dataOut.Set(nMy, Codes::ValueMy);
	dataOut.Set(nOther, Codes::ValueYours);
	dataOut.Set(hr0.m_Min, Codes::H0);
	dataOut.Set(hr0.m_Max, Codes::H1);

	UpdateNegotiator(dataIn, dataOut);

	SendPeerInternal(dataOut);

	return true;
}

bool Channel::Transfer(Amount val, bool bCloseGraceful)
{
	if (!m_pOpen)
		return false;

	Amount valMy = m_lstUpdates.back().m_Outp.m_Value;

	if (valMy < val)
		return false;

	Height h = get_Tip();
	if (!TransferInternal(valMy - val, 0, h, bCloseGraceful))
		return false;

	Storage::Map dataIn, dataOut;
	dataOut.Set(val, Codes::ValueTansfer);
	dataOut.Set(h, Codes::H0);

	if (bCloseGraceful)
		dataOut.Set(uint32_t(1), Codes::CloseGraceful);

	UpdateNegotiator(dataIn, dataOut);
	SendPeerInternal(dataOut);

	return true;
}

bool Channel::OpenInternal(uint32_t iRole, Amount nMy, Amount nOther, const HeightRange& hr0)
{
	if (m_pOpen || m_pNegCtx)
		return false; // already in progress

	// check params sanity
	// hr0 lifetime - should be order of standard tx negotiation (several hours)
	// m_hRevisionMaxLifeTime - can be long, but no longer than max kernel validity time (Rules::)
	if (hr0.IsEmpty())
		return false;
	if (hr0.m_Max - hr0.m_Min >= m_Params.m_hRevisionMaxLifeTime)
		return false; // typically it should be much smaller

	Height hMaxLifeTimeWithLock = m_Params.m_hRevisionMaxLifeTime + m_Params.m_hLockTime; // this is the validity lifetime of the 2nd-stage withdrawal tx
	if (hMaxLifeTimeWithLock < m_Params.m_hLockTime)
		return false; // overflow

	hMaxLifeTimeWithLock += m_Params.m_hPostLockReserve;
	if (hMaxLifeTimeWithLock < m_Params.m_hPostLockReserve)
		return false; // overflow

	if (hMaxLifeTimeWithLock > Rules::get().MaxKernelValidityDH)
		return false; // too large

	Amount nMyFee = m_Params.m_Fee / 2;
	if (iRole)
		nMyFee = m_Params.m_Fee - nMyFee; // in case it's odd

	std::vector<CoinID> vIn;
	Amount nRequired = nMy + nMyFee * 3; // fee to create msig0, plus 2-stage withdrawal

	Amount nChange = SelectInputs(vIn, nRequired, 0);
	if (nChange < nRequired)
		return false;
	nChange -= (nMy + nMyFee * 3);

	m_iRole = iRole;
	NegotiationCtx_Open* p = new NegotiationCtx_Open;
	m_pNegCtx.reset(p);
	m_pNegCtx->m_eType = NegotiationCtx::Open;

	p->m_Inst.m_pStorage = &p->m_Data;
	get_Kdf(p->m_Inst.m_pKdf);


	std::vector<CoinID> vChg, vOut;

	if (nChange)
	{
		CoinID& cid = vChg.emplace_back();
		cid.m_Value = nChange;
		cid.m_Type = Key::Type::Change;
		AllocTxoID(cid);
	}

	{
		CoinID& cid = vOut.emplace_back();
		cid.m_Value = nMy;
		cid.m_Type = Key::Type::Regular;
		AllocTxoID(cid);
	}

	CoinID ms0, msA, msB;
	ms0.m_Value = nMy + nOther + m_Params.m_Fee * 2;
	msA.m_Value = msB.m_Value = ms0.m_Value - m_Params.m_Fee;
	ms0.m_Type = msA.m_Type = msB.m_Type = FOURCC_FROM(MuSg);

	AllocTxoID(ms0);
	AllocTxoID(msA);
	AllocTxoID(msB);

	{
		ChannelOpen::Worker wrk(p->m_Inst);

		p->m_Inst.Set(iRole, Codes::Role);
		p->m_Inst.Set(hr0.m_Min, Codes::Scheme);


		MultiTx::KernelParam krn0;
		krn0.m_pFee = &m_Params.m_Fee;
		krn0.m_pH0 = &Cast::NotConst(hr0).m_Min;
		krn0.m_pH1 = &Cast::NotConst(hr0).m_Max;

		Height h1, h2;
		WithdrawTx::CommonParam cp;
		SetWithdrawParams(cp, hr0.m_Min, h1, h2);

		p->m_Inst.Setup(true, &Cast::NotConst(vIn), &vChg, &ms0, krn0, &msA, &msB, &vOut, cp);
	}


	m_pOpen.reset(new DataOpen);
	ZeroObject(m_pOpen->m_Comm0);
	m_pOpen->m_ms0 = ms0;
	m_pOpen->m_hrLimit = hr0;
	ZeroObject(m_pOpen->m_hvKernel0);
	m_pOpen->m_hOpened = 0;

	OnCoin(vIn, 0, CoinState::Locked, false);
	m_pOpen->m_vInp = std::move(vIn);

	if (vChg.empty())
		ZeroObject(m_pOpen->m_cidChange);
	else
		m_pOpen->m_cidChange = vChg.front();

	CreateUpdatePoint(iRole, msA, msB, vOut.front());

	return true;
}

void Channel::SetWithdrawParams(WithdrawTx::CommonParam& cp, const Height& h, Height& h1, Height& h2) const
{
	h1 = h + m_Params.m_hRevisionMaxLifeTime;
	h2 = h1 + m_Params.m_hLockTime + m_Params.m_hPostLockReserve;

	cp.m_Krn1.m_pFee = &Cast::NotConst(m_Params).m_Fee;
	cp.m_Krn1.m_pH0 = &Cast::NotConst(h);
	cp.m_Krn1.m_pH1 = &h1;
	cp.m_Krn2.m_pFee = &Cast::NotConst(m_Params).m_Fee;
	cp.m_Krn2.m_pH0 = &Cast::NotConst(h);
	cp.m_Krn2.m_pH1 = &h2;
	cp.m_Krn2.m_pLock = &Cast::NotConst(m_Params.m_hLockTime);
}


bool Channel::TransferInternal(Amount nMyNew, uint32_t iRole, Height h, bool bCloseGraceful)
{
	if (m_pNegCtx || (State::Open != get_State()))
		return false;

	assert(m_pOpen && !m_State.m_Terminate && !m_lstUpdates.empty());

	DataUpdate& d0 = m_lstUpdates.back();
	if (DataUpdate::Type::Direct == d0.m_Type)
		return false; // no negotiations past decision to (gracefully) close the channel

	if (h < d0.get_H0())
		return false; // attempt to decrease the height

	Amount nMyFee = m_Params.m_Fee / 2;
	if (iRole)
		nMyFee = m_Params.m_Fee - nMyFee; // in case it's odd

	std::vector<CoinID> vOut;

	CoinID& cid = vOut.emplace_back();
	cid.m_Value = nMyNew;
	cid.m_Type = Key::Type::Regular;

	if (bCloseGraceful)
	{
		NegotiationCtx_Close* p = new NegotiationCtx_Close;
		m_pNegCtx.reset(p);
		m_pNegCtx->m_eType = NegotiationCtx::Close;

		MultiTx& n = p->m_Inst;
		n.m_pStorage = &p->m_Data;
		get_Kdf(n.m_pKdf); 

		cid.m_Value += nMyFee;
		AllocTxoID(cid);

		n.Set(iRole, Codes::Role);
		n.Set(h, Codes::Scheme);


		n.Set(m_pOpen->m_ms0, MultiTx::Codes::InpMsCid);
		n.Set(m_pOpen->m_Comm0, MultiTx::Codes::InpMsCommitment);
		n.Set(vOut, MultiTx::Codes::OutpCids);
		n.Set(m_Params.m_Fee, MultiTx::Codes::KrnFee);
		n.Set(h, MultiTx::Codes::KrnH0);
		n.Set(h + m_Params.m_hRevisionMaxLifeTime, MultiTx::Codes::KrnH1);

		CoinID msDummy(Zero);
		msDummy.m_Value = m_pOpen->m_ms0.m_Value - m_Params.m_Fee;
		CreateUpdatePoint(iRole, msDummy, msDummy, vOut.front());

		m_gracefulClose = true;
	}
	else
	{
		NegotiationCtx_Update* p = new NegotiationCtx_Update;
		m_pNegCtx.reset(p);
		m_pNegCtx->m_eType = NegotiationCtx::Update;

		p->m_Inst.m_pStorage = &p->m_Data;
		get_Kdf(p->m_Inst.m_pKdf);

		AllocTxoID(cid);

		CoinID msA, msB;
		msA.m_Value = msB.m_Value = m_pOpen->m_ms0.m_Value - m_Params.m_Fee;
		msA.m_Type = msB.m_Type = FOURCC_FROM(MuSg);

		AllocTxoID(msA);
		AllocTxoID(msB);

		{
			ChannelUpdate::Worker wrk(p->m_Inst);

			p->m_Inst.Set(iRole, Codes::Role);
			p->m_Inst.Set(h, Codes::Scheme);

			Height h1, h2;
			WithdrawTx::CommonParam cp;
			SetWithdrawParams(cp, h, h1, h2);

			p->m_Inst.Setup(true, &m_pOpen->m_ms0, &m_pOpen->m_Comm0, &msA, &msB, &Cast::NotConst(vOut), cp, &d0.m_msMy, &d0.m_msPeer, &d0.m_CommPeer1);
		}

		CreateUpdatePoint(iRole, msA, msB, vOut.front());
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

bool Channel::IsSafeToForget() const
{
	if (!m_pOpen)
		return true;

	Height hMaxRollback = Rules::get().MaxRollback;

	if (!m_pOpen->m_hOpened)
	{
		return
			(m_State.m_hQueryLast >= m_pOpen->m_hrLimit.m_Max + hMaxRollback) || // too late
			(DataUpdate::Type::None == m_lstUpdates.front().m_Type); // negotiation is at a very early stage
	}

	return m_State.m_Close.m_hPhase2 && (m_State.m_Close.m_hPhase2 + hMaxRollback <= get_Tip());
}

void Channel::Forget()
{
	if (m_pOpen)
	{
		if (!m_pOpen->m_hOpened)
			OnCoin(m_pOpen->m_vInp, 0, CoinState::Locked, true);

		m_pOpen.reset();
	}

	CancelRequest();
	CancelTx();

	m_pNegCtx.reset();
	m_lstUpdates.Clear();
	
	ZeroObject(m_State);
}

void Channel::OnCoin(const std::vector<CoinID>& v, Height h, CoinState e, bool bReverse)
{
	for (size_t i = 0; i < v.size(); i++)
		OnCoin(v[i], h, e, bReverse);
}

void Channel::OnInpCoinsChanged()
{
	assert(m_pOpen);

	OnCoin(m_pOpen->m_vInp, m_pOpen->m_hOpened, CoinState::Spent, !m_pOpen->m_hOpened);

	if (m_pOpen->m_cidChange.m_Value)
		OnCoin(m_pOpen->m_cidChange, m_pOpen->m_hOpened, CoinState::Confirmed, !m_pOpen->m_hOpened);
}

void Channel::SendPeerInternal(Storage::Map& dataOut)
{
	if (!dataOut.empty())
	{
		dataOut.Set(m_nRevision, Codes::Revision);
		SendPeer(std::move(dataOut));
	}
}

} // namespace Lightning
} // namespace beam
