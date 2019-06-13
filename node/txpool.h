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
#include <boost/intrusive/list.hpp>
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
				HeightRange m_Height;

				bool operator < (const Threshold& t) const { return m_Height.m_Max < t.m_Height.m_Max; }

				IMPLEMENT_GET_PARENT_OBJ(Element, m_Threshold)
			} m_Threshold;

			struct Queue
				:public boost::intrusive::list_base_hook<>
			{
				uint32_t m_Refs = 0;
				IMPLEMENT_GET_PARENT_OBJ(Element, m_Queue)
			} m_Queue;
		};

		typedef boost::intrusive::multiset<Element::Tx> TxSet;
		typedef boost::intrusive::multiset<Element::Profit> ProfitSet;
		typedef boost::intrusive::multiset<Element::Threshold> ThresholdSet;
		typedef boost::intrusive::list<Element::Queue> Queue;

		TxSet m_setTxs;
		ProfitSet m_setProfit;
		ThresholdSet m_setThreshold;
		Queue m_Queue;

		Element* AddValidTx(Transaction::Ptr&&, const Transaction::Context&, const Transaction::KeyType&);
		void Delete(Element&);
		void Release(Element&);
		void Clear();

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

			HeightRange m_Height;

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

		virtual bool ValidateTxContext(const Transaction&, const HeightRange&) = 0; // assuming context-free validation is already performed, but 
		virtual void OnTimedOut(Element&) = 0;

	private:
		void DeleteRaw(Element&);
		void SetTimerRaw(uint32_t nTimeout_ms);
	};
};


} // namespace beam
