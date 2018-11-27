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

void TxPool::Profit::SetSize(const Transaction& tx)
{
	m_nSize = (uint32_t) tx.get_Reader().get_SizeNetto();
}

bool TxPool::Profit::operator < (const Profit& t) const
{
	// handle overflow. To be precise need to use big-int (96-bit) arithmetics
	//	return m_Fee * t.m_nSize > t.m_Fee * m_nSize;

	uintBigFor<AmountBig>::Type fee0, fee1;
	m_Fee.Export(fee0);
	t.m_Fee.Export(fee1);

	return
		(fee0 * uintBigFrom(t.m_nSize)) >
		(fee1 * uintBigFrom(m_nSize));
}

/////////////////////////////
// Fluff
void TxPool::Fluff::AddValidTx(Transaction::Ptr&& pValue, const Transaction::Context& ctx, const Transaction::KeyType& key)
{
	assert(pValue);

	Element* p = new Element;
	p->m_pValue = std::move(pValue);
	p->m_Threshold.m_Value	= ctx.m_Height.m_Max;
	p->m_Profit.m_Fee = ctx.m_Fee;
	p->m_Profit.SetSize(*p->m_pValue);
	p->m_Tx.m_Key = key;

	m_setThreshold.insert(p->m_Threshold);
	m_setProfit.insert(p->m_Profit);
	m_setTxs.insert(p->m_Tx);
}

void TxPool::Fluff::Delete(Element& x)
{
	m_setThreshold.erase(ThresholdSet::s_iterator_to(x.m_Threshold));
	m_setProfit.erase(ProfitSet::s_iterator_to(x.m_Profit));
	m_setTxs.erase(TxSet::s_iterator_to(x.m_Tx));
	delete &x;
}

void TxPool::Fluff::DeleteOutOfBound(Height h)
{
	while (!m_setThreshold.empty())
	{
		Element::Threshold& t = *m_setThreshold.begin();
		if (t.m_Value >= h)
			break;

		Delete(t.get_ParentObj());
	}
}

void TxPool::Fluff::ShrinkUpTo(uint32_t nCount)
{
	while (m_setProfit.size() > nCount)
		Delete(m_setProfit.rbegin()->get_ParentObj());
}

void TxPool::Fluff::Clear()
{
	while (!m_setThreshold.empty())
		Delete(m_setThreshold.begin()->get_ParentObj());
}

/////////////////////////////
// Stem
bool TxPool::Stem::TryMerge(Element& trg, Element& src)
{
	assert(trg.m_bAggregating && src.m_bAggregating);

	Transaction txNew;
	TxVectors::Writer wtx(txNew, txNew);

	volatile bool bStop = false;
	wtx.Combine(trg.m_pValue->get_Reader(), src.m_pValue->get_Reader(), bStop);

	txNew.m_Offset = ECC::Scalar::Native(trg.m_pValue->m_Offset) + ECC::Scalar::Native(src.m_pValue->m_Offset);

#ifdef _DEBUG
	Transaction::Context ctx;
	assert(txNew.IsValid(ctx));
#endif // _DEBUG

	if (!ValidateTxContext(txNew))
		return false; // conflicting txs, can't merge

	trg.m_Profit.m_Fee += src.m_Profit.m_Fee;
	trg.m_Profit.SetSize(txNew);

	Delete(src);
	DeleteKrn(trg);

	trg.m_pValue->m_vInputs.swap(txNew.m_vInputs);
	trg.m_pValue->m_vOutputs.swap(txNew.m_vOutputs);
	trg.m_pValue->m_vKernels.swap(txNew.m_vKernels);
	trg.m_pValue->m_Offset = txNew.m_Offset;

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
		tx.m_vKernels[i]->get_ID(n.m_hv);
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

} // namespace beam
