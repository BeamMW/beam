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

//#include "../node.h"

#include "../node.h"
#include "../db.h"
#include "../processor.h"
#include "../../core/fly_client.h"
#include "../../core/serialization_adapters.h"
#include "../../core/treasury.h"
#include "../../core/block_rw.h"
#include "../../utility/test_helpers.h"
#include "../../utility/serialize.h"
#include "../../core/unittest/mini_blockchain.h"

#ifndef LOG_VERBOSE_ENABLED
    #define LOG_VERBOSE_ENABLED 0
#endif
#include "utility/logger.h"

namespace ECC {

	void GenerateRandom(void* p, uint32_t n)
	{
		for (uint32_t i = 0; i < n; i++)
			((uint8_t*) p)[i] = (uint8_t) rand();
	}

	void SetRandom(uintBig& x)
	{
		GenerateRandom(x.m_pData, x.nBytes);
	}

	void SetRandom(Scalar::Native& x)
	{
		Scalar s;
		while (true)
		{
			SetRandom(s.m_Value);
			if (!x.Import(s))
				break;
		}
	}

	void SetRandom(Key::IKdf::Ptr& pRes)
	{
		uintBig seed;
		SetRandom(seed);
		HKdf::Create(pRes, seed);
	}

	void SetRandom(beam::Node& n)
	{
		uintBig seed;
		SetRandom(seed);

		n.m_Keys.InitSingleKey(seed);
	}
}

#ifndef WIN32
#	include <unistd.h>
#endif // WIN32

int g_TestsFailed = 0;

void TestFailed(const char* szExpr, uint32_t nLine)
{
	printf("Test failed! Line=%u, Expression: %s\n", nLine, szExpr);
	g_TestsFailed++;
	fflush(stdout);
}

