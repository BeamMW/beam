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
#include "../../core/block_crypt.h"
#include "../../utility/serialize.h"
#include "../../utility/test_helpers.h"
#include "../../core/serialization_adapters.h"

#define LOG_VERBOSE_ENABLED 0
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

		Blob bBody("body", 4);
		Merkle::Hash peer, peer2;
		memset(peer.m_pData, 0x66, peer.nBytes);

		db.SetStateBlock(pRows[0], bBody);
		verify_test(!db.get_Peer(pRows[0], peer2));

		db.set_Peer(pRows[0], &peer);
		verify_test(db.get_Peer(pRows[0], peer2));
		verify_test(peer == peer2);

		db.set_Peer(pRows[0], NULL);
		verify_test(!db.get_Peer(pRows[0], peer2));

		ByteBuffer bbBody, bbRollback;
		db.GetStateBlock(pRows[0], bbBody, bbRollback);
		db.SetStateRollback(pRows[0], bBody);
		db.GetStateBlock(pRows[0], bbBody, bbRollback);

		db.DelStateBlock(pRows[0]);
		db.GetStateBlock(pRows[0], bbBody, bbRollback);

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

		db.SetMined(sid, 200000);
		db.SetMined(sid, 250000);

		{
			NodeDB::WalkerMined wlkMined(db);
			db.EnumMined(wlkMined, 0);
			verify_test(wlkMined.MoveNext());
			verify_test(wlkMined.m_Sid.m_Height == sid.m_Height && wlkMined.m_Sid.m_Row == sid.m_Row);
			verify_test(wlkMined.m_Amount == 250000);
			verify_test(!wlkMined.MoveNext());
		}

		db.DeleteMinedSafe(sid);
		{
			NodeDB::WalkerMined wlkMined(db);
			db.EnumMined(wlkMined, 0);
			verify_test(!wlkMined.MoveNext());
		}

		db.DeleteMinedSafe(sid);

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
			wlkbbs.m_Data.m_TimePosted = 0;
			for (db.EnumBbs(wlkbbs); wlkbbs.MoveNext(); )
				;
		}

		db.BbsDelOld(267);

		for (wlkbbs.m_Data.m_Channel = 0; wlkbbs.m_Data.m_Channel < 7; wlkbbs.m_Data.m_Channel++)
		{
			wlkbbs.m_Data.m_TimePosted = 0;
			for (db.EnumBbs(wlkbbs); wlkbbs.MoveNext(); )
				;
		}

		for (db.EnumAllBbs(wlkbbs); wlkbbs.MoveNext(); )
			;

		Merkle::Hash hv;
		Blob b0(hv);
		hv = 345U;

		db.InsertDummy(176, b0);

		hv = 346U;
		db.InsertDummy(568, b0);

		Height h1;

		uint64_t rowid = db.FindDummy(h1, b0);
		verify_test(rowid);
		verify_test(h1 == 176);
		verify_test(hv == Merkle::Hash(345U));

		db.SetDummyHeight(rowid, 1055);

		rowid = db.FindDummy(h1, b0);
		verify_test(rowid);
		verify_test(h1 == 568);
		verify_test(hv == Merkle::Hash(346U));
		
		db.DeleteDummy(rowid);

		rowid = db.FindDummy(h1, b0);
		verify_test(rowid);
		verify_test(h1 == 1055);
		verify_test(hv == Merkle::Hash(345U));

		db.DeleteDummy(rowid);

		verify_test(!db.FindDummy(h1, b0));

		tr.Commit();
	}

#ifdef WIN32
		const char* g_sz = "mytest.db";
		const char* g_sz2 = "mytest2.db";
		const char* g_sz3 = "macroblock_";
