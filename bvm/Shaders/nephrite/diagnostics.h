#pragma once
#include "contract.h"
//#include "../Explorer/Fmt.h"
#include "../Sort.h"

namespace Nephrite
{

	struct Global1
		:public Global
	{
		const ContractID& m_Cid;

		Global1(const ContractID& cid) :m_Cid(cid) {}

		bool Load()
		{
			{
				Env::Key_T<uint8_t> key;
				_POD_(key.m_Prefix.m_Cid) = m_Cid;
				key.m_KeyInContract = Tags::s_State;

				if (!Env::VarReader::Read_T(key, Cast::Down<Global>(*this)))
					return false;
			}

			m_BaseRate.Decay();
			m_StabPool.AddReward(Env::get_Height());

			return true;
		}
	};


	struct Global2
		:public Global1
	{
		using Global1::Global1;

		template <typename TKey, typename TVal, typename TElement>
		struct Collection
		{
			struct Item
				:public TElement
			{
				TKey m_Key;

				bool operator < (const TKey& key) const {
					return m_Key < key;
				}
			};

			Utils::Vector<Item> m_v;

			void Load(const ContractID& cid, uint8_t nTag)
			{
#pragma pack (push, 1)
				struct MyKeyAux {
					uint8_t m_Tag;
					TKey m_Key;
				};
#pragma pack (pop)

				Env::Key_T<MyKeyAux> k0, k1;
				_POD_(k0.m_Prefix.m_Cid) = cid;
				_POD_(k1.m_Prefix.m_Cid) = cid;

				_POD_(k0.m_KeyInContract.m_Key).SetZero();
				_POD_(k1.m_KeyInContract.m_Key).SetObject(0xff);

				k0.m_KeyInContract.m_Tag = nTag;
				k1.m_KeyInContract.m_Tag = nTag;

				for (Env::VarReader r(k0, k1); ; m_v.m_Count++)
				{
					m_v.Prepare(m_v.m_Count + 1);
					if (!r.MoveNext_T(k0, Cast::Down<TVal>(m_v.m_p[m_v.m_Count])))
						break;

					m_v.m_p[m_v.m_Count].m_Key = k0.m_KeyInContract.m_Key;
				}
			}

			Item& Find(const TKey& k)
			{
				uint32_t iPos = MedianSearch(m_v.m_p, m_v.m_Count, k);
				assert(iPos < m_v.m_Count);
				auto& ret = m_v.m_p[iPos];
				assert(ret.m_Key == k);
				return ret;
			}

			void CvtKeyByteOrder()
			{
				for (uint32_t i = 0; i < m_v.m_Count; i++)
				{
					auto& key = m_v.m_p[i].m_Key;
					key = Utils::FromBE(key);
				}

			}

		};

		template <uint32_t nDims>
		struct EpochPlus
			:public HomogenousPool::Epoch<nDims>
		{
			struct Adj {
				Amount m_Sell;
				uint32_t m_Users;
				HomogenousPool::Epoch0::Dim m_pDim[nDims];

				void Reset() {
					_POD_(*this).SetZero();
				};

			} m_Adj;
		};

		Collection<Trove::ID, Trove, Trove> m_CollTroves;
		Collection<uint32_t, HomogenousPool::Epoch<1>, EpochPlus<1> > m_EpochsRedist;
		Collection<uint32_t, HomogenousPool::Epoch<2>, EpochPlus<2> > m_EpochsStab;
		Collection<PubKey, Balance, Balance> m_Balances;
		Collection<PubKey, StabPoolEntry, StabPoolEntry> m_Stabs;

		template <typename TCollection>
		struct EpochStorage
		{
			TCollection& m_Coll;
			EpochStorage(TCollection& coll) :m_Coll(coll) {}

			template <uint32_t nDims>
			void Load(uint32_t iEpoch, HomogenousPool::Epoch<nDims>& e) {
				e = m_Coll.Find(Utils::FromBE(iEpoch));
			}

			template <uint32_t nDims>
			void Save(uint32_t iEpoch, const HomogenousPool::Epoch<nDims>& e) {
			}
			void Del(uint32_t iEpoch) {
			}
		};

		EpochPlus<1>::Adj m_adjR0, m_adjR1;
		EpochPlus<2>::Adj m_adjS0, m_adjS1;


		bool Load()
		{
			if (!Global1::Load())
				return false;

			m_EpochsRedist.Load(m_Cid, Tags::s_Epoch_Redist);
			m_EpochsStab.Load(m_Cid, Tags::s_Epoch_Stable);
			m_Balances.Load(m_Cid, Tags::s_Balance);
			m_Stabs.Load(m_Cid, Tags::s_StabPool);
			m_CollTroves.Load(m_Cid, Tags::s_Trove);

			m_EpochsRedist.CvtKeyByteOrder();
			m_EpochsStab.CvtKeyByteOrder();
			m_CollTroves.CvtKeyByteOrder();

			m_adjR0.Reset();
			m_adjR1.Reset();
			m_adjS0.Reset();
			m_adjS1.Reset();

			for (uint32_t i = 0; i < m_EpochsRedist.m_v.m_Count; i++)
				m_EpochsRedist.m_v.m_p[i].m_Adj.Reset();
			for (uint32_t i = 0; i < m_EpochsStab.m_v.m_Count; i++)
				m_EpochsStab.m_v.m_p[i].m_Adj.Reset();

			return true;
		}

		Trove& get_Trove(Trove::ID tid, Pair& vals)
		{
			auto& t = m_CollTroves.Find(Utils::FromBE(tid));

			EpochStorage storR(m_EpochsRedist);
			vals = m_RedistPool.get_UpdatedAmounts(t, storR);

			EpochPlus<1>::Adj& adj =
				(t.m_RedistUser.m_iEpoch == m_RedistPool.m_iActive) ? m_adjR0 :
				(t.m_RedistUser.m_iEpoch + 1 == m_RedistPool.m_iActive) ? m_adjR1 :
				m_EpochsRedist.Find(Utils::FromBE(t.m_RedistUser.m_iEpoch)).m_Adj;

			adj.m_Users++;
			adj.m_Sell += vals.Tok;
			adj.m_pDim[0].m_Buy += vals.Col;

			return t;
		}

		void PrintAll()
		{

			EpochStorage storS(m_EpochsStab);

			Pair totals = { 0 };

			{
				Env::DocArray gr1("troves");

				uint32_t nTrovesActive = 0;
				for (Trove::ID tid = m_Troves.m_iHead; tid; nTrovesActive++)
				{
					if (nTrovesActive > m_CollTroves.m_v.m_Count * 2)
						break; // probably cycles

					Pair vals;
					auto& t = get_Trove(tid, vals);
					totals.Col += t.m_Amounts.Col;

					Env::DocGroup gr2("");

					Env::DocAddNum("tid", tid);
					Env::DocAddBlob_T("pk", t.m_pkOwner);
					Env::DocAddNum("_org-Tok", t.m_Amounts.Tok);
					Env::DocAddNum("_org-Col", t.m_Amounts.Col);
					Env::DocAddNum("_adj-Tok", vals.Tok);
					Env::DocAddNum("_adj-Col", vals.Col);
					Env::DocAddNum("iEpoch", t.m_RedistUser.m_iEpoch);

					tid = t.m_iNext;
				}

				if (nTrovesActive != m_CollTroves.m_v.m_Count)
				{
					Env::DocGroup gr2("");
					Env::DocAddNum("alert troves count", m_CollTroves.m_v.m_Count);
				}

			}

			{
				Env::DocGroup gr1("redist");

				Env::DocAddNum("iActive", m_RedistPool.m_iActive);

				{
					Env::DocArray gr2("epochs");

					if (m_RedistPool.m_Active.m_Users)
						OnEpoch(totals, m_RedistPool.m_Active, m_RedistPool.m_iActive, m_adjR0);
					if (m_RedistPool.m_Draining.m_Users)
						OnEpoch(totals, m_RedistPool.m_Draining, m_RedistPool.m_iActive - 1, m_adjR1);

					for (uint32_t i = m_EpochsRedist.m_v.m_Count; i--; )
					{
						auto& x = m_EpochsRedist.m_v.m_p[i];
						OnEpoch(totals, x, Utils::FromBE(x.m_Key), x.m_Adj);
					}
				}

			}

			{
				Env::DocGroup gr1("totals");

				Env::DocAddNum("_Tok", m_Troves.m_Totals.Tok);
				Env::DocAddNum("_Col", m_Troves.m_Totals.Col);

				if (m_Troves.m_Totals.Tok != totals.Tok)
					Env::DocAddNum("alert Tok", totals.Tok);
				if (m_Troves.m_Totals.Col != totals.Col)
					Env::DocAddNum("alert Col", totals.Col);
			}

			{
				Env::DocGroup gr1("Stab");
				_POD_(totals).SetZero();

				{
					Env::DocArray gr2("positions");

					for (uint32_t i = 0; i < m_Stabs.m_v.m_Count; i++)
					{
						Env::DocGroup gr3("");

						auto& x = m_Stabs.m_v.m_p[i];

						StabilityPool::User::Out vals;
						EpochStorage storS(m_EpochsStab);
						m_StabPool.UserDel<true, false>(x.m_User, vals, 0, storS);

						EpochPlus<2>::Adj& adj =
							(x.m_User.m_iEpoch == m_StabPool.m_iActive) ? m_adjS0 :
							(x.m_User.m_iEpoch + 1 == m_StabPool.m_iActive) ? m_adjS1 :
							m_EpochsStab.Find(Utils::FromBE(x.m_User.m_iEpoch)).m_Adj;

						adj.m_Users++;
						adj.m_Sell += vals.m_Sell;
						adj.m_pDim[0].m_Buy += vals.m_pBuy[0];
						adj.m_pDim[1].m_Buy += vals.m_pBuy[1];

						Env::DocAddBlob_T("pk", x.m_Key);
						Env::DocAddNum("_adj-Tok", vals.m_Sell);
						Env::DocAddNum("_adj-Col", vals.m_pBuy[0]);
						Env::DocAddNum("_adj-Rew", vals.m_pBuy[1]);
						Env::DocAddNum("iEpoch", x.m_User.m_iEpoch);
					}
				}

				{
					Env::DocArray gr2("epochs");

					if (m_StabPool.m_Active.m_Users)
						OnEpoch(totals, m_StabPool.m_Active, m_StabPool.m_iActive, m_adjS0);
					if (m_StabPool.m_Draining.m_Users)
						OnEpoch(totals, m_StabPool.m_Draining, m_StabPool.m_iActive - 1, m_adjS1);

					for (uint32_t i = m_EpochsStab.m_v.m_Count; i--; )
					{
						auto& x = m_EpochsStab.m_v.m_p[i];
						OnEpoch(totals, x, Utils::FromBE(x.m_Key), x.m_Adj);
					}
				}

				Env::DocAddNum("_Tok", totals.Tok);
				Env::DocAddNum("_Col", totals.Col);

			}
		}

		template <uint32_t nDims>
		void OnEpoch(Pair& totals, const HomogenousPool::Epoch<nDims>& e, uint32_t iEpoch, const typename EpochPlus<nDims>::Adj& adj)
		{
			Env::DocGroup gr1("");
			Env::DocAddNum("iEpoch", iEpoch);
			Env::DocAddNum("_Tok", e.m_Sell);
			Env::DocAddNum("_Col", e.m_pDim[0].m_Buy);

			Env::DocAddNum("_uTok", adj.m_Sell);
			Env::DocAddNum("_uCol", adj.m_pDim[0].m_Buy);

			if constexpr (nDims == 2)
			{
				Env::DocAddNum("_Rew", e.m_pDim[1].m_Buy);
				Env::DocAddNum("_uRew", adj.m_pDim[1].m_Buy);
			}

			if (!e.m_Users)
				Env::DocAddText("alert no users", "");

			Env::DocAddNum("Users", e.m_Users);
			if (e.m_Users != adj.m_Users)
				Env::DocAddNum("alert _Users", adj.m_Users);

			totals.Tok += e.m_Sell;
			totals.Col += e.m_pDim[0].m_Buy;
		}
	};

}
