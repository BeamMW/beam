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

#pragma once

#include <boost/intrusive/set.hpp>
#include "../core/block_crypt.h"
#include "../utility/io/timer.h"

namespace beam {

struct TxPool
{
	struct Profit
		:public boost::intrusive::set_base_hook<>
	{
		AmountBig::Type m_Fee; // since a tx may include multiple kernels - theoretically fee may be huge (though highly unlikely)
		uint32_t m_nSize;

		void SetSize(const Transaction&);

		bool operator < (const Profit& t) const;
	};

	struct Fluff
	{
		struct Element
		{
			Transaction::Ptr m_pValue;

			struct Tx
				:public boost::intrusive::set_base_hook<>
			{
				Transaction::KeyType m_Key;

				bool operator < (const Tx& t) const { return m_Key < t.m_Key; }
				IMPLEMENT_GET_PARENT_OBJ(Element, m_Tx)
			} m_Tx;

			struct Profit
				:public TxPool::Profit
			{
				IMPLEMENT_GET_PARENT_OBJ(Element, m_Profit)
			} m_Profit;

			struct Threshold
				:public boost::intrusive::set_base_hook<>
			{
				Height m_Value;

				bool operator < (const Threshold& t) const { return m_Value < t.m_Value; }

				IMPLEMENT_GET_PARENT_OBJ(Element, m_Threshold)
			} m_Threshold;
		};

		typedef boost::intrusive::multiset<Element::Tx> TxSet;
		typedef boost::intrusive::multiset<Element::Profit> ProfitSet;
		typedef boost::intrusive::multiset<Element::Threshold> ThresholdSet;

		TxSet m_setTxs;
		ProfitSet m_setProfit;
		ThresholdSet m_setThreshold;

		void AddValidTx(Transaction::Ptr&&, const Transaction::Context&, const Transaction::KeyType&);
		void Delete(Element&);
		void Clear();

		void DeleteOutOfBound(Height);
		void ShrinkUpTo(uint32_t nCount);

		~Fluff() { Clear(); }
	};

	struct Stem
	{

		struct Element
		{
			Transaction::Ptr m_pValue;
			bool m_bAggregating; // if set - the tx isn't broadcasted yet, and inserted in the 'Profit' set

			struct Time
				:public boost::intrusive::set_base_hook<>
			{
				uint32_t m_Value;

				bool operator < (const Time& t) const { return m_Value < t.m_Value; }

				IMPLEMENT_GET_PARENT_OBJ(Element, m_Time)
			} m_Time;

			struct Profit
				:public TxPool::Profit
			{
				IMPLEMENT_GET_PARENT_OBJ(Element, m_Profit)
			} m_Profit;

			struct Kernel
				:public boost::intrusive::set_base_hook<>
			{
				Element* m_pThis;
				Merkle::Hash m_hv;
				bool operator < (const Kernel& t) const { return m_hv < t.m_hv; }
			};

			std::vector<Kernel> m_vKrn;
		};

		typedef boost::intrusive::multiset<Element::Kernel> KrnSet;
		typedef boost::intrusive::multiset<Element::Time> TimeSet;
		typedef boost::intrusive::multiset<Element::Profit> ProfitSet;

		KrnSet m_setKrns;
		TimeSet m_setTime;
		ProfitSet m_setProfit;

		void Delete(Element&);
		void Clear();
		void InsertKrn(Element&);
		void DeleteKrn(Element&);
		void InsertAggr(Element&);
		void DeleteAggr(Element&);
		void DeleteTimer(Element&);

		bool TryMerge(Element& trg, Element& src);

		Element* get_NextTimeout(uint32_t& nTimeout_ms);
		void SetTimer(uint32_t nTimeout_ms, Element&);
		void KillTimer();

		io::Timer::Ptr m_pTimer; // set during the 1st phase
		void OnTimer();

		~Stem() { Clear(); }

		virtual bool ValidateTxContext(const Transaction&) = 0; // assuming context-free validation is already performed, but 
		virtual void OnTimedOut(Element&) = 0;

	private:
		void DeleteRaw(Element&);
		void SetTimerRaw(uint32_t nTimeout_ms);
	};
};


} // namespace beam