#else // WIN32
		const char* g_sz = "/tmp/mytest.db";
		const char* g_sz2 = "/tmp/mytest2.db";
		const char* g_sz3 = "/tmp/macroblock_";
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
		uint32_t m_nKernelSubIdx = 1;

		struct MyUtxo
		{
			ECC::Scalar m_Key;
			Amount m_Value;

			void ToOutput(TxVectors& txv, ECC::Scalar::Native& offset, Height hIncubation) const
			{
				ECC::Scalar::Native k = m_Key;

				Output::Ptr pOut(new Output);
				pOut->m_Incubation = hIncubation;
				pOut->Create(k, m_Value, true); // confidential transactions will be too slow for test in debug mode.
				txv.m_vOutputs.push_back(std::move(pOut));

				k = -k;
				offset += k;
			}
		};

		typedef std::multimap<Height, MyUtxo> UtxoQueue;
		UtxoQueue m_MyUtxos;

		const MyUtxo* AddMyUtxo(Amount n, Height h, Key::Type eType)
		{
			if (!n)
				return NULL;


			ECC::Scalar::Native key;
			m_pKdf->DeriveKey(key, Key::ID(h, eType));

			MyUtxo utxo;
			utxo.m_Key = key;
			utxo.m_Value = n;

			h += (Key::Type::Coinbase == eType) ? Rules::get().MaturityCoinbase : Rules::get().MaturityStd;

			return &m_MyUtxos.insert(std::make_pair(h, utxo))->second;
		}

		struct MyKernel
		{
			Amount m_Fee;
			ECC::Scalar::Native m_k;
			bool m_bUseHashlock;

			void Export(TxKernel& krn) const
			{
				krn.m_Fee = m_Fee;
				krn.m_Excess = ECC::Point::Native(ECC::Context::get().G * m_k);

				if (m_bUseHashlock)
				{
					krn.m_pHashLock.reset(new TxKernel::HashLock); // why not?
					ECC::Hash::Processor() << m_Fee << m_k >> krn.m_pHashLock->m_Preimage;
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


		bool MakeTx(Transaction::Ptr& pTx, Height h, Height hIncubation)
		{
			UtxoQueue::iterator it = m_MyUtxos.begin();
			if (m_MyUtxos.end() == it)
				return false;

			if (it->first > h)
				return false; // not spendable yet

			pTx = std::make_shared<Transaction>();

			const MyUtxo& utxo = it->second;
			assert(utxo.m_Value);

			m_MyKernels.resize(m_MyKernels.size() + 1);
			MyKernel& mk = m_MyKernels.back();
			mk.m_Fee = 1090000;
			mk.m_bUseHashlock = 0 != (1 & h);

			Input::Ptr pInp(new Input);
			pInp->m_Commitment = ECC::Commitment(utxo.m_Key, utxo.m_Value);
			pTx->m_vInputs.push_back(std::move(pInp));

			ECC::Scalar::Native kOffset = utxo.m_Key;
			ECC::Scalar::Native k;

			if (mk.m_Fee >= utxo.m_Value)
				mk.m_Fee = utxo.m_Value;
			else
			{
				MyUtxo utxoOut;
				utxoOut.m_Value = utxo.m_Value - mk.m_Fee;

				m_pKdf->DeriveKey(k, Key::ID(h, Key::Type::Regular));
				utxoOut.m_Key = k;

				utxoOut.ToOutput(*pTx, kOffset, hIncubation);

				m_MyUtxos.insert(std::make_pair(h + hIncubation, utxoOut));
			}

			m_MyUtxos.erase(it);

			m_pKdf->DeriveKey(mk.m_k, Key::ID(h, Key::Type::Kernel, m_nKernelSubIdx++));

			TxKernel::Ptr pKrn;
			mk.Export(pKrn);
			pTx->m_vKernelsOutput.push_back(std::move(pKrn));


			k = -mk.m_k;
			kOffset += k;
			pTx->m_Offset = kOffset;

			pTx->Sort();
			Transaction::Context ctx;
			bool isTxValid = pTx->IsValid(ctx);
			verify_test(isTxValid);
			return isTxValid;
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
			std::shared_ptr<ECC::HKdf> pKdf(new ECC::HKdf);
			ECC::SetRandom(pKdf->m_Secret.V);

			m_Wallet.m_pKdf = pKdf;
	}
	};

	struct BlockPlus
	{
		typedef std::unique_ptr<BlockPlus> Ptr;

		Block::SystemState::Full m_Hdr;
		ByteBuffer m_Body;
	};

	void TestNodeProcessor1(std::vector<BlockPlus::Ptr>& blockChain)
	{
		MyNodeProcessor1 np;
		np.m_Horizon.m_Branching = 35;
		//np.m_Horizon.m_Schwarzschild = 40; - will prevent extracting some macroblock ranges
		np.Initialize(g_sz);

		NodeProcessor::BlockContext bc(np.m_TxPool, *np.m_Wallet.m_pKdf);

		const Height hIncubation = 3; // artificial incubation period for outputs.

		for (Height h = Rules::HeightGenesis; h < 96 + Rules::HeightGenesis; h++)
		{
			while (true)
			{
				// Spend it in a transaction
				Transaction::Ptr pTx;
				if (!np.m_Wallet.MakeTx(pTx, h, hIncubation))
					break;

				Transaction::Context ctx;
				ctx.m_Height.m_Min = ctx.m_Height.m_Max = np.m_Cursor.m_Sid.m_Height + 1;
				verify_test(pTx->IsValid(ctx));

				Transaction::KeyType key;
				pTx->get_Key(key);

				np.m_TxPool.AddValidTx(std::move(pTx), ctx, key);
			}

			verify_test(np.GenerateNewBlock(bc));

			np.OnState(bc.m_Hdr, PeerID());

			Block::SystemState::ID id;
			bc.m_Hdr.get_ID(id);

			np.OnBlock(id, bc.m_Body, PeerID());

			np.m_Wallet.AddMyUtxo(bc.m_Fees, h, Key::Type::Comission);
			np.m_Wallet.AddMyUtxo(Rules::get().CoinbaseEmission, h, Key::Type::Coinbase);

			BlockPlus::Ptr pBlock(new BlockPlus);
			pBlock->m_Hdr = std::move(bc.m_Hdr);
			pBlock->m_Body = std::move(bc.m_Body);
			blockChain.push_back(std::move(pBlock));
		}

		Block::BodyBase::RW rwData;
		rwData.m_sPath = g_sz3;

		Height hMid = blockChain.size() / 2 + Rules::HeightGenesis;

		{
			DeleteFile(g_sz2);

			NodeProcessor np2;
			np2.Initialize(g_sz2);

			rwData.m_hvContentTag = Zero;
			rwData.WCreate();
			np.ExportMacroBlock(rwData, HeightRange(Rules::HeightGenesis, hMid)); // first half
			rwData.Close();

			rwData.ROpen();
			verify_test(np2.ImportMacroBlock(rwData));
			rwData.Close();

			rwData.m_hvContentTag.Inc();
			rwData.WCreate();
			np.ExportMacroBlock(rwData, HeightRange(hMid + 1, Rules::HeightGenesis + blockChain.size() - 1)); // second half
			rwData.Close();

			rwData.ROpen();
			verify_test(np2.ImportMacroBlock(rwData));
			rwData.Close();

			rwData.Delete();
		}
	}


	class MyNodeProcessor2
		:public NodeProcessor
	{
	public:


		// NodeProcessor
		virtual void RequestData(const Block::SystemState::ID&, bool bBlock, const PeerID* pPreferredPeer) override {}
		virtual void OnPeerInsane(const PeerID&) override {}
		virtual void OnNewState() override {}

	};


	void TestNodeProcessor2(std::vector<BlockPlus::Ptr>& blockChain)
	{
		NodeProcessor::Horizon horz;
		horz.m_Branching = 12;
		horz.m_Schwarzschild = 12;

		size_t nMid = blockChain.size() / 2;

		{
			MyNodeProcessor2 np;
			np.m_Horizon = horz;
			np.Initialize(g_sz);

			PeerID peer;
			ZeroObject(peer);

			for (size_t i = 0; i < blockChain.size(); i += 2)
				np.OnState(blockChain[i]->m_Hdr, peer);
		}

		{
			MyNodeProcessor2 np;
			np.m_Horizon = horz;
			np.Initialize(g_sz);

			PeerID peer;
			ZeroObject(peer);

			for (size_t i = 0; i < nMid; i += 2)
			{
				Block::SystemState::ID id;
				blockChain[i]->m_Hdr.get_ID(id);
				np.OnBlock(id, blockChain[i]->m_Body, peer);
			}
		}

		{
			MyNodeProcessor2 np;
			np.m_Horizon = horz;
			np.Initialize(g_sz);

			PeerID peer;
			ZeroObject(peer);

			for (size_t i = 1; i < blockChain.size(); i += 2)
				np.OnState(blockChain[i]->m_Hdr, peer);
		}

		{
			MyNodeProcessor2 np;
			np.m_Horizon = horz;
			np.Initialize(g_sz);

			PeerID peer;
			ZeroObject(peer);

			for (size_t i = 0; i < nMid; i++)
			{
				Block::SystemState::ID id;
				blockChain[i]->m_Hdr.get_ID(id);
				np.OnBlock(id, blockChain[i]->m_Body, peer);
			}
		}

		{
			MyNodeProcessor2 np;
			np.m_Horizon = horz;
			np.Initialize(g_sz);

			PeerID peer;
			ZeroObject(peer);

			for (size_t i = nMid; i < blockChain.size(); i++)
			{
				Block::SystemState::ID id;
				blockChain[i]->m_Hdr.get_ID(id);
				np.OnBlock(id, blockChain[i]->m_Body, peer);
			}
		}

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
		node.m_Cfg.m_Sync.m_SrcPeers = 0;

		node.m_Cfg.m_Timeout.m_GetBlock_ms = 1000 * 60;
		node.m_Cfg.m_Timeout.m_GetState_ms = 1000 * 60;

		node2.m_Cfg.m_sPathLocal = g_sz2;
		node2.m_Cfg.m_Listen.port(g_Port + 1);
		node2.m_Cfg.m_Listen.ip(INADDR_ANY);
		node2.m_Cfg.m_Timeout = node.m_Cfg.m_Timeout;
		node2.m_Cfg.m_Sync.m_SrcPeers = 0;

		node2.m_Cfg.m_BeaconPort = g_Port;

		std::shared_ptr<ECC::HKdf> pKdf(new ECC::HKdf);
		ECC::SetRandom(pKdf->m_Secret.V);
		node.m_pKdf = pKdf;

		pKdf.reset(new ECC::HKdf);
		ECC::SetRandom(pKdf->m_Secret.V);
		node2.m_pKdf = pKdf;

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
					NodeProcessor::BlockContext bc(txPool, *n.m_pKdf);

					verify_test(n.get_Processor().GenerateNewBlock(bc));

					n.get_Processor().OnState(bc.m_Hdr, PeerID());

					Block::SystemState::ID id;
					bc.m_Hdr.get_ID(id);

					n.get_Processor().OnBlock(id, bc.m_Body, PeerID());

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
			void KillTimer() {
				m_pTimer->cancel();
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

		std::shared_ptr<ECC::HKdf> pKdf(new ECC::HKdf);
		ECC::SetRandom(pKdf->m_Secret.V);
		node.m_pKdf = pKdf;

		node.m_Cfg.m_Horizon.m_Branching = 6;
		node.m_Cfg.m_Horizon.m_Schwarzschild = 8;
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


			MyClient(const Key::IKdf::Ptr& pKdf)
			{
				m_Wallet.m_pKdf = pKdf;
				m_pTimer = io::Timer::create(io::Reactor::get_Current());
			}

			virtual void OnConnectedSecure() override
			{
				SetTimer(90 * 1000);

				proto::Config msgCfg;
				msgCfg.m_CfgChecksum = Rules::get().Checksum;
				Send(msgCfg);

				Send(proto::GetTime(Zero));
				Send(proto::GetExternalAddr(Zero));
			}

			virtual void OnMsg(proto::Authentication&& msg) override
			{
				proto::NodeConnection::OnMsg(std::move(msg));

				if (proto::IDType::Node == msg.m_IDType)
				{
					ECC::Scalar::Native sk;
					m_Wallet.m_pKdf->DeriveKey(sk, Key::ID(0, Key::Type::Identity));
					ProveID(sk, proto::IDType::Owner);
				}
			}

			virtual void OnMsg(proto::Time&& msg) override
			{
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

			void OnTimer() {

				io::Reactor::get_Current().stop();
			}

			virtual void OnMsg(proto::NewTip&& msg) override
			{
				printf("Tip Height=%u\n", (unsigned int) msg.m_Description.m_Height);
				verify_test(m_vStates.size() + 1 == msg.m_Description.m_Height);

				m_vStates.push_back(msg.m_Description);

				if (IsHeightReached())
				{
					if (IsAllProofsReceived() && IsAllBbsReceived())
						io::Reactor::get_Current().stop();
					return;
				}

				proto::GetMined msgOut;
				Send(msgOut);

				proto::BbsMsg msgBbs;
				msgBbs.m_Channel = 11;
				msgBbs.m_TimePosted = getTimestamp();
				msgBbs.m_Message.resize(1);
				msgBbs.m_Message[0] = (uint8_t) msg.m_Description.m_Height;
				Send(msgBbs);

				m_nBbsMsgsPending++;

				// assume we've mined this
				m_Wallet.AddMyUtxo(Rules::get().CoinbaseEmission, msg.m_Description.m_Height, Key::Type::Coinbase);

				for (size_t i = 0; i + 1 < m_vStates.size(); i++)
				{
					proto::GetProofState msgOut2;
					msgOut2.m_Height = i + Rules::HeightGenesis;
					Send(msgOut2);

					m_queProofsStateExpected.push_back((uint32_t) i);
				}

				for (auto it = m_Wallet.m_MyUtxos.begin(); m_Wallet.m_MyUtxos.end() != it; it++)
				{
					const MiniWallet::MyUtxo& utxo = it->second;

					proto::GetProofUtxo msgOut2;
					msgOut2.m_Utxo.m_Commitment = ECC::Commitment(utxo.m_Key, utxo.m_Value);
					Send(msgOut2);

					m_queProofsExpected.push_back(msgOut2.m_Utxo.m_Commitment);
				}

				for (uint32_t i = 0; i < m_Wallet.m_MyKernels.size(); i++)
				{
					const MiniWallet::MyKernel mk = m_Wallet.m_MyKernels[i];

					TxKernel krn;
					mk.Export(krn);

					proto::GetProofKernel msgOut2;
					krn.get_ID(msgOut2.m_ID);
					Send(msgOut2);

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
					if (!m_Wallet.MakeTx(msgTx.m_Transaction, msg.m_Description.m_Height, 2))
						break;

					assert(msgTx.m_Transaction);
					Send(msgTx);
				}
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

			virtual void OnMsg(proto::ProofUtxo&& msg) override
			{
				if (!m_queProofsExpected.empty())
				{
					Input inp;
					inp.m_Commitment = m_queProofsExpected.front();

					auto it = m_UtxosConfirmed.find(inp.m_Commitment);

					if (msg.m_Proofs.empty())
						verify_test(m_UtxosConfirmed.end() == it);
					else
					{
						for (uint32_t j = 0; j < msg.m_Proofs.size(); j++)
							verify_test(m_vStates.back().IsValidProofUtxo(inp, msg.m_Proofs[j]));

						if (m_UtxosConfirmed.end() == it)
							m_UtxosConfirmed.insert(inp.m_Commitment);
					}

					m_queProofsExpected.pop_front();
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


			void SetTimer(uint32_t timeout_ms) {
				m_pTimer->start(timeout_ms, false, [this]() { return (this->OnTimer)(); });
			}
			void KillTimer() {
				m_pTimer->cancel();
			}
		};

		MyClient cl(pKdf);

		io::Address addr;
		addr.resolve("127.0.0.1");
		addr.port(g_Port);

		node.m_Cfg.m_vTreasury.resize(1);
		Block::Body& treasury = node.m_Cfg.m_vTreasury[0];

		treasury.ZeroInit();
		ECC::Scalar::Native offset(Zero);

		for (int i = 0; i < 10; i++)
		{
			const Amount val = Rules::Coin * 10;
			const MiniWallet::MyUtxo& utxo = *cl.m_Wallet.AddMyUtxo(val, i, Key::Type::Regular);
			utxo.ToOutput(treasury, offset, i);
			treasury.m_Subsidy += val;
		}

		treasury.m_Offset = offset;
		treasury.Sort();

		node.Initialize();

		cl.Connect(addr);


		struct MyClient2
			:public proto::NodeConnection
		{
			MyClient* m_pOtherClient;

			virtual void OnConnectedSecure() override {
				proto::Config msgCfg;
				msgCfg.m_CfgChecksum = Rules::get().Checksum;
				msgCfg.m_SendPeers = true; // just for fun
				Send(msgCfg);
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

				verify_test(msg.m_Message.size() == 1);
				uint8_t nMsg = msg.m_Message[0];

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

		node2.m_Cfg.m_Sync.m_Timeout_ms = 0; // sync immediately after seeing 1st peer
		node2.m_Cfg.m_Dandelion = node.m_Cfg.m_Dandelion;

		pKdf.reset(new ECC::HKdf);
		ECC::SetRandom(pKdf->m_Secret.V);
		node2.m_pKdf = pKdf;
		node2.Initialize();

		pReactor->run();


		if (!cl.IsHeightReached())
			fail_test("Blockchain height didn't reach target");
		if (!cl.IsAllProofsReceived())
			fail_test("some proofs missing");
		if (!cl.IsAllBbsReceived())
			fail_test("some BBS messages missing");

		NodeProcessor::UtxoRecover urec(node2.get_Processor());
		urec.m_vKeys.push_back(node.m_pKdf);
		urec.Proceed();

		verify_test(!urec.m_Map.empty());
	}


	struct ChainContext
	{
		struct State
		{
			Block::SystemState::Full m_Hdr;
			std::unique_ptr<uint8_t[]> m_pMmrData;
		};

		std::vector<State> m_vStates;
		Merkle::Hash m_hvLive;

		struct DMmr
			:public Merkle::DistributedMmr
		{
			virtual const void* get_NodeData(Key key) const
			{
				return ((State*) key)->m_pMmrData.get();
			}

			virtual void get_NodeHash(Merkle::Hash& hv, Key key) const
			{
				((State*)key)->m_Hdr.get_Hash(hv);
			}

			IMPLEMENT_GET_PARENT_OBJ(ChainContext, m_Mmr)
		} m_Mmr;

		struct Source
			:public Block::ChainWorkProof::ISource
		{
			virtual void get_StateAt(Block::SystemState::Full& s, const Difficulty::Raw& d) override
			{
				// median search. The Hdr.m_ChainWork must be strictly bigger than d. (It's exclusive)
				typedef std::vector<State>::const_iterator Iterator;
				Iterator it0 = get_ParentObj().m_vStates.begin();
				Iterator it1 = get_ParentObj().m_vStates.end();

				while (it0 < it1)
				{
					Iterator itMid = it0 + (it1 - it0) / 2;
					if (itMid->m_Hdr.m_ChainWork <= d)
						it0 = itMid + 1;
					else
						it1 = itMid;
				}

				s = it0->m_Hdr;
			}

			virtual void get_Proof(Merkle::IProofBuilder& bld, Height h) override
			{
				assert(h >= Rules::HeightGenesis);
				h -= Rules::HeightGenesis;

				assert(h < get_ParentObj().m_vStates.size());

				get_ParentObj().m_Mmr.get_Proof(bld, h);
			}

			IMPLEMENT_GET_PARENT_OBJ(ChainContext, m_Source)
		} m_Source;

		void Init()
		{
			m_hvLive = 55U;

			m_vStates.resize(200000);
			Difficulty d = Rules::get().StartDifficulty;

			for (size_t i = 0; i < m_vStates.size(); i++)
			{
				State& s = m_vStates[i];
				if (i)
				{
					const State& s0 = m_vStates[i - 1];
					s.m_Hdr = s0.m_Hdr;
					s.m_Hdr.NextPrefix();

					m_Mmr.Append(DMmr::Key(&s0), s0.m_pMmrData.get(), s.m_Hdr.m_Prev);
				}
				else
				{
					ZeroObject(s.m_Hdr);
					s.m_Hdr.m_Height = Rules::HeightGenesis;
				}

				s.m_Hdr.m_PoW.m_Difficulty = d;
				d.Inc(s.m_Hdr.m_ChainWork);

				if (!((i + 1) % 8000))
					d.Adjust(140, 150, 3); // slightly raise

				m_Mmr.get_Hash(s.m_Hdr.m_Definition);
				Merkle::Interpret(s.m_Hdr.m_Definition, m_hvLive, true);

				uint32_t nSize = m_Mmr.get_NodeSize(i);
				s.m_pMmrData.reset(new uint8_t[nSize]);
			}
		}
	};


	void TestChainworkProof()
	{
		ChainContext cc;

		printf("Preparing blockchain ...\n");
		cc.Init();

		Block::ChainWorkProof cwp;
		cwp.m_hvRootLive = cc.m_hvLive;
		cwp.Create(cc.m_Source, cc.m_vStates.back().m_Hdr);

		uint32_t nStates = (uint32_t) cc.m_vStates.size();
		for (size_t i0 = 0; ; i0++)
		{
			verify_test(cwp.IsValid());

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

			verify_test(cwp2.IsValid());
			verify_test(cwp.IsValid());
		}
	}

}

int main()
{
	//auto logger = beam::Logger::create(LOG_LEVEL_DEBUG, LOG_LEVEL_DEBUG);

	beam::Rules::get().AllowPublicUtxos = true;
	beam::Rules::get().FakePoW = true;
	beam::Rules::get().UpdateChecksum();

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

	return g_TestsFailed ? -1 : 0;
}
