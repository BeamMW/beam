//#include "../node.h"

#include "../node.h"
#include "../node_db.h"
#include "../node_processor.h"
#include "../../core/ecc_native.h"
#include "../../utility/serialize.h"
#include "../../core/serialization_adapters.h"

namespace ECC {

	Context g_Ctx;
	const Context& Context::get() { return g_Ctx; }

	void GenerateRandom(void* p, uint32_t n)
	{
		for (uint32_t i = 0; i < n; i++)
			((uint8_t*) p)[i] = (uint8_t) rand();
	}

	void SetRandom(uintBig& x)
	{
		GenerateRandom(x.m_pData, sizeof(x.m_pData));
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

#ifndef WIN32
void DeleteFileA(const char* szPath)
{
	unlink(szPath);
}
#endif // WIN32

#define verify_test(x) \
	do { \
		if (!(x)) \
			TestFailed(#x, __LINE__); \
	} while (false)

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

		std::vector<Block::SystemState::Full> vStates;
		vStates.resize(hMax);
		memset0(&vStates.at(0), vStates.size());

		Merkle::CompactMmr cmmr, cmmrFork;

		for (uint32_t h = 0; h < hMax; h++)
		{
			Block::SystemState::Full& s = vStates[h];
			s.m_Height = h;

			if (h)
				vStates[h-1].get_Hash(s.m_Prev);

			cmmr.Append(s.m_Prev);

			if (hFork0 == h)
				cmmrFork = cmmr;

			cmmr.get_Hash(s.m_History);
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

		NodeDB::Blob bBody("body", 4);
		Merkle::Hash peer, peer2;
		memset(peer.m_pData, 0x66, sizeof(peer.m_pData));

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
		s.m_LiveObjects.Inc(); // alter

		uint64_t r0 = db.InsertState(s);

		verify_test(CountTips(db, false) == 2);

		db.assert_valid();
		db.SetStateFunctional(r0);
		db.assert_valid();

		verify_test(CountTips(db, true) == 0);

		s.get_Hash(s.m_Prev);
		cmmrFork.Append(s.m_Prev);
		cmmrFork.get_Hash(s.m_History);
		s.m_Height++;

		uint64_t rowLast1 = db.InsertState(s);

		NodeDB::StateID sid;
		verify_test(CountTips(db, false, &sid) == 2);
		verify_test(sid.m_Height == hMax-1);

		db.SetStateFunctional(rowLast1);
		db.assert_valid();

		db.SetStateFunctional(pRows[0]); // this should trigger big update
		db.assert_valid();
		verify_test(CountTips(db, true, &sid) == 2);
		verify_test(sid.m_Height == hFork0 + 1);

		tr.Commit();
		tr.Start(db);

		// test proofs
		NodeDB::StateID sid2;
		verify_test(CountTips(db, false, &sid2) == 2);
		verify_test(sid2.m_Height == hMax-1);

		do
		{
			if (sid2.m_Height + 1 < hMax)
			{
				Merkle::Hash hv;
				db.get_PredictedStatesHash(hv, sid2);
				verify_test(hv == vStates[(size_t) sid2.m_Height + 1].m_History);
			}

			const Merkle::Hash& hvRoot = vStates[(size_t) sid2.m_Height].m_History;

			for (uint32_t h = 0; h <= sid2.m_Height; h++)
			{
				Merkle::Proof proof;
				db.get_Proof(proof, sid2, h);

				Merkle::Hash hv = vStates[h].m_Prev;
				Merkle::Interpret(hv, proof);

				verify_test(hvRoot == hv);
			}

		} while (db.get_Prev(sid2));

		while (db.get_Prev(sid))
			;
		verify_test(sid.m_Height == 0);

		db.SetStateNotFunctional(pRows[0]);
		db.assert_valid();
		verify_test(CountTips(db, true) == 0);

		db.SetStateFunctional(pRows[0]);
		db.assert_valid();
		verify_test(CountTips(db, true) == 2);

		for (sid.m_Height = 0; sid.m_Height < hMax; sid.m_Height++)
		{
			sid.m_Row = pRows[sid.m_Height];
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

		// utxos and kernels
		NodeDB::Blob b0(vStates[0].m_Prev.m_pData, sizeof(vStates[0].m_Prev.m_pData));

		db.AddSpendable(b0, NodeDB::Blob("hello, world!", 13), 5, 3);

		NodeDB::WalkerSpendable wsp(db);
		for (db.EnumUnpsent(wsp); wsp.MoveNext(); )
			;
		db.ModifySpendable(b0, 0, -3);
		for (db.EnumUnpsent(wsp); wsp.MoveNext(); )
			;

		db.ModifySpendable(b0, 0, 2);
		for (db.EnumUnpsent(wsp); wsp.MoveNext(); )
			;

		db.ModifySpendable(b0, -5, -4);
		for (db.EnumUnpsent(wsp); wsp.MoveNext(); )
			;
	}

#ifdef WIN32
		const char* g_sz = "mytest.db";
#else // WIN32
		const char* g_sz = "/tmp/mytest.db";
#endif // WIN32

	void TestNodeDB()
	{

		DeleteFileA(g_sz);
		TestNodeDB(g_sz); // will create

		{
			NodeDB db;
			db.Open(g_sz); // test to open already-existing DB
		}
		DeleteFileA(g_sz);
	}

	class MyNodeProcessor1
		:public NodeProcessor
	{
	public:

		struct MyUtxo {
			ECC::Scalar m_Key;
			Amount m_Value;
			//Height m_Height;
			//bool m_Coinbase;
		};

		typedef std::multimap<Height, MyUtxo> UtxoQueue;
		UtxoQueue m_MyUtxos;

		void AddMyUtxo(const ECC::Scalar::Native& key, Amount n, Height h, bool bCoinbase)
		{
			if (!n)
				return;

			MyUtxo utxo;
			utxo.m_Key = key;
			utxo.m_Value = n;
			//utxo.m_Height = h;
			//utxo.m_Coinbase = bCoinbase;

			h += bCoinbase ? Block::s_MaturityCoinbase : Block::s_MaturityStd;

			m_MyUtxos.insert(std::make_pair(h, utxo));
		}

		// NodeProcessor
		virtual void get_Key(ECC::Scalar::Native& k, Height h, bool bCoinbase) override
		{
			ECC::SetRandom(k);
		}

		virtual void OnMined(Height h, const ECC::Scalar::Native& kFee, Amount nFee, const ECC::Scalar::Native& kCoinbase, Amount nCoinbase) override
		{
			AddMyUtxo(kFee, nFee, h, false);
			AddMyUtxo(kCoinbase, nCoinbase, h, true);
		}
	};

	void SpendUtxo(Transaction& tx, const MyNodeProcessor1::MyUtxo& utxo, MyNodeProcessor1::MyUtxo& utxoOut)
	{
		if (!utxo.m_Value)
			return; //?!

		TxKernel::Ptr pKrn(new TxKernel);
		pKrn->m_Fee = 1090000;

		Input::Ptr pInp(new Input);
		pInp->m_Commitment = ECC::Commitment(utxo.m_Key, utxo.m_Value);
		tx.m_vInputs.push_back(std::move(pInp));

		ECC::Scalar::Native kOffset = utxo.m_Key;
		ECC::Scalar::Native k;

		if (pKrn->m_Fee >= utxo.m_Value)
		{
			pKrn->m_Fee = utxo.m_Value;
			utxoOut.m_Value = 0;
		}
		else
		{
			utxoOut.m_Value = utxo.m_Value - pKrn->m_Fee;

			ECC::SetRandom(k);
			utxoOut.m_Key = k;

			Output::Ptr pOut(new Output);
			pOut->m_pPublic.reset(new ECC::RangeProof::Public);
			pOut->m_pPublic->m_Value = utxoOut.m_Value;
			pOut->m_pPublic->Create(k);
			pOut->m_Commitment = ECC::Commitment(k, utxoOut.m_Value);
			tx.m_vOutputs.push_back(std::move(pOut));

			k = -k;
			kOffset += k;
		}

		ECC::SetRandom(k);
		pKrn->m_Excess = ECC::Point::Native(ECC::Context::get().G * k);

		ECC::Hash::Value hv;
		pKrn->get_Hash(hv);
		pKrn->m_Signature.Sign(hv, k);
		tx.m_vKernelsOutput.push_back(std::move(pKrn));


		k = -k;
		kOffset += k;
		tx.m_Offset = kOffset;

		tx.Sort();
		Transaction::Context ctx;
		verify_test(tx.IsValid(ctx));
	}

	struct BlockPlus
	{
		typedef std::unique_ptr<BlockPlus> Ptr;

		Block::SystemState::Full m_Hdr;
		ByteBuffer m_PoW;
		ByteBuffer m_Body;
	};

	void TestNodeProcessor1(std::vector<BlockPlus::Ptr>& blockChain)
	{
		DeleteFileA(g_sz);

		MyNodeProcessor1 np;
		np.m_Horizon.m_Branching = 35;
		np.m_Horizon.m_Schwarzschild = 40;
		np.Initialize(g_sz);

		for (Height h = 0; h < 96; h++)
		{
			std::list<MyNodeProcessor1::MyUtxo> lstNewOutputs;

			while (true)
			{
				MyNodeProcessor1::UtxoQueue::iterator it = np.m_MyUtxos.begin();
				if (np.m_MyUtxos.end() == it)
					break;

				if (it->first > h)
					break; // not spendable yet

				// Spend it in a transaction
				Transaction::Ptr pTx(new Transaction);
				MyNodeProcessor1::MyUtxo utxoOut;
				SpendUtxo(*pTx, it->second, utxoOut);

				np.m_MyUtxos.erase(it);

				if (utxoOut.m_Value)
					lstNewOutputs.push_back(utxoOut);

				np.FeedTransaction(std::move(pTx));
			}

			for (; !lstNewOutputs.empty(); lstNewOutputs.pop_front())
				np.m_MyUtxos.insert(std::make_pair(h + 3, lstNewOutputs.front()));

			BlockPlus::Ptr pBlock(new BlockPlus);

			np.SimulateMinedBlock(pBlock->m_Hdr, pBlock->m_Body, pBlock->m_PoW);
			blockChain.push_back(std::move(pBlock));
		}

	}


	class MyNodeProcessor2
		:public NodeProcessor
	{
	public:


		// NodeProcessor
		virtual void get_Key(ECC::Scalar::Native& k, Height h, bool bCoinbase) override { }
		virtual void RequestData(const Block::SystemState::ID&, bool bBlock, const PeerID* pPreferredPeer) {}
		virtual void OnPeerInsane(const PeerID&) {}
		virtual void OnNewState() {}

	};


	void TestNodeProcessor2(std::vector<BlockPlus::Ptr>& blockChain)
	{
		DeleteFileA(g_sz);

		NodeProcessor::Horizon horz;
		horz.m_Branching = 12;
		horz.m_Schwarzschild = 12;

		size_t nMid = blockChain.size() / 2;

		{
			MyNodeProcessor2 np;
			np.m_Horizon = horz;
			np.Initialize(g_sz);

			NodeProcessor::PeerID peer;
			ZeroObject(peer);

			for (size_t i = 0; i < blockChain.size(); i += 2)
				np.OnState(blockChain[i]->m_Hdr, blockChain[i]->m_PoW, peer);
		}

		{
			MyNodeProcessor2 np;
			np.m_Horizon = horz;
			np.Initialize(g_sz);

			NodeProcessor::PeerID peer;
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

			NodeProcessor::PeerID peer;
			ZeroObject(peer);

			for (size_t i = 1; i < blockChain.size(); i += 2)
				np.OnState(blockChain[i]->m_Hdr, blockChain[i]->m_PoW, peer);
		}

		{
			MyNodeProcessor2 np;
			np.m_Horizon = horz;
			np.Initialize(g_sz);

			NodeProcessor::PeerID peer;
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

			NodeProcessor::PeerID peer;
			ZeroObject(peer);

			for (size_t i = nMid; i < blockChain.size(); i++)
			{
				Block::SystemState::ID id;
				blockChain[i]->m_Hdr.get_ID(id);
				np.OnBlock(id, blockChain[i]->m_Body, peer);
			}
		}

	}

	void TestNode1()
	{
		io::Reactor::Ptr pReactor(io::Reactor::create());
		io::Reactor::Scope scope(*pReactor);

		Node node;
		node.m_Cfg.m_sPathLocal = g_sz;
		node.m_Cfg.m_Listen.port(Node::s_PortDefault);
		node.m_Cfg.m_Listen.ip(INADDR_ANY);

		node.Initialize();

		struct MyClient
			:public proto::NodeConnection
		{
			bool m_bConnected;

			MyClient() {
				m_pTimer = io::Timer::create(io::Reactor::get_Current().shared_from_this());
				m_bConnected = false;
			}

			virtual void OnConnected() override {

				m_bConnected = true;

				proto::IsHasBody msg;
				msg.m_ID.m_Height = 0;
				ZeroObject(msg.m_ID.m_Hash);

				Send(msg);

				SetTimer(1200); // for reconnection
			}

			virtual void OnClosed(int errorCode) override {
				OnFail();
			}

			void OnFail() {
				Reset();
				SetTimer(2000);
				m_bConnected = false;
			}

			io::Timer::Ptr m_pTimer;
			void OnTimer() {

				if (!m_bConnected)
				{
					Reset();

					try {

						io::Address addr;
						addr.resolve("127.0.0.1");
						addr.port(Node::s_PortDefault);

						Connect(addr);

					}
					catch (...) {
						OnFail();
					}
				}
				else
					OnFail();
			}

			void SetTimer(uint32_t timeout_ms) {
				m_pTimer->start(timeout_ms, false, [this]() { return (this->OnTimer)(); });
			}
			void KillTimer() {
				m_pTimer->cancel();
			}
		};

		MyClient cl;

		cl.SetTimer(1500);

		pReactor->run();
	}

}

int main()
{
	beam::TestNodeDB();

	std::vector<beam::BlockPlus::Ptr> blockChain;
	beam::TestNodeProcessor1(blockChain);
	beam::TestNodeProcessor2(blockChain);

    //beam::Node node;
    
    return 0;
}
