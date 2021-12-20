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

#include "processor.h"
#include "../utility/logger.h"
#include "../utility/logger_checkpoints.h"

namespace beam {

template <typename Archive, typename TPtr>
void save_VecPtr(Archive& ar, const std::vector<TPtr>& v)
{
	for (uint32_t i = 0; i < v.size(); i++)
		ar & *v[i];
}

void TxPool::Stats::SetSize(const Transaction& tx)
{
	m_Size = (uint32_t)tx.get_Reader().get_SizeNetto();
}

void TxPool::Stats::From(const Transaction& tx, const Transaction::Context& ctx, Amount feeReserve, uint32_t nSizeCorrection)
{
	assert(!AmountBig::get_Hi(ctx.m_Stats.m_Fee)); // ignore such txs atm
	m_Fee = AmountBig::get_Lo(ctx.m_Stats.m_Fee);
	m_FeeReserve = feeReserve;
	m_Hr = ctx.m_Height;

	SetSize(tx);
	m_SizeCorrection = nSizeCorrection;
}

bool TxPool::Profit::operator < (const Profit& t) const
{
	// handle overflow. To be precise need to use big-int (96-bit) arithmetics

	return
		(uintBigFrom(m_Stats.m_Fee) * uintBigFrom(t.m_Stats.m_Size + t.m_Stats.m_SizeCorrection)) >
		(uintBigFrom(t.m_Stats.m_Fee) * uintBigFrom(m_Stats.m_Size + m_Stats.m_SizeCorrection));
}

/////////////////////////////
// Fluff
TxPool::Fluff::Element* TxPool::Fluff::AddValidTx(Transaction::Ptr&& pValue, const Stats& stats, const Transaction::KeyType& key, State s, Height hLst /* = 0 */)
{
	assert(pValue);

	Element* p = new Element;
	p->m_pValue = std::move(pValue);
	p->m_Profit.m_Stats = stats;
	p->m_Tx.m_Key = key;
	p->m_Hist.m_Height = hLst;

	p->m_State = s;

	Features f0 = { false };
	SetState(*p, f0, Features::get(s));

	return p;
}

TxPool::Fluff::Features TxPool::Fluff::Features::get(State s)
{
	Features ret = { false };
	switch (s)
	{
	case State::Fluffed:
		ret.m_TxSet = true;
		ret.m_Send = true;
		break;

	case State::PreFluffed:
		ret.m_TxSet = true;
		ret.m_WaitFluff = true;
		break;

	case State::Outdated:
		ret.m_Outdated = true;
		break;

	default: // suppress warning
		break;
	}

	return ret;
}

void TxPool::Fluff::SetState(Element& x, State s)
{
	auto f0 = Features::get(x.m_State);
	auto f = Features::get(s);

	x.m_State = s;
	SetState(x, f0, f);
}

void TxPool::Fluff::SetState(Element& x, Features f0, Features f)
{
	if (f.m_Send != f0.m_Send)
	{
		if (f.m_Send)
		{
			assert(!x.m_pSend);

			x.m_pSend = new Element::Send;
			x.m_pSend->m_Refs = 1;
			x.m_pSend->m_pThis = &x;
			m_SendQueue.push_back(*x.m_pSend);
		}
		else
		{
			assert(x.m_pSend && (&x == x.m_pSend->m_pThis));

			x.m_pSend->m_pThis = nullptr;
			Release(*x.m_pSend);
			x.m_pSend = nullptr;
		}
	}

	if (f.m_TxSet != f0.m_TxSet)
	{
		if (f.m_TxSet)
			m_setProfit.insert(x.m_Profit);
		else
			m_setProfit.erase(ProfitSet::s_iterator_to(x.m_Profit));
	}

	SetStateHistOut(x, m_lstOutdated, f0.m_Outdated, f.m_Outdated);
	SetStateHistOut(x, m_lstWaitFluff, f0.m_WaitFluff, f.m_WaitFluff);
	SetStateHistIn(x, m_lstOutdated, f0.m_Outdated, f.m_Outdated);
	SetStateHistIn(x, m_lstWaitFluff, f0.m_WaitFluff, f.m_WaitFluff);
}

void TxPool::Fluff::SetStateHistIn(Element& x, HistList& lst, bool b0, bool b)
{
	if (b && !b0)
	{
		assert(lst.empty() || (lst.back().m_Height <= x.m_Hist.m_Height)); // order must be preserved
		lst.push_back(x.m_Hist);
	}
}

void TxPool::Fluff::SetStateHistOut(Element& x, HistList& lst, bool b0, bool b)
{
	if (b0 && !b)
		lst.erase(HistList::s_iterator_to(x.m_Hist));
}

void TxPool::Fluff::Delete(Element& x)
{
	auto f0 = Features::get(x.m_State);
	Features f = { false };
	SetState(x, f0, f);

	delete &x;
}

void TxPool::Fluff::Release(Element::Send& x)
{
	assert(x.m_Refs);
	if (!--x.m_Refs)
	{
		assert(!x.m_pThis);
		m_SendQueue.erase(SendQueue::s_iterator_to(x));
		delete &x;
	}
}

void TxPool::Fluff::Clear()
{
	while (!m_setProfit.empty())
		Delete(m_setProfit.begin()->get_ParentObj());

	while (!m_lstOutdated.empty())
		Delete(m_lstOutdated.begin()->get_ParentObj());
}

/////////////////////////////
// Stem
bool TxPool::Stem::TryMerge(Element& trg, Element& src)
{
	assert(trg.m_bAggregating && src.m_bAggregating);

	HeightRange hr = trg.m_Profit.m_Stats.m_Hr;
	hr.Intersect(src.m_Profit.m_Stats.m_Hr);
	if (hr.IsEmpty())
		return false;

	Transaction txNew;
	TxVectors::Writer wtx(txNew, txNew);

	volatile bool bStop = false;
	wtx.Combine(trg.m_pValue->get_Reader(), src.m_pValue->get_Reader(), bStop);

	txNew.m_Offset = ECC::Scalar::Native(trg.m_pValue->m_Offset) + ECC::Scalar::Native(src.m_pValue->m_Offset);

//#ifdef _DEBUG
//	Transaction::Context::Params pars;
//	Transaction::Context ctx(pars);
////	ctx.m_Height = ;
//	assert(txNew.IsValid(ctx));
//#endif // _DEBUG

	auto fees = trg.m_Profit.m_Stats.m_Fee;
	fees += src.m_Profit.m_Stats.m_Fee;
	Amount feeReserve = 0;
	if (!ValidateTxContext(txNew, hr, fees, feeReserve))
		return false; // conflicting txs, can't merge


	trg.m_Profit.m_Stats.m_Fee += src.m_Profit.m_Stats.m_Fee;
	trg.m_Profit.m_Stats.m_FeeReserve = feeReserve;
	trg.m_Profit.m_Stats.m_Hr = hr;
	trg.m_Profit.m_Stats.SetSize(txNew);
	trg.m_Profit.m_Stats.m_SizeCorrection += src.m_Profit.m_Stats.m_SizeCorrection;

	trg.m_pValue->m_vInputs.swap(txNew.m_vInputs);
	trg.m_pValue->m_vOutputs.swap(txNew.m_vOutputs);
	trg.m_pValue->m_vKernels.swap(txNew.m_vKernels);
	trg.m_pValue->m_Offset = txNew.m_Offset;

	Delete(src);
	DeleteKrn(trg);
	InsertKrn(trg);

	return true;
}

void TxPool::Stem::Delete(Element& x)
{
	uint32_t n_ms;
	bool bResetTimer = (x.m_Time.m_Value && (get_NextTimeout(n_ms) == &x));

	DeleteRaw(x);

	if (bResetTimer)
	{
		if (get_NextTimeout(n_ms))
			SetTimerRaw(n_ms);
		else
			KillTimer();
	}
}

void TxPool::Stem::DeleteRaw(Element& x)
{
	DeleteTimer(x);
	DeleteAggr(x);
	DeleteKrn(x);

	delete &x;
}

void TxPool::Stem::DeleteKrn(Element& x)
{
	for (size_t i = 0; i < x.m_vKrn.size(); i++)
		m_setKrns.erase(KrnSet::s_iterator_to(x.m_vKrn[i]));
	x.m_vKrn.clear();
}

void TxPool::Stem::InsertAggr(Element& x)
{
	if (!x.m_bAggregating)
	{
		x.m_bAggregating = true;
		m_setProfit.insert(x.m_Profit);
	}
}

void TxPool::Stem::DeleteAggr(Element& x)
{
	if (x.m_bAggregating)
	{
		m_setProfit.erase(ProfitSet::s_iterator_to(x.m_Profit));
		x.m_bAggregating = false;
	}
}

void TxPool::Stem::DeleteTimer(Element& x)
{
	if (x.m_Time.m_Value)
	{
		m_setTime.erase(TimeSet::s_iterator_to(x.m_Time));
		x.m_Time.m_Value = 0;
	}
}

void TxPool::Stem::InsertKrn(Element& x)
{
	const Transaction& tx = *x.m_pValue;
	x.m_vKrn.resize(tx.m_vKernels.size());

	for (size_t i = 0; i < x.m_vKrn.size(); i++)
	{
		Element::Kernel& n = x.m_vKrn[i];
		n.m_pKrn = tx.m_vKernels[i].get();
		m_setKrns.insert(n);
		n.m_pThis = &x;
	}
}

void TxPool::Stem::Clear()
{
	while (!m_setKrns.empty())
		DeleteRaw(*m_setKrns.begin()->m_pThis);

	KillTimer();
}

void TxPool::Stem::SetTimerRaw(uint32_t nTimeout_ms)
{
	if (!m_pTimer)
		m_pTimer = io::Timer::create(io::Reactor::get_Current());


	m_pTimer->start(nTimeout_ms, false, [this]() { OnTimer(); });
}

void TxPool::Stem::KillTimer()
{
	if (m_pTimer)
		m_pTimer->cancel();
}

void TxPool::Stem::SetTimer(uint32_t nTimeout_ms, Element& x)
{
	DeleteTimer(x);

	x.m_Time.m_Value = GetTime_ms() + nTimeout_ms;
	if (!x.m_Time.m_Value)
		x.m_Time.m_Value = 1;

	m_setTime.insert(x.m_Time);

	uint32_t nVal_ms;
	if (get_NextTimeout(nVal_ms) == &x)
		SetTimerRaw(nVal_ms);
}

void TxPool::Stem::OnTimer()
{
	while (true)
	{
		uint32_t nTimeout_ms;
		Element* pElem = get_NextTimeout(nTimeout_ms);
		if (!pElem)
		{
			KillTimer();
			break;
		}

		if (nTimeout_ms > 0)
		{
			SetTimerRaw(nTimeout_ms);
			break;
		}

		DeleteTimer(*pElem);
		OnTimedOut(*pElem);
	}
}

TxPool::Stem::Element* TxPool::Stem::get_NextTimeout(uint32_t& nTimeout_ms)
{
	if (m_setTime.empty())
		return NULL;

	uint32_t now_ms = GetTime_ms();
	Element::Time tmPrev;
	tmPrev.m_Value = now_ms - (uint32_t(-1) >> 1);

	TimeSet::iterator it = m_setTime.lower_bound(tmPrev);
	if (m_setTime.end() == it)
		it = m_setTime.begin();

	Element& ret = it->get_ParentObj();

	bool bLate = ((ret.m_Time.m_Value >= tmPrev.m_Value) == (now_ms >= ret.m_Time.m_Value)) == (now_ms > tmPrev.m_Value);

	nTimeout_ms = bLate ? 0 : (ret.m_Time.m_Value - now_ms);
	return &ret;
}

/////////////////////////////
// Dependent
TxPool::Dependent::Element* TxPool::Dependent::AddValidTx(Transaction::Ptr&& pValue, const Transaction::Context& ctx, const Transaction::KeyType& key, const Merkle::Hash& hvContext, Element* pParent)
{
	assert(pValue);

	Element* p = new Element;
	p->m_pValue = std::move(pValue);
	p->m_pParent = pParent;

	p->m_Context.m_Key = hvContext;
	m_setContexts.insert(p->m_Context);

	p->m_Tx.m_Key = key;
	m_setTxs.insert(p->m_Tx);

	p->m_Size = (uint32_t) p->m_pValue->get_Reader().get_SizeNetto();
	if (pParent)
		p->m_Size += pParent->m_Size;

	const Amount feeMax = static_cast<Amount>(-1);
	auto& fee = ctx.m_Stats.m_Fee;

	if (AmountBig::get_Hi(fee)) // unlikely, though possible
		p->m_Fee = feeMax;
	else
	{
		p->m_Fee = AmountBig::get_Lo(fee);
		if (pParent)
		{
			p->m_Fee += pParent->m_Fee;
			if (p->m_Fee < pParent->m_Fee)
				p->m_Fee = feeMax; // overflow
		}
	}

	if (ShouldUpdateBest(*p))
		m_pBest = p;

	return p;
}

bool TxPool::Dependent::ShouldUpdateBest(const Element& x)
{
	if (x.m_Size + 1024 > Rules::get().MaxBodySize) // This is rough, fix this later
		return false;

	if (static_cast<Amount>(-1) == x.m_Fee)
		return false;

	if (!m_pBest || (m_pBest->m_Fee < x.m_Fee))
		return true;

	if (m_pBest->m_Fee > x.m_Fee)
		return false;

	// fee is identical. Enforece order w.r.t. context to ensure consistent ordering across different nodes
	return x.m_Context.m_Key > m_pBest->m_Context.m_Key;
}

void TxPool::Dependent::Clear()
{
	m_pBest = nullptr;

	while (!m_setContexts.empty())
	{
		auto it = m_setContexts.begin();
		auto& x = it->get_ParentObj();

		m_setContexts.erase(it);
		m_setTxs.erase(TxSet::s_iterator_to(x.m_Tx));

		delete &x;
	}
}

} // namespace beam