#define verify_test(x) \
	do { \
		if (!(x)) \
			TestFailed(#x, __LINE__); \
	} while (false)

#define fail_test(msg) TestFailed(msg, __LINE__)

namespace beam
{
	ByteBuffer g_Treasury;

	Amount get_Emission(const HeightRange& hr, Amount base = Rules::get().Emission.Value0)
	{
		AmountBig::Type vbig;
		Rules::get().get_Emission(vbig, hr, base);

		Amount res;
		vbig.ExportWord<1>(res);
		return res;
	}

	void PrintEmissionSchedule()
	{
		struct Year
		{
			Amount m_PerBlockMiner;
			Amount m_PerBlockTreasury;
			Amount m_PerBlock;
			Amount m_Total;
		};

		std::vector<Year> vYears;
		HeightRange hr(0, 0);
		const uint32_t nYearBlocks = 1440 * 365;

		Amount nCumulative = 0;

		for (uint32_t iY = 0; ; iY++)
		{
			hr.m_Min = hr.m_Max + 1;
			hr.m_Max += nYearBlocks;

			Amount val = get_Emission(hr);
			if (!val)
				break;

			vYears.emplace_back();
			Year& y = vYears.back();

			y.m_PerBlock = val / nYearBlocks;
			y.m_PerBlockMiner = y.m_PerBlock;

			if (iY < 5)
			{
				y.m_PerBlockTreasury = y.m_PerBlockMiner / 4;
				y.m_PerBlock += y.m_PerBlockTreasury;
				val += y.m_PerBlockTreasury * nYearBlocks;
			}
			else
				y.m_PerBlockTreasury = 0;

			y.m_Total = val;
			nCumulative += val;
		}

		std::cout << "Emission schedule" << std::endl;
		std::cout << "Year, Miner emission per block, Treasury emission per block, Total coins emitted per block, Total coins emitted per year, Cumulative Coins Emitted, Cumulative % of total" << std::endl;

		Amount nEmitted = 0;
		uint32_t nYear = 2019;
		for (size_t i = 0; i < vYears.size(); i++)
		{
			const Year& y = vYears[i];

			std::cout << (nYear++) << "," << y.m_PerBlockMiner << "," << y.m_PerBlockTreasury << "," << y.m_PerBlock << "," << y.m_Total << ",";
			std::cout << (nEmitted += y.m_Total) << ",";
			std::cout << double(nEmitted) * 100. / double(nCumulative) << std::endl;
		}

		nCumulative++;
	}

	void PrepareTreasury()
	{
		Key::IKdf::Ptr pKdf;
		ECC::SetRandom(pKdf);

		PeerID pid;
		ECC::Scalar::Native sk;
		Treasury::get_ID(*pKdf, pid, sk);

		Treasury tres;
		Treasury::Parameters pars;
		pars.m_Bursts = 1;
		Treasury::Entry* pE = tres.CreatePlan(pid, Rules::get().Emission.Value0 / 5, pars);

		pE->m_pResponse.reset(new Treasury::Response);
		uint64_t nIndex = 1;
		verify_test(pE->m_pResponse->Create(pE->m_Request, *pKdf, nIndex));

		Treasury::Data data;
		data.m_sCustomMsg = "test treasury";
		tres.Build(data);

		beam::Serializer ser;
		ser & data;

		ser.swap_buf(g_Treasury);

		ECC::Hash::Processor() << Blob(g_Treasury) >> Rules::get().TreasuryChecksum;
	}

	uint32_t CountTips(NodeDB& db, bool bFunctional, NodeDB::StateID* pLast = NULL)
	{
		uint32_t nTips = 0;

		NodeDB::WalkerState ws(db);

		if (bFunctional)
			db.EnumFunctionalTips(ws);
		else
			db.EnumTips(ws);

		while (ws.MoveNext())
			nTips++;

		if (pLast)
			*pLast = ws.m_Sid;

		return nTips;
	}

	void TestNodeDB(const char* sz)
	{
		NodeDB db;
		db.Open(sz);

		NodeDB::Transaction tr(db);

		const uint32_t hMax = 250;
		const uint32_t nOrd = 3;
		const uint32_t hFork0 = 70;

		Merkle::Hash hvZero(Zero);

		std::vector<Block::SystemState::Full> vStates;
		vStates.resize(hMax);
		memset0(&vStates.at(0), vStates.size());

		Merkle::CompactMmr cmmr, cmmrFork;

		for (uint32_t h = 0; h < hMax; h++)
		{
			Block::SystemState::Full& s = vStates[h];
			s.m_Height = h + Rules::HeightGenesis;

			s.m_ChainWork = h; // must be in ascending order

			if (h)
			{
				vStates[h - 1].get_Hash(s.m_Prev);
				cmmr.Append(s.m_Prev);
			}

			if (hFork0 == h)
				cmmrFork = cmmr;

			cmmr.get_Hash(s.m_Definition);
			Merkle::Interpret(s.m_Definition, hvZero, true);

			s.m_Kernels = Zero;
		}

		uint64_t pRows[hMax];

		// insert states in random order
		for (uint32_t h1 = 0; h1 < nOrd; h1++)
		{
			for (uint32_t h = h1; h < hMax; h += nOrd)
			{
				pRows[h] = db.InsertState(vStates[h]);
				db.assert_valid();

				if (h)
				{
					db.SetStateFunctional(pRows[h]);
					db.assert_valid();
				}
			}
		}

		Blob bBodyP("body", 4), bBodyE("abc", 3);
		Merkle::Hash peer, peer2;
		memset(peer.m_pData, 0x66, peer.nBytes);

		db.SetStateBlock(pRows[0], bBodyP, bBodyE);
		verify_test(!db.get_Peer(pRows[0], peer2));

		db.set_Peer(pRows[0], &peer);
		verify_test(db.get_Peer(pRows[0], peer2));
		verify_test(peer == peer2);

		db.set_Peer(pRows[0], NULL);
		verify_test(!db.get_Peer(pRows[0], peer2));

		ByteBuffer bbBodyP, bbBodyE;
		db.GetStateBlock(pRows[0], &bbBodyP, &bbBodyE);

		//db.DelStateBlockPP(pRows[0]);
		//db.GetStateBlock(pRows[0], &bbBodyP, &bbBodyE);

		db.DelStateBlockAll(pRows[0]);
		db.GetStateBlock(pRows[0], &bbBodyP, &bbBodyE);

		tr.Commit();
		tr.Start(db);

		verify_test(CountTips(db, false) == 1);
		verify_test(CountTips(db, true) == 0);

		// a subbranch
		Block::SystemState::Full s = vStates[hFork0];
		s.m_Definition.Inc(); // alter

		uint64_t r0 = db.InsertState(s);

		verify_test(CountTips(db, false) == 2);

		db.assert_valid();
		db.SetStateFunctional(r0);
		db.assert_valid();

		verify_test(CountTips(db, true) == 0);

		s.get_Hash(s.m_Prev);
		cmmrFork.Append(s.m_Prev);
		cmmrFork.get_Hash(s.m_Definition);
		Merkle::Interpret(s.m_Definition, hvZero, true);

		s.m_Height++;
		s.m_ChainWork = s.m_Height;

		uint64_t rowLast1 = db.InsertState(s);

		NodeDB::StateID sid;
		verify_test(CountTips(db, false, &sid) == 2);
		verify_test(sid.m_Height == hMax-1 + Rules::HeightGenesis);

		db.SetStateFunctional(rowLast1);
		db.assert_valid();

		db.SetStateFunctional(pRows[0]); // this should trigger big update
		db.assert_valid();
		verify_test(CountTips(db, true, &sid) == 2);
		verify_test(sid.m_Height == hFork0 + 1 + Rules::HeightGenesis);

		tr.Commit();
		tr.Start(db);

		// test proofs
		NodeDB::StateID sid2;
		verify_test(CountTips(db, false, &sid2) == 2);
		verify_test(sid2.m_Height == hMax-1 + Rules::HeightGenesis);

		do
		{
			if (sid2.m_Height + 1 < hMax + Rules::HeightGenesis)
			{
				Merkle::Hash hv;
				db.get_PredictedStatesHash(hv, sid2);
				Merkle::Interpret(hv, hvZero, true);
				verify_test(hv == vStates[(size_t) sid2.m_Height + 1 - Rules::HeightGenesis].m_Definition);
			}

			const Merkle::Hash& hvRoot = vStates[(size_t) sid2.m_Height - Rules::HeightGenesis].m_Definition;

			for (Height h = Rules::HeightGenesis; h < sid2.m_Height; h++)
			{
				Merkle::ProofBuilderStd bld;
				db.get_Proof(bld, sid2, h);

				Merkle::Hash hv;
				vStates[h - Rules::HeightGenesis].get_Hash(hv);
				Merkle::Interpret(hv, bld.m_Proof);
				Merkle::Interpret(hv, hvZero, true);

				verify_test(hvRoot == hv);
			}

		} while (db.get_Prev(sid2));

		while (db.get_Prev(sid))
			;
		verify_test(sid.m_Height == Rules::HeightGenesis);

		db.SetStateNotFunctional(pRows[0]);
		db.assert_valid();
		verify_test(CountTips(db, true) == 0);

		db.SetStateFunctional(pRows[0]);
		db.assert_valid();
		verify_test(CountTips(db, true) == 2);

		for (sid.m_Height = Rules::HeightGenesis; sid.m_Height <= hMax; sid.m_Height++)
		{
			sid.m_Row = pRows[sid.m_Height - Rules::HeightGenesis];
			db.MoveFwd(sid);
		}

		tr.Commit();
		tr.Start(db);

		while (sid.m_Row)
			db.MoveBack(sid);

		tr.Commit();
		tr.Start(db);

		// Delete main branch up to this tip
		uint64_t row = pRows[hMax-1];
		uint32_t h = hMax;
		for (; ; h--)
		{
			verify_test(row);
			if (!db.DeleteState(row, row))
				break;
			db.assert_valid();
		}

		verify_test(row && (h == hFork0));

		for (h += 2; ; h--)
		{
			if (!rowLast1)
				break;
			verify_test(db.DeleteState(rowLast1, rowLast1));
			db.assert_valid();
		}

		verify_test(!h);

		tr.Commit();
		tr.Start(db);

		for (int i = 0; i < 20; i++)
		{
			NodeDB::WalkerPeer::Data d;
			ECC::SetRandom(d.m_ID);
			d.m_Address = i * 17;
			d.m_LastSeen = i + 10;
			d.m_Rating = i * 100 + 50;

			db.PeerIns(d);
		}

		NodeDB::WalkerPeer wlkp(db);
		for (db.EnumPeers(wlkp); wlkp.MoveNext(); )
			;

		db.PeersDel();

		for (db.EnumPeers(wlkp); wlkp.MoveNext(); )
			;


		NodeDB::WalkerBbs::Data dBbs;

		for (uint32_t i = 0; i < 200; i++)
		{
			dBbs.m_Key = i;
			dBbs.m_Channel = i % 7;
			dBbs.m_TimePosted = i + 100;
			dBbs.m_Message.p = "hello";
			dBbs.m_Message.n = 5;

			db.BbsIns(dBbs);
		}

		NodeDB::WalkerBbs wlkbbs(db);
		wlkbbs.m_Data = dBbs;
		verify_test(db.BbsFind(wlkbbs));

		wlkbbs.m_Data.m_Key.Inc();
		verify_test(!db.BbsFind(wlkbbs));


		for (wlkbbs.m_Data.m_Channel = 0; wlkbbs.m_Data.m_Channel < 7; wlkbbs.m_Data.m_Channel++)
		{
			wlkbbs.m_ID = 0;
			for (db.EnumBbsCSeq(wlkbbs); wlkbbs.MoveNext(); )
				;
		}

		for (wlkbbs.m_Data.m_Channel = 0; wlkbbs.m_Data.m_Channel < 7; wlkbbs.m_Data.m_Channel++)
		{
			wlkbbs.m_ID = 0;
			for (db.EnumBbsCSeq(wlkbbs); wlkbbs.MoveNext(); )
				;
		}

		Key::ID kid(Zero);
		kid.m_Idx = 345;

		verify_test(db.GetDummyHeight(kid) == MaxHeight);

		db.InsertDummy(176, kid);

		kid.m_Idx = 346;
		db.InsertDummy(568, kid);

		kid.m_Idx = 345;
		verify_test(db.GetDummyHeight(kid) == 176);

		Height h1 = db.GetLowestDummy(kid);
		verify_test(h1 == 176);
		verify_test(kid.m_Idx == 345U);

		db.SetDummyHeight(kid, 1055);

		h1 = db.GetLowestDummy(kid);
		verify_test(h1 == 568);
		verify_test(kid.m_Idx == 346U);
		
		db.DeleteDummy(kid);

		h1 = db.GetLowestDummy(kid);
		verify_test(h1 == 1055);
		verify_test(kid.m_Idx == 345U);

		db.DeleteDummy(kid);

		verify_test(MaxHeight == db.GetLowestDummy(kid));

		// Kernels
		db.InsertKernel(bBodyP, 5);
		db.InsertKernel(bBodyP, 5); // duplicate
		db.InsertKernel(bBodyP, 7);
		db.InsertKernel(bBodyP, 2);

		verify_test(db.FindKernel(bBodyP) == 7);
		verify_test(db.FindKernel(bBodyE) == 0);

		db.DeleteKernel(bBodyP, 7);
		verify_test(db.FindKernel(bBodyP) == 5);
		db.DeleteKernel(bBodyP, 5);
		verify_test(db.FindKernel(bBodyP) == 5);
		db.DeleteKernel(bBodyP, 2);
		verify_test(db.FindKernel(bBodyP) == 5);
		db.DeleteKernel(bBodyP, 5);
		verify_test(db.FindKernel(bBodyP) == 0);


		tr.Commit();
	}

#ifdef WIN32
		const char* g_sz = "mytest.db";
		const char* g_sz2 = "mytest2.db";
		const char* g_sz3 = "recovery_info";
#else // WIN32
		const char* g_sz = "/tmp/mytest.db";
		const char* g_sz2 = "/tmp/mytest2.db";
		const char* g_sz3 = "/tmp/recovery_info";
#endif // WIN32

	void TestNodeDB()
	{
		TestNodeDB(g_sz); // will create

		{
			NodeDB db;
			db.Open(g_sz); // test to open already-existing DB
		}
	}

	struct MiniWallet
	{
		Key::IKdf::Ptr m_pKdf;
		uint32_t m_nRunningIndex = 0;

		struct MyUtxo
		{
			Key::IDV m_Kidv;
		};

		void ToOutput(const MyUtxo& utxo, TxVectors::Perishable& txv, ECC::Scalar::Native& offset, Height h, Height hIncubation) const
		{
			ECC::Scalar::Native k;

			Output::Ptr pOut(new Output);
			pOut->m_Incubation = hIncubation;
			pOut->Create(h + 1, k, *m_pKdf, utxo.m_Kidv, *m_pKdf, true); // confidential transactions will be too slow for test in debug mode.
			txv.m_vOutputs.push_back(std::move(pOut));

			k = -k;
			offset += k;
		}

		void ToCommtiment(const MyUtxo& utxo, ECC::Point& comm, ECC::Scalar::Native& k) const
		{
			SwitchCommitment().Create(k, comm, *m_pKdf, utxo.m_Kidv);
		}

		void ToInput(const MyUtxo& utxo, TxVectors::Perishable& txv, ECC::Scalar::Native& offset) const
		{
			ECC::Scalar::Native k;
			Input::Ptr pInp(new Input);

			ToCommtiment(utxo, pInp->m_Commitment, k);

			txv.m_vInputs.push_back(std::move(pInp));
			offset += k;
		}

		typedef std::multimap<Height, MyUtxo> UtxoQueue;
		UtxoQueue m_MyUtxos;

		const MyUtxo* AddMyUtxo(const Key::IDV& kidv)
		{
			if (!kidv.m_Value)
				return NULL;

			MyUtxo utxo;
			utxo.m_Kidv = kidv;

			Height h = kidv.m_Idx; // this is our convention
			h += (Key::Type::Coinbase == kidv.m_Type) ? Rules::get().Maturity.Coinbase : Rules::get().Maturity.Std;

			return &m_MyUtxos.insert(std::make_pair(h, utxo))->second;
		}

		struct MyKernel
		{
			Amount m_Fee;
			ECC::Scalar::Native m_k;
			bool m_bUseHashlock;
			Height m_Height = 0;
			Merkle::Hash m_hvRelLock = Zero;

			void Export(TxKernel& krn) const
			{
				krn.m_Fee = m_Fee;
				krn.m_Commitment = ECC::Point::Native(ECC::Context::get().G * m_k);

				if (m_bUseHashlock)
				{
					krn.m_pHashLock.reset(new TxKernel::HashLock); // why not?
					ECC::Hash::Processor() << m_Fee << m_k >> krn.m_pHashLock->m_Preimage;
				}

				if (!(m_hvRelLock == Zero))
				{
					krn.m_pRelativeLock.reset(new TxKernel::RelativeLock);
					krn.m_pRelativeLock->m_ID = m_hvRelLock;
					krn.m_pRelativeLock->m_LockHeight = 1;
				}

				ECC::Hash::Value hv;
				krn.get_Hash(hv);
				krn.m_Signature.Sign(hv, m_k);

			}

			void Export(TxKernel::Ptr& pKrn) const
			{
				pKrn.reset(new TxKernel);
				Export(*pKrn);
			}
		};

		typedef std::vector<MyKernel> KernelList;
		KernelList m_MyKernels;

		Merkle::Hash m_hvKrnRel = Zero;

		bool MakeTx(Transaction::Ptr& pTx, Height h, Height hIncubation)
		{
			Amount val = MakeTxInput(pTx, h);
			if (!val)
				return false;

			MakeTxOutput(*pTx, h, hIncubation, val);
			return true;
		}


		Amount MakeTxInput(Transaction::Ptr& pTx, Height h)
		{
			UtxoQueue::iterator it = m_MyUtxos.begin();
			if (m_MyUtxos.end() == it)
				return 0;

			if (it->first > h)
				return 0; // not spendable yet

			pTx = std::make_shared<Transaction>();

			const MyUtxo& utxo = it->second;
			assert(utxo.m_Kidv.m_Value);

			ECC::Scalar::Native kOffset = Zero;
			ToInput(utxo, *pTx, kOffset);
			pTx->m_Offset = kOffset;

			Amount ret = utxo.m_Kidv.m_Value;
			m_MyUtxos.erase(it);

			return ret;
		}

		void MakeTxOutput(Transaction& tx, Height h, Height hIncubation, Amount val)
		{
			ECC::Scalar::Native kOffset = tx.m_Offset;

			m_MyKernels.emplace_back();
			MyKernel& mk = m_MyKernels.back();
			mk.m_Fee = 10900000;
			mk.m_bUseHashlock = 0 != (1 & h);
			mk.m_Height = h;

			if (mk.m_Fee >= val)
				mk.m_Fee = val;
			else
			{
				MyUtxo utxoOut;
				utxoOut.m_Kidv.m_Value = val - mk.m_Fee;
				utxoOut.m_Kidv.m_Idx = ++m_nRunningIndex;
				utxoOut.m_Kidv.set_Subkey(0);
				utxoOut.m_Kidv.m_Type = Key::Type::Regular;

				ToOutput(utxoOut, tx, kOffset, h, hIncubation);

				m_MyUtxos.insert(std::make_pair(h + 1 + hIncubation, utxoOut));
			}

			if (!(m_hvKrnRel == Zero))
			{
				mk.m_hvRelLock = m_hvKrnRel;
				m_hvKrnRel = Zero;
			}

			m_pKdf->DeriveKey(mk.m_k, Key::ID(++m_nRunningIndex, Key::Type::Kernel));

			TxKernel::Ptr pKrn;
			mk.Export(pKrn);
			tx.m_vKernels.push_back(std::move(pKrn));

			ECC::Scalar::Native k = -mk.m_k;
			kOffset += k;
			tx.m_Offset = kOffset;

			tx.Normalize();

			Transaction::Context::Params pars;
			Transaction::Context ctx(pars);
			ctx.m_Height.m_Min = h + 1;
			bool isTxValid = tx.IsValid(ctx);
			verify_test(isTxValid);
		}
	};

	class MyNodeProcessor1
		:public NodeProcessor
	{
	public:

		TxPool::Fluff m_TxPool;
		MiniWallet m_Wallet;

		MyNodeProcessor1()
		{
			ECC::SetRandom(m_Wallet.m_pKdf);
		}
	};

	struct BlockPlus
	{
		typedef std::unique_ptr<BlockPlus> Ptr;

		Block::SystemState::Full m_Hdr;
		ByteBuffer m_BodyP;
		ByteBuffer m_BodyE;
	};

	void TestNodeProcessor1(std::vector<BlockPlus::Ptr>& blockChain)
	{
		MyNodeProcessor1 np;
		np.m_Horizon.m_Branching = 35;
		np.Initialize(g_sz);
		np.OnTreasury(g_Treasury);

		const Height hIncubation = 3; // artificial incubation period for outputs.

		for (Height h = Rules::HeightGenesis; h < 96 + Rules::HeightGenesis; h++)
		{
			while (true)
			{
				// Spend it in a transaction
				Transaction::Ptr pTx;
				if (!np.m_Wallet.MakeTx(pTx, np.m_Cursor.m_ID.m_Height, hIncubation))
					break;

				HeightRange hr(np.m_Cursor.m_ID.m_Height + 1, MaxHeight);

				verify_test(np.ValidateTxContext(*pTx, hr));
				verify_test(np.ValidateTxWrtHeight(*pTx, hr));

				Transaction::Context::Params pars;
				Transaction::Context ctx(pars);
				ctx.m_Height = np.m_Cursor.m_Sid.m_Height + 1;
				verify_test(pTx->IsValid(ctx));

				Transaction::KeyType key;
				pTx->get_Key(key);

				np.m_TxPool.AddValidTx(std::move(pTx), ctx, key);
			}

			NodeProcessor::BlockContext bc(np.m_TxPool, 0, *np.m_Wallet.m_pKdf, *np.m_Wallet.m_pKdf);
			verify_test(np.GenerateNewBlock(bc));

			np.OnState(bc.m_Hdr, PeerID());

			Block::SystemState::ID id;
			bc.m_Hdr.get_ID(id);

			np.OnBlock(id, bc.m_BodyP, bc.m_BodyE, PeerID());
			np.TryGoUp();

			np.m_Wallet.AddMyUtxo(Key::IDV(bc.m_Fees, h, Key::Type::Comission));
			np.m_Wallet.AddMyUtxo(Key::IDV(Rules::get_Emission(h), h, Key::Type::Coinbase));

			BlockPlus::Ptr pBlock(new BlockPlus);
			pBlock->m_Hdr = std::move(bc.m_Hdr);
			pBlock->m_BodyP = std::move(bc.m_BodyP);
			pBlock->m_BodyE = std::move(bc.m_BodyE);
			blockChain.push_back(std::move(pBlock));
		}

		for (Height h = 1; h <= np.m_Cursor.m_ID.m_Height; h++)
		{
			NodeDB::StateID sid;
			sid.m_Height = h;
			sid.m_Row = np.FindActiveAtStrict(h);

			Block::Body block;
			np.ExtractBlockWithExtra(block, sid);

			// inputs must come with maturities!
			for (size_t i = 0; i < block.m_vInputs.size(); i++)
			{
				const Input& inp = *block.m_vInputs[i];
				verify_test(inp.m_Internal.m_ID && inp.m_Internal.m_Maturity);
			}
		}

	}


	void TestNodeProcessor2(std::vector<BlockPlus::Ptr>& blockChain)
	{
		NodeProcessor::Horizon horz;
		horz.m_Branching = 12;
		horz.m_Sync.Hi = 12;
		horz.m_Sync.Lo = 15;
		horz.m_Local = horz.m_Sync;

		size_t nMid = blockChain.size() / 2;

		{
			NodeProcessor np;
			np.m_Horizon = horz;
			np.Initialize(g_sz);
			np.OnTreasury(g_Treasury);

			PeerID peer;
			ZeroObject(peer);

			for (size_t i = 0; i < blockChain.size(); i += 2)
				np.OnState(blockChain[i]->m_Hdr, peer);
		}

		{
			NodeProcessor np;
			np.m_Horizon = horz;
			np.Initialize(g_sz);

			PeerID peer;
			ZeroObject(peer);

			for (size_t i = 0; i < nMid; i += 2)
			{
				Block::SystemState::ID id;
				blockChain[i]->m_Hdr.get_ID(id);
				np.OnBlock(id, blockChain[i]->m_BodyP, blockChain[i]->m_BodyE, peer);
				np.TryGoUp();
			}
		}

		{
			NodeProcessor np;
			np.m_Horizon = horz;
			np.Initialize(g_sz);

			PeerID peer;
			ZeroObject(peer);

			for (size_t i = 1; i < blockChain.size(); i += 2)
				np.OnState(blockChain[i]->m_Hdr, peer);
		}

		{
			NodeProcessor np;
			np.m_Horizon = horz;
			np.Initialize(g_sz);

			PeerID peer;
			ZeroObject(peer);

			for (size_t i = 0; i < nMid; i++)
			{
				Block::SystemState::ID id;
				blockChain[i]->m_Hdr.get_ID(id);
				np.OnBlock(id, blockChain[i]->m_BodyP, blockChain[i]->m_BodyE, peer);
				np.TryGoUp();
			}
		}

		{
			NodeProcessor np;
			np.m_Horizon = horz;
			np.Initialize(g_sz);

			PeerID peer;
			ZeroObject(peer);

			for (size_t i = nMid; i < blockChain.size(); i++)
			{
				Block::SystemState::ID id;
				blockChain[i]->m_Hdr.get_ID(id);
				np.OnBlock(id, blockChain[i]->m_BodyP, blockChain[i]->m_BodyE, peer);
				np.TryGoUp();
			}
		}

		{
			NodeProcessor np;
			np.m_Horizon = horz;

			NodeProcessor::StartParams sp;
			sp.m_CheckIntegrity = true;
			sp.m_Vacuum = true;
			sp.m_ResetCursor = true;
			np.Initialize(g_sz, sp);
		}

	}

	void TestNodeProcessor3(std::vector<BlockPlus::Ptr>& blockChain)
	{
		NodeProcessor np, npSrc;
		np.m_Horizon.m_Branching = 5;
		np.m_Horizon.m_Sync.Hi = 12;
		np.m_Horizon.m_Sync.Lo = 30;
		np.m_Horizon.m_Local = np.m_Horizon.m_Sync;
		np.Initialize(g_sz);
		np.OnTreasury(g_Treasury);

		npSrc.Initialize(g_sz2);
		npSrc.OnTreasury(g_Treasury);

		PeerID pid(Zero);

		for (size_t i = 0; i < blockChain.size(); i++)
		{
			const BlockPlus& bp = *blockChain[i];
			verify_test(np.OnState(bp.m_Hdr, pid) == NodeProcessor::DataStatus::Accepted);

			verify_test(npSrc.OnState(bp.m_Hdr, pid) == NodeProcessor::DataStatus::Accepted);

			Block::SystemState::ID id;
			bp.m_Hdr.get_ID(id);
			verify_test(npSrc.OnBlock(id, bp.m_BodyP, bp.m_BodyE, pid) == NodeProcessor::DataStatus::Accepted);
		}

		npSrc.TryGoUp();
		verify_test(npSrc.m_Cursor.m_ID.m_Height == blockChain.size());

		np.EnumCongestions();

		verify_test(np.IsFastSync()); // should go into fast-sync mode
		verify_test(np.m_SyncData.m_TxoLo); // should be used on the 1st attempt

		// 1st attempt - tamper with txlo. Remove arbitrary input
		bool bTampered = false;
		for (Height h = Rules::HeightGenesis; h <= np.m_SyncData.m_TxoLo; h++)
		{
			NodeDB::StateID sid;
			sid.m_Row = npSrc.FindActiveAtStrict(h);
			sid.m_Height = h;

			ByteBuffer bbE, bbP;
			verify_test(npSrc.GetBlock(sid, &bbE, &bbP, 0, np.m_SyncData.m_TxoLo, np.m_SyncData.m_Target.m_Height, true));

			if (!bTampered)
			{
				Deserializer der;
				der.reset(bbP);

				Block::BodyBase bbb;
				TxVectors::Perishable txvp;
				der & bbb;
				der & txvp;

				verify_test(txvp.m_vInputs.empty()); // may contain only treasury, but we don't spend it in the test

				if (!txvp.m_vOutputs.empty())
				{
					txvp.m_vOutputs.pop_back();

					Serializer ser;
					ser & bbb;
					ser & txvp;
					ser.swap_buf(bbP);

					bTampered = true;
				}
			}

			Block::SystemState::ID id;
			blockChain[h-1]->m_Hdr.get_ID(id);
			verify_test(np.OnBlock(id, bbP, bbE, pid) == NodeProcessor::DataStatus::Accepted);
		}

		np.TryGoUp();
		verify_test(np.m_Cursor.m_ID.m_Height == Rules::HeightGenesis - 1); // should fall back to start
		verify_test(!np.m_SyncData.m_TxoLo); // next attempt should be with TxLo disabled


		// 1.1 attempt - tamper with txlo. Modify block offset
		np.m_SyncData.m_TxoLo = np.m_SyncData.m_Target.m_Height / 2;
		bTampered = false;
		for (Height h = Rules::HeightGenesis; h <= np.m_SyncData.m_TxoLo; h++)
		{
			NodeDB::StateID sid;
			sid.m_Row = npSrc.FindActiveAtStrict(h);
			sid.m_Height = h;

			ByteBuffer bbE, bbP;
			verify_test(npSrc.GetBlock(sid, &bbE, &bbP, 0, np.m_SyncData.m_TxoLo, np.m_SyncData.m_Target.m_Height, true));

			if (!bTampered)
			{
				Deserializer der;
				der.reset(bbP);

				Block::BodyBase bbb;
				TxVectors::Perishable txvp;
				der & bbb;
				der & txvp;

				bbb.m_Offset.m_Value.Inc();

				Serializer ser;
				ser & bbb;
				ser & txvp;
				ser.swap_buf(bbP);

				bTampered = true;
			}

			Block::SystemState::ID id;
			blockChain[h - 1]->m_Hdr.get_ID(id);
			verify_test(np.OnBlock(id, bbP, bbE, pid) == NodeProcessor::DataStatus::Accepted);
		}

		np.TryGoUp();
		verify_test(np.m_Cursor.m_ID.m_Height == Rules::HeightGenesis - 1); // should fall back to start
		verify_test(!np.m_SyncData.m_TxoLo); // next attempt should be with TxLo disabled

		// 2nd attempt. Tamper with the non-naked output
		np.m_SyncData.m_TxoLo = np.m_SyncData.m_Target.m_Height / 2;
		bTampered = false;
		for (Height h = Rules::HeightGenesis; h <= np.m_SyncData.m_Target.m_Height; h++)
		{
			NodeDB::StateID sid;
			sid.m_Row = npSrc.FindActiveAtStrict(h);
			sid.m_Height = h;

			ByteBuffer bbE, bbP;
			verify_test(npSrc.GetBlock(sid, &bbE, &bbP, 0, np.m_SyncData.m_TxoLo, np.m_SyncData.m_Target.m_Height, true));

			if (!bTampered)
			{
				Deserializer der;
				der.reset(bbP);

				Block::BodyBase bbb;
				TxVectors::Perishable txvp;
				der & bbb;
				der & txvp;

				for (size_t j = 0; j < txvp.m_vOutputs.size(); j++)
				{
					Output& outp = *txvp.m_vOutputs[j];
					if (outp.m_pConfidential)
					{
						outp.m_pConfidential->m_P_Tag.m_pCondensed[0].m_Value.Inc();
						bTampered = true;
						break;
					}
				}

				if (bTampered)
				{
					Serializer ser;
					ser & bbb;
					ser & txvp;
					ser.swap_buf(bbP);
				}
			}

			Block::SystemState::ID id;
			blockChain[h - 1]->m_Hdr.get_ID(id);
			verify_test(np.OnBlock(id, bbP, bbE, pid) == NodeProcessor::DataStatus::Accepted);

			np.TryGoUp();

			if (bTampered)
			{
				verify_test(np.m_Cursor.m_ID.m_Height == h - 1);
				break;
			}

			verify_test(np.m_Cursor.m_ID.m_Height == h);
		}

		verify_test(bTampered);
		verify_test(np.m_SyncData.m_TxoLo);

		// 3rd attempt. enforce "naked" output. The node won't notice a problem until all the blocks are fed
		bTampered = false;

		for (Height h = np.m_Cursor.m_ID.m_Height + 1; ; h++)
		{
			NodeDB::StateID sid;
			sid.m_Row = npSrc.FindActiveAtStrict(h);
			sid.m_Height = h;

			ByteBuffer bbE, bbP;
			verify_test(npSrc.GetBlock(sid, &bbE, &bbP, 0, np.m_SyncData.m_TxoLo, np.m_SyncData.m_Target.m_Height, true));

			if (!bTampered)
			{
				Deserializer der;
				der.reset(bbP);

				Block::BodyBase bbb;
				TxVectors::Perishable txvp;
				der & bbb;
				der & txvp;

				for (size_t j = 0; j < txvp.m_vOutputs.size(); j++)
				{
					Output& outp = *txvp.m_vOutputs[j];
					if (outp.m_pConfidential || outp.m_pPublic)
					{
						outp.m_pConfidential.reset();
						outp.m_pPublic.reset();
						bTampered = true;
						break;
					}
				}

				if (bTampered)
				{
					Serializer ser;
					ser & bbb;
					ser & txvp;
					ser.swap_buf(bbP);
				}
			}

			Block::SystemState::ID id;
			blockChain[h - 1]->m_Hdr.get_ID(id);
			verify_test(np.OnBlock(id, bbP, bbE, pid) == NodeProcessor::DataStatus::Accepted);

			bool bLast = (h == np.m_SyncData.m_Target.m_Height);

			np.TryGoUp();

			if (bLast)
			{
				verify_test(np.m_Cursor.m_ID.m_Height == Rules::HeightGenesis - 1);
				break;
			}

			verify_test(np.m_Cursor.m_ID.m_Height == h);
		}

		verify_test(!np.m_SyncData.m_TxoLo);

		// 3.1 Same as above, but now TxoLo is zero, Node should not erase all the blocks on error
		Height hTampered = 0;

		for (Height h = np.m_Cursor.m_ID.m_Height + 1; ; h++)
		{
			NodeDB::StateID sid;
			sid.m_Row = npSrc.FindActiveAtStrict(h);
			sid.m_Height = h;

			ByteBuffer bbE, bbP;
			verify_test(npSrc.GetBlock(sid, &bbE, &bbP, 0, np.m_SyncData.m_TxoLo, np.m_SyncData.m_Target.m_Height, true));

			if (!hTampered)
			{
				Deserializer der;
				der.reset(bbP);

				Block::BodyBase bbb;
				TxVectors::Perishable txvp;
				der & bbb;
				der & txvp;

				for (size_t j = 0; j < txvp.m_vOutputs.size(); j++)
				{
					Output& outp = *txvp.m_vOutputs[j];
					if (outp.m_pConfidential || outp.m_pPublic)
					{
						outp.m_pConfidential.reset();
						outp.m_pPublic.reset();
						hTampered = h;
						break;
					}
				}

				if (hTampered)
				{
					Serializer ser;
					ser & bbb;
					ser & txvp;
					ser.swap_buf(bbP);
				}
			}

			Block::SystemState::ID id;
			blockChain[h - 1]->m_Hdr.get_ID(id);
			verify_test(np.OnBlock(id, bbP, bbE, pid) == NodeProcessor::DataStatus::Accepted);

			bool bLast = (h == np.m_SyncData.m_Target.m_Height);

			np.TryGoUp();

			if (bLast)
			{
				verify_test(np.m_Cursor.m_ID.m_Height == hTampered - 1);
				break;
			}

			verify_test(np.m_Cursor.m_ID.m_Height == h);
		}

		// 4th attempt. provide valid data
		for (Height h = np.m_Cursor.m_ID.m_Height + 1; h <= blockChain.size(); h++)
		{
			NodeDB::StateID sid;
			sid.m_Row = npSrc.FindActiveAtStrict(h);
			sid.m_Height = h;

			ByteBuffer bbE, bbP;
			verify_test(npSrc.GetBlock(sid, &bbE, &bbP, 0, np.m_SyncData.m_TxoLo, np.m_SyncData.m_Target.m_Height, true));

			Block::SystemState::ID id;
			blockChain[h - 1]->m_Hdr.get_ID(id);
			np.OnBlock(id, bbP, bbE, pid);
		}

		np.TryGoUp();
		verify_test(!np.IsFastSync());
		verify_test(np.m_Cursor.m_ID.m_Height == blockChain.size());
	}

	const uint16_t g_Port = 25003; // don't use the default port to prevent collisions with running nodes, beacons and etc.

	void TestNodeConversation()
	{
		// Testing configuration: Node0 <-> Node1 <-> Client.

		io::Reactor::Ptr pReactor(io::Reactor::create());
		io::Reactor::Scope scope(*pReactor);

		Node node, node2;
		node.m_Cfg.m_sPathLocal = g_sz;
		node.m_Cfg.m_Listen.port(g_Port);
		node.m_Cfg.m_Listen.ip(INADDR_ANY);
		node.m_Cfg.m_Treasury = g_Treasury;

		node.m_Cfg.m_Timeout.m_GetBlock_ms = 1000 * 60;
		node.m_Cfg.m_Timeout.m_GetState_ms = 1000 * 60;

		node2.m_Cfg.m_sPathLocal = g_sz2;
		node2.m_Cfg.m_Listen.port(g_Port + 1);
		node2.m_Cfg.m_Listen.ip(INADDR_ANY);
		node2.m_Cfg.m_Timeout = node.m_Cfg.m_Timeout;
		node2.m_Cfg.m_Treasury = g_Treasury;

		node2.m_Cfg.m_BeaconPort = g_Port;

		ECC::SetRandom(node);
		ECC::SetRandom(node2);

		node.Initialize();
		node2.Initialize();

		struct MyClient
			:public proto::NodeConnection
		{
			Node* m_ppNode[2];
			unsigned int m_iNode;
			unsigned int m_WaitingCycles;

			Height m_HeightMax;
			const Height m_HeightTrg = 70;

			MyClient()
			{
				m_pTimer = io::Timer::create(io::Reactor::get_Current());
			}

			virtual void OnConnectedSecure() override {
				OnTimer();
			}

			virtual void OnDisconnect(const DisconnectReason&) override {
				fail_test("OnDisconnect");
			}

			io::Timer::Ptr m_pTimer;

			void OnTimer() {


				if (m_HeightMax < m_HeightTrg)
				{
					Node& n = *m_ppNode[m_iNode];

					TxPool::Fluff txPool; // empty, no transactions
					NodeProcessor::BlockContext bc(txPool, 0, *n.m_Keys.m_pMiner, *n.m_Keys.m_pMiner);

					verify_test(n.get_Processor().GenerateNewBlock(bc));

					n.get_Processor().OnState(bc.m_Hdr, PeerID());

					Block::SystemState::ID id;
					bc.m_Hdr.get_ID(id);

					n.get_Processor().OnBlock(id, bc.m_BodyP, bc.m_BodyE, PeerID());
					n.get_Processor().TryGoUp();

					m_HeightMax = std::max(m_HeightMax, bc.m_Hdr.m_Height);

					printf("Mined block Height = %u, node = %u \n", (unsigned int) bc.m_Hdr.m_Height, (unsigned int)m_iNode);

					++m_iNode %= _countof(m_ppNode);
				}
				else
					if (m_WaitingCycles++ > 60)
					{
						fail_test("Blockchain height didn't reach target");
						io::Reactor::get_Current().stop();
					}

				SetTimer(100);
			}

			virtual void OnMsg(proto::NewTip&& msg) override
			{
				printf("Tip Height=%u\n", (unsigned int) msg.m_Description.m_Height);
				verify_test(msg.m_Description.m_Height <= m_HeightMax);
				if (msg.m_Description.m_Height == m_HeightTrg)
					io::Reactor::get_Current().stop();
			}

			void SetTimer(uint32_t timeout_ms) {
				m_pTimer->start(timeout_ms, false, [this]() { return (this->OnTimer)(); });
			}
		};

		MyClient cl;
		cl.m_iNode = 0;
		cl.m_HeightMax = 0;
		cl.m_WaitingCycles = 0;
		cl.m_ppNode[0] = &node;
		cl.m_ppNode[1] = &node2;

		io::Address addr;
		addr.resolve("127.0.0.1");
		addr.port(g_Port + 1);

		cl.Connect(addr);


		pReactor->run();

		node.GenerateRecoveryInfo(g_sz3);

		RecoveryInfo::Reader rp;

		Block::SystemState::Full sTip;
		rp.Open(g_sz3);

		uint32_t nUnrecognized = 0;
		while (true)
		{
			RecoveryInfo::Entry x;
			if (!rp.Read(x))
				break;

			Key::IDV kidv;
			bool b1 = x.m_Output.Recover(x.m_CreateHeight, *node.m_Keys.m_pOwner, kidv);
			bool b2 = x.m_Output.Recover(x.m_CreateHeight, *node2.m_Keys.m_pOwner, kidv);
			if (!(b1 || b2))
			{
				verify_test(!x.m_CreateHeight); // treasury
				nUnrecognized++;
				verify_test(nUnrecognized <= 1);
			}
		}

		rp.Finalyze(); // final verification

		DeleteFile(g_sz3);
	}



	void TestNodeClientProto()
	{
		// Testing configuration: Node <-> Client. Node is a miner

		io::Reactor::Ptr pReactor(io::Reactor::create());
		io::Reactor::Scope scope(*pReactor);

		Node node;
		node.m_Cfg.m_sPathLocal = g_sz;
		node.m_Cfg.m_Listen.port(g_Port);
		node.m_Cfg.m_Listen.ip(INADDR_ANY);
		node.m_Cfg.m_TestMode.m_FakePowSolveTime_ms = 100;
		node.m_Cfg.m_MiningThreads = 1;

		ECC::SetRandom(node);

		node.m_Cfg.m_Horizon.m_Branching = 6;
		node.m_Cfg.m_Horizon.m_Sync.Hi = 10;
		node.m_Cfg.m_Horizon.m_Sync.Lo = 14;
		node.m_Cfg.m_Horizon.m_Local = node.m_Cfg.m_Horizon.m_Sync;
		node.m_Cfg.m_VerificationThreads = -1;

		node.m_Cfg.m_Dandelion.m_AggregationTime_ms = 0;
		node.m_Cfg.m_Dandelion.m_OutputsMin = 3;
		node.m_Cfg.m_Dandelion.m_DummyLifetimeLo = 5;
		node.m_Cfg.m_Dandelion.m_DummyLifetimeHi = 10;

		struct MyClient
			:public proto::NodeConnection
		{
			const Height m_HeightTrg = 75;

			MiniWallet m_Wallet;

			std::vector<Block::SystemState::Full> m_vStates;

			std::set<ECC::Point> m_UtxosConfirmed;
			std::list<ECC::Point> m_queProofsExpected;
			std::list<uint32_t> m_queProofsStateExpected;
			std::list<uint32_t> m_queProofsKrnExpected;
			uint32_t m_nChainWorkProofsPending = 0;
			uint32_t m_nBbsMsgsPending = 0;
			uint32_t m_nRecoveryPending = 0;
			AssetID m_AssetEmitted = Zero;
			bool m_bCustomAssetRecognized = false;


			MyClient(const Key::IKdf::Ptr& pKdf)
			{
				m_Wallet.m_pKdf = pKdf;
				m_pTimer = io::Timer::create(io::Reactor::get_Current());
			}

			virtual void OnConnectedSecure() override
			{
				SetTimer(90 * 1000);
				SendLogin();

				Send(proto::GetExternalAddr(Zero));
			}

			virtual void OnMsg(proto::Authentication&& msg) override
			{
				proto::NodeConnection::OnMsg(std::move(msg));

				switch (msg.m_IDType)
				{
				case proto::IDType::Node:
					ProveKdfObscured(*m_Wallet.m_pKdf, proto::IDType::Owner);
					break;

				case proto::IDType::Viewer:
					verify_test(IsPKdfObscured(*m_Wallet.m_pKdf, msg.m_ID));
					break;

				default: // suppress warning
					break;
				}
			}

			virtual void OnMsg(proto::ExternalAddr&& msg) override
			{
			}

			virtual void OnDisconnect(const DisconnectReason&) override {
				fail_test("OnDisconnect");
				io::Reactor::get_Current().stop();
			}

			io::Timer::Ptr m_pTimer;

			bool IsHeightReached() const
			{
				return !m_vStates.empty() && (m_vStates.back().m_Height >= m_HeightTrg);
			}

			bool IsAllProofsReceived() const
			{
				return
					m_queProofsExpected.empty() &&
					m_queProofsKrnExpected.empty() &&
					m_queProofsStateExpected.empty() &&
					!m_nChainWorkProofsPending;
			}

			bool IsAllBbsReceived() const
			{
				return !m_nBbsMsgsPending;
			}

			bool IsAllRecoveryReceived() const
			{
				return !m_nRecoveryPending;
			}

			void OnTimer() {

				io::Reactor::get_Current().stop();
			}

			virtual void OnMsg(proto::NewTip&& msg) override
			{
				if (!msg.m_Description.m_Height)
					return; // skip the treasury-received notification

				printf("Tip Height=%u\n", (unsigned int) msg.m_Description.m_Height);
				verify_test(m_vStates.size() + 1 == msg.m_Description.m_Height);

				m_vStates.push_back(msg.m_Description);

				if (IsHeightReached())
				{
					if (IsAllProofsReceived() && IsAllBbsReceived() && IsAllRecoveryReceived() /* && m_bCustomAssetRecognized*/)
						io::Reactor::get_Current().stop();
					return;
				}

				proto::BbsMsg msgBbs;
				msgBbs.m_Channel = 11;
				msgBbs.m_TimePosted = getTimestamp();
				msgBbs.m_Message.resize(1);
				msgBbs.m_Message[0] = (uint8_t) msg.m_Description.m_Height;
				Send(msgBbs);

				m_nBbsMsgsPending++;

				// assume we've mined this
				m_Wallet.AddMyUtxo(Key::IDV(Rules::get_Emission(msg.m_Description.m_Height), msg.m_Description.m_Height, Key::Type::Coinbase));

				for (size_t i = 0; i + 1 < m_vStates.size(); i++)
				{
					proto::GetProofState msgOut2;
					msgOut2.m_Height = i + Rules::HeightGenesis;
					Send(msgOut2);

					m_queProofsStateExpected.push_back((uint32_t) i);
				}

				if (m_vStates.size() > 1)
				{
					proto::GetCommonState msgOut2;
					msgOut2.m_IDs.resize(m_vStates.size() - 1);

					for (size_t i = 0; i < m_vStates.size() - 1; i++)
						m_vStates[m_vStates.size() - i - 2].get_ID(msgOut2.m_IDs[i]);

					Send(msgOut2);
				}

				for (auto it = m_Wallet.m_MyUtxos.begin(); m_Wallet.m_MyUtxos.end() != it; it++)
				{
					const MiniWallet::MyUtxo& utxo = it->second;

					proto::GetProofUtxo msgOut2;

					ECC::Scalar::Native sk;
					m_Wallet.ToCommtiment(utxo, msgOut2.m_Utxo, sk);

					Send(msgOut2);

					m_queProofsExpected.push_back(msgOut2.m_Utxo);
				}

				for (uint32_t i = 0; i < m_Wallet.m_MyKernels.size(); i++)
				{
					const MiniWallet::MyKernel mk = m_Wallet.m_MyKernels[i];

					TxKernel krn;
					mk.Export(krn);

					proto::GetProofKernel2 msgOut2;
					krn.get_ID(msgOut2.m_ID);
					msgOut2.m_Fetch = true;
					Send(msgOut2);

					m_queProofsKrnExpected.push_back(i);

					proto::GetProofKernel msgOut3;
					krn.get_ID(msgOut3.m_ID);
					Send(msgOut3);

					m_queProofsKrnExpected.push_back(i);
				}

				{
					proto::GetProofChainWork msgOut2;
					Send(msgOut2);
					m_nChainWorkProofsPending++;
				}

				proto::NewTransaction msgTx;
				while (true)
				{
					Amount val = m_Wallet.MakeTxInput(msgTx.m_Transaction, msg.m_Description.m_Height);
					if (!val)
						break;

					assert(msgTx.m_Transaction);

					bool bEmitAsset = (m_AssetEmitted == Zero);

					if (bEmitAsset)
					{
						Key::IDV kidv(Zero);
						kidv.m_Value = val;

						ECC::Scalar::Native skAsset, skOut;
						ECC::SetRandom(skAsset);
						proto::Sk2Pk(m_AssetEmitted, skAsset);

						TxKernel::Ptr pKrn(new TxKernel);
						pKrn->m_Commitment.m_X = m_AssetEmitted;
						pKrn->m_Commitment.m_Y = 0;
						pKrn->m_AssetEmission = kidv.m_Value;
						pKrn->Sign(skAsset);

						Output::Ptr pOutp(new Output);
						pOutp->m_AssetID = m_AssetEmitted;
						pOutp->Create(msg.m_Description.m_Height + 1, skOut, *m_Wallet.m_pKdf, kidv, *m_Wallet.m_pKdf);

						skAsset += skOut;
						skAsset = -skAsset;
						skAsset += msgTx.m_Transaction->m_Offset;
						msgTx.m_Transaction->m_Offset = skAsset;

						msgTx.m_Transaction->m_vOutputs.push_back(std::move(pOutp));
						msgTx.m_Transaction->m_vKernels.push_back(std::move(pKrn));

						msgTx.m_Transaction->Normalize();
					}

					if (!(bEmitAsset && Rules::get().CA.Deposit))
						m_Wallet.MakeTxOutput(*msgTx.m_Transaction, msg.m_Description.m_Height, 2, val);

					Transaction::Context::Params pars;
					Transaction::Context ctx(pars);
					ctx.m_Height.m_Min = msg.m_Description.m_Height + 1;
					verify_test(msgTx.m_Transaction->IsValid(ctx));

					Send(msgTx);
				}

				proto::GetUtxoEvents msgEvt;
				Send(msgEvt);
				m_nRecoveryPending++;

				if (!(msg.m_Description.m_Height % 4))
				{
					// switch offline/online mining modes
					m_MiningFinalization = ((msg.m_Description.m_Height % 8) != 0);
					SendLogin();
				}

			}

			bool m_MiningFinalization = false;

			virtual void SetupLogin(proto::Login& msg) override
			{
				if (m_MiningFinalization)
					msg.m_Flags |= proto::LoginFlags::MiningFinalization;
			}

			virtual void OnMsg(proto::ProofState&& msg) override
			{
				if (!m_queProofsStateExpected.empty())
				{
					const Block::SystemState::Full& s = m_vStates[m_queProofsStateExpected.front()];
					Block::SystemState::ID id;
					s.get_ID(id);

					verify_test(m_vStates.back().IsValidProofState(id, msg.m_Proof));

					m_queProofsStateExpected.pop_front();
				}
				else
					fail_test("unexpected proof");
			}

			virtual void OnMsg(proto::ProofCommonState&& msg) override
			{
				verify_test(!m_vStates.empty());
				verify_test(m_vStates.back().IsValidProofState(msg.m_ID, msg.m_Proof));
			}

			virtual void OnMsg(proto::ProofUtxo&& msg) override
			{
				if (!m_queProofsExpected.empty())
				{
					const ECC::Point& comm = m_queProofsExpected.front();

					auto it = m_UtxosConfirmed.find(comm);

					if (msg.m_Proofs.empty())
						verify_test(m_UtxosConfirmed.end() == it);
					else
					{
						for (uint32_t j = 0; j < msg.m_Proofs.size(); j++)
							verify_test(m_vStates.back().IsValidProofUtxo(comm, msg.m_Proofs[j]));

						if (m_UtxosConfirmed.end() == it)
							m_UtxosConfirmed.insert(comm);
					}

					m_queProofsExpected.pop_front();
				}
				else
					fail_test("unexpected proof");
			}

			virtual void OnMsg(proto::ProofKernel2&& msg) override
			{
				if (!m_queProofsKrnExpected.empty())
				{
					m_queProofsKrnExpected.pop_front();

					if (!msg.m_Proof.empty())
					{
						verify_test(msg.m_Kernel);

						AmountBig::Type fee;
						ECC::Point::Native exc;
						verify_test(msg.m_Kernel->IsValid(msg.m_Height, fee, exc));

						Merkle::Hash hv;
						msg.m_Kernel->get_ID(hv);
						Merkle::Interpret(hv, msg.m_Proof);

						verify_test(msg.m_Height <= m_vStates.size());
						const Block::SystemState::Full& s = m_vStates[msg.m_Height - 1];
						verify_test(s.m_Height == msg.m_Height);

						verify_test(s.m_Kernels == hv);
					}
				}
				else
					fail_test("unexpected proof");
			}

			virtual void OnMsg(proto::ProofKernel&& msg) override
			{
				if (!m_queProofsKrnExpected.empty())
				{
					const MiniWallet::MyKernel& mk = m_Wallet.m_MyKernels[m_queProofsKrnExpected.front()];
					m_queProofsKrnExpected.pop_front();

					if (!msg.m_Proof.empty())
					{
						TxKernel krn;
						mk.Export(krn);
						verify_test(m_vStates.back().IsValidProofKernel(krn, msg.m_Proof));

						krn.get_ID(m_Wallet.m_hvKrnRel);
					}
				}
				else
					fail_test("unexpected proof");
			}

			virtual void OnMsg(proto::ProofChainWork&& msg) override
			{
				verify_test(m_nChainWorkProofsPending);
				verify_test(!m_vStates.empty() && (msg.m_Proof.m_Heading.m_Prefix.m_Height + msg.m_Proof.m_Heading.m_vElements.size() - 1 == m_vStates.back().m_Height));
				verify_test(msg.m_Proof.IsValid());
				m_nChainWorkProofsPending--;
			}

			virtual void OnMsg(proto::UtxoEvents&& msg) override
			{
				verify_test(m_nRecoveryPending);
				m_nRecoveryPending--;

				verify_test(!msg.m_Events.empty());

				for (size_t i = 0; i < msg.m_Events.size(); i++)
				{
					const proto::UtxoEvent& evt = msg.m_Events[i];

					if (!(evt.m_AssetID == Zero))
					{
						verify_test(evt.m_AssetID == m_AssetEmitted);
						m_bCustomAssetRecognized = true;
					}

					ECC::Scalar::Native sk;
					ECC::Point comm;
					SwitchCommitment(&evt.m_AssetID).Create(sk, comm, *m_Wallet.m_pKdf, evt.m_Kidv);
					verify_test(comm == evt.m_Commitment);

				}
			}

			virtual void OnMsg(proto::GetBlockFinalization&& msg) override
			{
				Block::Builder bb(0, *m_Wallet.m_pKdf, *m_Wallet.m_pKdf, msg.m_Height);
				bb.AddCoinbaseAndKrn();
				bb.AddFees(msg.m_Fees);

				proto::BlockFinalization msgOut;
				msgOut.m_Value.reset(new Transaction);
				bb.m_Txv.MoveInto(*msgOut.m_Value);
				msgOut.m_Value->m_Offset = -bb.m_Offset;
				msgOut.m_Value->Normalize();

				Send(msgOut);
			}

			void SetTimer(uint32_t timeout_ms) {
				m_pTimer->start(timeout_ms, false, [this]() { return (this->OnTimer)(); });
			}
		};

		MyClient cl(node.m_Keys.m_pMiner);

		io::Address addr;
		addr.resolve("127.0.0.1");
		addr.port(g_Port);

		node.m_Cfg.m_Treasury = g_Treasury;
		node.Initialize();

		cl.Connect(addr);


		struct MyClient2
			:public proto::NodeConnection
		{
			MyClient* m_pOtherClient;

			virtual void OnConnectedSecure() override
			{
				SendLogin();
			}

			virtual void SetupLogin(proto::Login& msg) override
			{
				msg.m_Flags |= proto::LoginFlags::SendPeers;
			}

			virtual void OnDisconnect(const DisconnectReason&) override {
				fail_test("OnDisconnect");
			}

			virtual void OnMsg(proto::NewTip&& msg) override {
				if (msg.m_Description.m_Height == 10)
				{
					proto::BbsSubscribe msgOut;
					msgOut.m_Channel = 11;
					msgOut.m_On = true;

					Send(msgOut);
				}
			}

			uint32_t m_MsgCount = 0;

			virtual void OnMsg(proto::BbsMsg&& msg) override {
				OnBbsMsg(msg.m_Message);
			}

			void OnBbsMsg(const ByteBuffer& msg)
			{
				verify_test(msg.size() == 1);
				uint8_t nMsg = msg[0];

				verify_test(nMsg == (uint8_t) m_MsgCount + 1);
				m_MsgCount++;

				verify_test(m_pOtherClient->m_nBbsMsgsPending);
				m_pOtherClient->m_nBbsMsgsPending--;

				printf("Got BBS msg=%u\n", m_MsgCount);
			}
		};

		MyClient2 cl2;
		cl2.m_pOtherClient = &cl;
		cl2.Connect(addr);


		Node node2;
		node2.m_Cfg.m_sPathLocal = g_sz2;
		node2.m_Cfg.m_Connect.resize(1);
		node2.m_Cfg.m_Connect[0].resolve("127.0.0.1");
		node2.m_Cfg.m_Connect[0].port(g_Port);
		node2.m_Cfg.m_Timeout = node.m_Cfg.m_Timeout;

		node2.m_Cfg.m_Dandelion = node.m_Cfg.m_Dandelion;

		ECC::SetRandom(node2);
		node2.Initialize();
		verify_test(node2.get_AcessiblePeerCount() == 1);

		pReactor->run();


		if (!cl.IsHeightReached())
			fail_test("Blockchain height didn't reach target");
		if (!cl.IsAllProofsReceived())
			fail_test("some proofs missing");
		if (!cl.IsAllBbsReceived())
			fail_test("some BBS messages missing");
		if (!cl.IsAllRecoveryReceived())
			fail_test("some recovery messages missing");
		//if (!cl.m_bCustomAssetRecognized)
		//	fail_test("CA not recognized");

		struct TxoRecover
			:public NodeProcessor::ITxoRecover
		{
			uint32_t m_Recovered = 0;

			TxoRecover(NodeProcessor& x) :NodeProcessor::ITxoRecover(x) {}

			virtual bool OnTxo(const NodeDB::WalkerTxo&, Height hCreate, Output&, const Key::IDV& kidv) override
			{
				m_Recovered++;
				return true;
			}
		};

		TxoRecover wlk(node.get_Processor());
		node2.get_Processor().EnumTxos(wlk);

		node.get_Processor().RescanOwnedTxos();

		verify_test(wlk.m_Recovered);
	}


	void TestChainworkProof()
	{
		printf("Preparing blockchain ...\n");

		MiniBlockChain cc;
		cc.Generate(200000);

		const Block::SystemState::Full& sRoot = cc.m_vStates.back().m_Hdr;

		Block::ChainWorkProof cwp;
		cwp.m_hvRootLive = cc.m_hvLive;
		cwp.Create(cc.m_Source, sRoot);

		uint32_t nStates = (uint32_t) cc.m_vStates.size();
		for (size_t i0 = 0; ; i0++)
		{
			Block::SystemState::Full sTip;
			verify_test(cwp.IsValid(&sTip));
			verify_test(sRoot == sTip);

			printf("Blocks = %u. Proof: States = %u/%u, Hashes = %u, Size = %u\n",
				nStates,
				uint32_t(cwp.m_Heading.m_vElements.size()),
				uint32_t(cwp.m_vArbitraryStates.size()),
				uint32_t(cwp.m_Proof.m_vData.size()),
				uint32_t(sizeof(Block::SystemState::Sequence::Prefix) + cwp.m_Heading.m_vElements.size() * sizeof(Block::SystemState::Sequence::Element) + cwp.m_vArbitraryStates.size() * sizeof(Block::SystemState::Full) + cwp.m_Proof.m_vData.size() * sizeof(Merkle::Hash))
				);

			nStates >>= 1;
			if (nStates < 64)
				break;

			Block::ChainWorkProof cwp2;
			cwp2.m_LowerBound = cc.m_vStates[cc.m_vStates.size() - nStates].m_Hdr.m_ChainWork;
			verify_test(cwp2.Crop(cwp));

			cwp.m_LowerBound = cwp2.m_LowerBound;
			verify_test(cwp.Crop());

			verify_test(cwp2.IsValid(&sTip));
			verify_test(sRoot == sTip);
			verify_test(cwp.IsValid(&sTip));
			verify_test(sRoot == sTip);
		}
	}

	void RaiseHeightTo(Node& node, Height h)
	{
		TxPool::Fluff txPool;

		while (node.get_Processor().m_Cursor.m_ID.m_Height < h)
		{
			NodeProcessor::BlockContext bc(txPool, 0, *node.m_Keys.m_pMiner, *node.m_Keys.m_pMiner);
			verify_test(node.get_Processor().GenerateNewBlock(bc));
			node.get_Processor().OnState(bc.m_Hdr, PeerID());

			Block::SystemState::ID id;
			bc.m_Hdr.get_ID(id);
			node.get_Processor().OnBlock(id, bc.m_BodyP, bc.m_BodyE, PeerID());
			node.get_Processor().TryGoUp();
		}
	}

	void TestFlyClient()
	{
		io::Reactor::Ptr pReactor(io::Reactor::create());
		io::Reactor::Scope scope(*pReactor);

		Node node;
		node.m_Cfg.m_sPathLocal = g_sz;
		node.m_Cfg.m_Listen.port(g_Port);
		node.m_Cfg.m_Listen.ip(INADDR_ANY);
		node.m_Cfg.m_MiningThreads = 0;
		node.m_Cfg.m_Treasury = g_Treasury;

		ECC::SetRandom(node);

		node.Initialize();

		struct MyFlyClient
			:public proto::FlyClient
			,public proto::FlyClient::Request::IHandler
			,public proto::FlyClient::IBbsReceiver
		{
			io::Timer::Ptr m_pTimer;

			bool m_bRunning;
			bool m_bTip;
			Height m_hRolledTo;
			uint32_t m_nProofsExpected;
			BbsChannel m_LastBbsChannel = 0;
			bool m_bBbsReceived;
			Block::SystemState::HistoryMap m_Hist;

			MyFlyClient()
			{
				m_pTimer = io::Timer::create(io::Reactor::get_Current());
			}

			virtual Block::SystemState::IHistory& get_History() override
			{
				return m_Hist;
			}

			virtual void OnNewTip() override
			{
				m_bTip = true;
				MaybeStop();
			}

			void MaybeStop()
			{
				if (m_bRunning && m_bTip && !m_nProofsExpected && m_bBbsReceived)
				{
					io::Reactor::get_Current().stop();
					m_bRunning = false;
				}
			}

			virtual void OnRolledBack() override
			{
				m_hRolledTo = m_Hist.m_Map.empty() ? 0 : m_Hist.m_Map.rbegin()->first;
			}

			void OnTimer() {
				io::Reactor::get_Current().stop();
			}

			void SetTimer(uint32_t timeout_ms) {
				m_pTimer->start(timeout_ms, false, [this]() { return (this->OnTimer)(); });
			}

			void KillTimer() {
				m_pTimer->cancel();
			}

			virtual void OnComplete(Request& r) override
			{
				verify_test(this == r.m_pTrg);
				verify_test(m_nProofsExpected);
				m_nProofsExpected--;
				MaybeStop();
			}

			virtual void OnMsg(proto::BbsMsg&&) override
			{
				m_bBbsReceived = true;
				MaybeStop();
			}

			void SyncSync() // synchronize synchronously. Joky joke.
			{
				m_bTip = false;
				m_hRolledTo = MaxHeight;
				m_nProofsExpected = 0;
				m_bBbsReceived = false;
				++m_LastBbsChannel;

				NetworkStd net(*this);

							io::Address addr;
							addr.resolve("127.0.0.1");
							addr.port(g_Port);
				net.m_Cfg.m_vNodes.resize(4, addr); // create several connections, let the compete

				net.Connect();

				// request several proofs
				for (uint32_t i = 0; i < 10; i++)
				{
					RequestUtxo::Ptr pUtxo(new RequestUtxo);
					net.PostRequest(*pUtxo, *this);

					if (1 & i)
						pUtxo->m_pTrg = NULL;
					else
						m_nProofsExpected++;

					RequestKernel::Ptr pKrnl(new RequestKernel);
					net.PostRequest(*pKrnl, *this);

					if (1 & i)
						pKrnl->m_pTrg = NULL;
					else
						m_nProofsExpected++;

					RequestBbsMsg::Ptr pBbs(new RequestBbsMsg);
					pBbs->m_Msg.m_Channel = m_LastBbsChannel;
					pBbs->m_Msg.m_TimePosted = getTimestamp();
					net.PostRequest(*pBbs, *this);
					m_nProofsExpected++;
				}

				net.BbsSubscribe(m_LastBbsChannel, 0, this);

				SetTimer(90 * 1000);
				m_bRunning = true;
				io::Reactor::get_Current().run();
				KillTimer();
			}
		};

		const Height hThrd1 = 250;
		RaiseHeightTo(node, hThrd1);


		MyFlyClient fc;
		// simple case
		fc.SyncSync();

		verify_test(fc.m_bTip);
		verify_test(fc.m_hRolledTo == MaxHeight);
		verify_test(!fc.m_Hist.m_Map.empty() && fc.m_Hist.m_Map.rbegin()->second.m_Height == hThrd1);

		{
			// pop last
			auto it = fc.m_Hist.m_Map.rbegin();
			fc.m_Hist.m_Map.erase((++it).base());
		}

		fc.SyncSync(); // should be trivial
		verify_test(fc.m_bTip);
		verify_test(fc.m_hRolledTo == MaxHeight);
		verify_test(!fc.m_Hist.m_Map.empty() && fc.m_Hist.m_Map.rbegin()->second.m_Height == hThrd1);

		const Height hThrd2 = 270;
		RaiseHeightTo(node, hThrd2);

		// should only fill the gap to the tip, not from the beginning. Should involve
		fc.SyncSync();
		verify_test(fc.m_bTip);
		verify_test(fc.m_hRolledTo == MaxHeight);
		verify_test(!fc.m_Hist.m_Map.empty() && fc.m_Hist.m_Map.rbegin()->second.m_Height == hThrd2);

		// simulate branching, make it rollback
		Height hBranch = 203;
		fc.m_Hist.DeleteFrom(hBranch + 1);

		Block::SystemState::Full s1;
		ZeroObject(s1);
		s1.m_Height = hBranch + 2;
		fc.m_Hist.m_Map[s1.m_Height] = s1;

		fc.SyncSync();

		verify_test(fc.m_bTip);
		verify_test(fc.m_hRolledTo <= hBranch); // must rollback beyond the manually appended state
		verify_test(!fc.m_Hist.m_Map.empty() && fc.m_Hist.m_Map.rbegin()->second.m_Height == hThrd2);
	}

	void TestHalving()
	{
		HeightRange hr;
		for (hr.m_Min = Rules::HeightGenesis; ; hr.m_Min++)
		{
			Amount v = Rules::get_Emission(hr.m_Min);
			if (!v)
				break;

			AmountBig::Type sum0(v);

			uint32_t nZeroTest = 200;
			for (hr.m_Max = hr.m_Min; nZeroTest; )
			{
				AmountBig::Type sum1;
				Rules::get_Emission(sum1, hr);
				verify_test(sum0 == sum1);

				v = Rules::get_Emission(++hr.m_Max);
				if (v)
					sum0 += uintBigFrom(v);
				else
					nZeroTest--;
			}
		}
	}

}

int main()
{
	//auto logger = beam::Logger::create(LOG_LEVEL_DEBUG, LOG_LEVEL_DEBUG);
	beam::PrintEmissionSchedule();

	beam::Rules::get().AllowPublicUtxos = true;
	beam::Rules::get().FakePoW = true;
	beam::Rules::get().MaxRollback = 10;
	beam::Rules::get().DA.WindowWork = 35;
	beam::Rules::get().Maturity.Coinbase = 35; // lowered to see more txs
	beam::Rules::get().Emission.Drop0 = 5;
	beam::Rules::get().Emission.Drop1 = 8;
	beam::Rules::get().CA.Enabled = true;
	beam::Rules::get().Maturity.Coinbase = 10;
	beam::Rules::get().pForks[1].m_Height = 16;
	beam::Rules::get().UpdateChecksum();

	beam::PrepareTreasury();

	beam::TestHalving();
	beam::TestChainworkProof();

	// Make sure this test doesn't run in parallel. We have the following potential collisions for Nodes:
	//	.db files
	//	ports, wrong beacon and etc.
	verify_test(beam::helpers::ProcessWideLock("/tmp/BEAM_node_test_lock"));

	beam::DeleteFile(beam::g_sz);
	beam::DeleteFile(beam::g_sz2);

	printf("NodeDB test...\n");
	fflush(stdout);

	beam::TestNodeDB();
	beam::DeleteFile(beam::g_sz);

	{
		printf("NodeProcessor test1...\n");
		fflush(stdout);


		std::vector<beam::BlockPlus::Ptr> blockChain;
		beam::TestNodeProcessor1(blockChain);
		beam::DeleteFile(beam::g_sz);
		beam::DeleteFile(beam::g_sz2);

		printf("NodeProcessor test2...\n");
		fflush(stdout);

		beam::TestNodeProcessor2(blockChain);
		beam::DeleteFile(beam::g_sz);

		printf("NodeProcessor test3...\n");
		fflush(stdout);

		beam::TestNodeProcessor3(blockChain);
		beam::DeleteFile(beam::g_sz);
		beam::DeleteFile(beam::g_sz2);
	}

	printf("NodeX2 concurrent test...\n");
	fflush(stdout);

	beam::TestNodeConversation();
	beam::DeleteFile(beam::g_sz);
	beam::DeleteFile(beam::g_sz2);

	printf("Node <---> Client test (with proofs)...\n");
	fflush(stdout);

	beam::TestNodeClientProto();
	beam::DeleteFile(beam::g_sz);
	beam::DeleteFile(beam::g_sz2);

	printf("Node <---> FlyClient test...\n");
	fflush(stdout);

	beam::TestFlyClient();
	beam::DeleteFile(beam::g_sz);

	return g_TestsFailed ? -1 : 0;
}
