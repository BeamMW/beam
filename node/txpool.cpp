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

void TxPool::Profit::SetSize(const Transaction& tx, uint32_t nCorrection)
{
	m_nSize = (uint32_t) tx.get_Reader().get_SizeNetto();
	m_nSizeCorrected = m_nSize + nCorrection;
}

uint32_t TxPool::Profit::get_Correction() const
{
	uint32_t ret;
	m_nSizeCorrected.Export(ret);
	return ret - m_nSize;
}

bool TxPool::Profit::operator < (const Profit& t) const
{
	// handle overflow. To be precise need to use big-int (96-bit) arithmetics
	//	return m_Fee * t.m_nSize > t.m_Fee * m_nSize;

	return
		(m_Fee * t.m_nSizeCorrected) >
		(t.m_Fee * m_nSizeCorrected);
}

/////////////////////////////
// Fluff
TxPool::Fluff::Element* TxPool::Fluff::AddValidTx(Transaction::Ptr&& pValue, const Transaction::Context& ctx, const Transaction::KeyType& key, uint32_t nSizeCorrection)
{
	assert(pValue);

	Element* p = new Element;
	p->m_pValue = std::move(pValue);
	p->m_Height	= ctx.m_Height;
	p->m_Profit.m_Fee = ctx.m_Stats.m_Fee;
	p->m_Profit.SetSize(*p->m_pValue, nSizeCorrection);
	p->m_Tx.m_Key = key;
	p->m_Outdated.m_Height = MaxHeight;
	assert(!p->IsOutdated());

	InternalInsert(*p);

	p->m_Queue.m_Refs = 1;
	m_Queue.push_back(p->m_Queue);

	return p;
}

void TxPool::Fluff::SetOutdated(Element& x, Height h)
{
	InternalErase(x);
	x.m_Outdated.m_Height = h;
	InternalInsert(x);
}

void TxPool::Fluff::InternalInsert(Element& x)
{
	if (x.IsOutdated())
		m_setOutdated.insert(x.m_Outdated);
	else
	{
		m_setTxs.insert(x.m_Tx);
		m_setProfit.insert(x.m_Profit);
	}
}

void TxPool::Fluff::InternalErase(Element& x)
{
	if (x.IsOutdated())
		m_setOutdated.erase(OutdatedSet::s_iterator_to(x.m_Outdated));
	else
	{
		m_setTxs.erase(TxSet::s_iterator_to(x.m_Tx));
		m_setProfit.erase(ProfitSet::s_iterator_to(x.m_Profit));
	}
}

void TxPool::Fluff::Delete(Element& x)
{
	assert(x.m_pValue);
	x.m_pValue.reset();
	DeleteEmpty(x);
}

void TxPool::Fluff::DeleteEmpty(Element& x)
{
	assert(!x.m_pValue);
	InternalErase(x);
	Release(x);
}

void TxPool::Fluff::Release(Element& x)
{
	assert(x.m_Queue.m_Refs);
	if (!--x.m_Queue.m_Refs)
	{
		assert(!x.m_pValue);
		m_Queue.erase(Queue::s_iterator_to(x.m_Queue));
		delete &x;
	}
}

void TxPool::Fluff::Clear()
{
	while (!m_setProfit.empty())
		Delete(m_setProfit.begin()->get_ParentObj());

	while (!m_setOutdated.empty())
		Delete(m_setOutdated.begin()->get_ParentObj());
}

/////////////////////////////
// Stem
bool TxPool::Stem::TryMerge(Element& trg, Element& src)
{
	assert(trg.m_bAggregating && src.m_bAggregating);

	HeightRange hr = trg.m_Height;
	hr.Intersect(src.m_Height);
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

	auto fees = trg.m_Profit.m_Fee;
	fees += src.m_Profit.m_Fee;
	Amount feeReserve = 0;
	if (!ValidateTxContext(txNew, hr, fees, feeReserve))
		return false; // conflicting txs, can't merge

	trg.m_Profit.m_Fee += fees;
	trg.m_Profit.SetSize(txNew, trg.m_Profit.get_Correction() + src.m_Profit.get_Correction());

	Delete(src);
	DeleteKrn(trg);

	trg.m_pValue->m_vInputs.swap(txNew.m_vInputs);
	trg.m_pValue->m_vOutputs.swap(txNew.m_vOutputs);
	trg.m_pValue->m_vKernels.swap(txNew.m_vKernels);
	trg.m_pValue->m_Offset = txNew.m_Offset;
	trg.m_FeeReserve = feeReserve;
	trg.m_Height = hr;

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
	DeleteConfirm(x);

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

void TxPool::Stem::InsertConfirm(Element& x, Height h)
{
	DeleteConfirm(x);
	if (MaxHeight != h)
	{
		x.m_Confirm.m_Height = h;
		m_lstConfirm.push_back(x.m_Confirm);
	}
}

void TxPool::Stem::DeleteConfirm(Element& x)
{
	if (MaxHeight != x.m_Confirm.m_Height)
	{
		m_lstConfirm.erase(ConfirmList::s_iterator_to(x.m_Confirm));
		x.m_Confirm.m_Height = MaxHeight;
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
	p->m_Depth = 1;
	if (pParent)
	{
		p->m_Size += pParent->m_Size;
		p->m_Depth += pParent->m_Depth;
	}

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
