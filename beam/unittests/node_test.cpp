//#include "../node.h"

#include "../node.h"
#include "../node_db.h"
#include "../node_processor.h"
#include "../../core/block_crypt.h"
#include "../../utility/serialize.h"
#include "../../core/serialization_adapters.h"

namespace ECC {

	Initializer g_Initializer;

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

		Merkle::Hash hvZero = { 0 };

		std::vector<Block::SystemState::Full> vStates;
		vStates.resize(hMax);
		memset0(&vStates.at(0), vStates.size());

		Merkle::CompactMmr cmmr, cmmrFork;

		for (uint32_t h = 0; h < hMax; h++)
		{
			Block::SystemState::Full& s = vStates[h];
			s.m_Height = h + Block::Rules::HeightGenesis;

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

		uint64_t rowLast1 = db.InsertState(s);

		NodeDB::StateID sid;
		verify_test(CountTips(db, false, &sid) == 2);
		verify_test(sid.m_Height == hMax-1 + Block::Rules::HeightGenesis);

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
		verify_test(sid.m_Height == hFork0 + 1 + Block::Rules::HeightGenesis);

		tr.Commit();
		tr.Start(db);

		// test proofs
		NodeDB::StateID sid2;
		verify_test(CountTips(db, false, &sid2) == 2);
		verify_test(sid2.m_Height == hMax-1 + Block::Rules::HeightGenesis);

		do
		{
			if (sid2.m_Height + 1 < hMax + Block::Rules::HeightGenesis)
			{
				Merkle::Hash hv;
				db.get_PredictedStatesHash(hv, sid2);
				Merkle::Interpret(hv, hvZero, true);
				verify_test(hv == vStates[(size_t) sid2.m_Height + 1 - Block::Rules::HeightGenesis].m_Definition);
			}

			const Merkle::Hash& hvRoot = vStates[(size_t) sid2.m_Height - Block::Rules::HeightGenesis].m_Definition;

			for (uint32_t h = Block::Rules::HeightGenesis; h < sid2.m_Height; h++)
			{
				Merkle::Proof proof;
				db.get_Proof(proof, sid2, h);

				Merkle::Hash hv;
				vStates[h - Block::Rules::HeightGenesis].get_Hash(hv);
				Merkle::Interpret(hv, proof);
				Merkle::Interpret(hv, hvZero, true);

				verify_test(hvRoot == hv);
			}

		} while (db.get_Prev(sid2));

		while (db.get_Prev(sid))
			;
		verify_test(sid.m_Height == Block::Rules::HeightGenesis);

		db.SetStateNotFunctional(pRows[0]);
		db.assert_valid();
		verify_test(CountTips(db, true) == 0);

		db.SetStateFunctional(pRows[0]);
		db.assert_valid();
		verify_test(CountTips(db, true) == 2);

		for (sid.m_Height = Block::Rules::HeightGenesis; sid.m_Height <= hMax; sid.m_Height++)
		{
			sid.m_Row = pRows[sid.m_Height - Block::Rules::HeightGenesis];
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

		NodeDB::WalkerSpendable wsp(db, false);
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
		const char* g_sz2 = "mytest2.db";
#else // WIN32
		const char* g_sz = "/tmp/mytest.db";
		const char* g_sz2 = "/tmp/mytest2.db";
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
		ECC::Kdf m_Kdf;

		struct MyUtxo
		{
			ECC::Scalar m_Key;
			Amount m_Value;

			void ToOutput(TxBase& tx, ECC::Scalar::Native& offset, Height hIncubation) const
			{
				ECC::Scalar::Native k = m_Key;

				Output::Ptr pOut(new Output);
				pOut->m_Incubation = hIncubation;
				pOut->Create(k, m_Value, true); // confidential transactions will be too slow for test in debug mode.
				tx.m_vOutputs.push_back(std::move(pOut));

				k = -k;
				offset += k;
			}
		};

		typedef std::multimap<Height, MyUtxo> UtxoQueue;
		UtxoQueue m_MyUtxos;

		const MyUtxo* AddMyUtxo(Amount n, Height h, KeyType eType)
		{
			if (!n)
				return NULL;

			ECC::Scalar::Native key;
			DeriveKey(key, m_Kdf, h, eType);

			MyUtxo utxo;
			utxo.m_Key = key;
			utxo.m_Value = n;

			h += (KeyType::Coinbase == eType) ? Block::Rules::MaturityCoinbase : Block::Rules::MaturityStd;

			return &m_MyUtxos.insert(std::make_pair(h, utxo))->second;
		}

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

			TxKernel::Ptr pKrn(new TxKernel);
			pKrn->m_Fee = 1090000;

			Input::Ptr pInp(new Input);
			pInp->m_Commitment = ECC::Commitment(utxo.m_Key, utxo.m_Value);
				pTx->m_vInputs.push_back(std::move(pInp));

			ECC::Scalar::Native kOffset = utxo.m_Key;
			ECC::Scalar::Native k;

			if (pKrn->m_Fee >= utxo.m_Value)
				pKrn->m_Fee = utxo.m_Value;
			else
			{
				MyUtxo utxoOut;
				utxoOut.m_Value = utxo.m_Value - pKrn->m_Fee;

				DeriveKey(k, m_Kdf, h, KeyType::Regular);
				utxoOut.m_Key = k;

				utxoOut.ToOutput(*pTx, kOffset, hIncubation);

				m_MyUtxos.insert(std::make_pair(h + hIncubation, utxoOut));
			}

			m_MyUtxos.erase(it);

			DeriveKey(k, m_Kdf, h, KeyType::Kernel);
			pKrn->m_Excess = ECC::Point::Native(ECC::Context::get().G * k);

			ECC::Hash::Value hv;
			pKrn->get_HashForSigning(hv);
			pKrn->m_Signature.Sign(hv, k);
			pTx->m_vKernelsOutput.push_back(std::move(pKrn));


			k = -k;
			kOffset += k;
			pTx->m_Offset = kOffset;

			pTx->Sort();
			Transaction::Context ctx;
			verify_test(pTx->IsValid(ctx));
		}
	};

	class MyNodeProcessor1
		:public NodeProcessor
	{
	public:

		TxPool m_TxPool;
		MiniWallet m_Wallet;

		MyNodeProcessor1()
		{
			ECC::SetRandom(m_Kdf.m_Secret.V);
			m_Wallet.m_Kdf = m_Kdf;
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
		np.m_Horizon.m_Schwarzschild = 40;
		np.Initialize(g_sz);

		const Height hIncubation = 3; // artificial incubation period for outputs.

		for (Height h = Block::Rules::HeightGenesis; h < 96 + Block::Rules::HeightGenesis; h++)
		{
			while (true)
			{
				// Spend it in a transaction
				Transaction::Ptr pTx;
				if (!np.m_Wallet.MakeTx(pTx, h, hIncubation))
					break;

				Transaction::Context ctx;
				verify_test(np.ValidateTx(*pTx, ctx));

				Transaction::KeyType key;
				pTx->get_Key(key);

				np.m_TxPool.AddValidTx(std::move(pTx), ctx, key);
			}

			BlockPlus::Ptr pBlock(new BlockPlus);

			Amount fees = 0;
			verify_test(np.GenerateNewBlock(np.m_TxPool, pBlock->m_Hdr, pBlock->m_Body, fees));

			np.OnState(pBlock->m_Hdr, true, NodeDB::PeerID());

			Block::SystemState::ID id;
			pBlock->m_Hdr.get_ID(id);

			np.OnBlock(id, pBlock->m_Body, NodeDB::PeerID());

			np.m_Wallet.AddMyUtxo(fees, h, KeyType::Comission);
			np.m_Wallet.AddMyUtxo(Block::Rules::CoinbaseEmission, h, KeyType::Coinbase);

			blockChain.push_back(std::move(pBlock));
		}

		Block::Body macroBlock;
		np.ExportMacroBlock(macroBlock);

		NodeProcessor np2;
		np2.Initialize(g_sz2);

		Block::SystemState::ID id;
		blockChain.back()->m_Hdr.get_ID(id);

		verify_test(!np2.ImportMacroBlock(id, macroBlock)); // no headers

		for (size_t i = 0; i < blockChain.size(); i++)
			np2.OnState(blockChain[i]->m_Hdr, true, NodeDB::PeerID());

		verify_test(np2.ImportMacroBlock(id, macroBlock));
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

			NodeProcessor::PeerID peer;
			ZeroObject(peer);

			for (size_t i = 0; i < blockChain.size(); i += 2)
				np.OnState(blockChain[i]->m_Hdr, true, peer);
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
				np.OnState(blockChain[i]->m_Hdr, true, peer);
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

	void TestNodeConversation()
	{
		// Testing configuration: Node0 <-> Node1 <-> Client.

		io::Reactor::Ptr pReactor(io::Reactor::create());
		io::Reactor::Scope scope(*pReactor);

		Node node, node2;
		node.m_Cfg.m_sPathLocal = g_sz;
		node.m_Cfg.m_Listen.port(Node::s_PortDefault);
		node.m_Cfg.m_Listen.ip(INADDR_ANY);
		node.m_Cfg.m_TestMode.m_bFakePoW = true;

		node.m_Cfg.m_Timeout.m_GetBlock_ms = 1000 * 60;
		node.m_Cfg.m_Timeout.m_GetState_ms = 1000 * 60;

		node2.m_Cfg.m_sPathLocal = g_sz2;
		node2.m_Cfg.m_Listen.port(Node::s_PortDefault + 1);
		node2.m_Cfg.m_Listen.ip(INADDR_ANY);
		node2.m_Cfg.m_Connect.resize(1);
		node2.m_Cfg.m_Connect[0].resolve("127.0.0.1");
		node2.m_Cfg.m_Connect[0].port(Node::s_PortDefault);
		node2.m_Cfg.m_Timeout = node.m_Cfg.m_Timeout;
		node2.m_Cfg.m_TestMode.m_bFakePoW = true;

		ECC::SetRandom(node.get_Processor().m_Kdf.m_Secret.V);
		ECC::SetRandom(node2.get_Processor().m_Kdf.m_Secret.V);

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

			MyClient() {
				m_pTimer = io::Timer::create(io::Reactor::get_Current().shared_from_this());
			}

			virtual void OnConnected() override {
				OnTimer();
			}

			virtual void OnClosed(int errorCode) override {
				fail_test("OnClosed");
			}

			io::Timer::Ptr m_pTimer;

			void OnTimer() {


				if (m_HeightMax < m_HeightTrg)
				{
					Block::SystemState::Full s;
					ByteBuffer body, pow;

					NodeProcessor::TxPool txPool; // empty, no transactions

					Node& n = *m_ppNode[m_iNode];
					Amount fees = 0;
					n.get_Processor().GenerateNewBlock(txPool, s, body, fees);

					n.get_Processor().OnState(s, true, NodeDB::PeerID());

					Block::SystemState::ID id;
					s.get_ID(id);

					n.get_Processor().OnBlock(id, body, NodeDB::PeerID());

					m_HeightMax = std::max(m_HeightMax, s.m_Height);

					printf("Mined block Height = %u, node = %u \n", (unsigned int) s.m_Height, (unsigned int)m_iNode);

					++m_iNode %= _countof(m_ppNode);
				}
				else
					if (m_WaitingCycles++ > 30)
					{
						fail_test("Blockchain height didn't reach target");
						io::Reactor::get_Current().stop();
					}

				SetTimer(100);
			}

			virtual void OnMsg(proto::NewTip&& msg) override
			{
				printf("Tip Height=%u\n", msg.m_ID.m_Height);
				verify_test(msg.m_ID.m_Height <= m_HeightMax);
				if (msg.m_ID.m_Height == m_HeightTrg)
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
		addr.port(Node::s_PortDefault + 1);

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
		node.m_Cfg.m_Listen.port(Node::s_PortDefault);
		node.m_Cfg.m_Listen.ip(INADDR_ANY);
		node.m_Cfg.m_TestMode.m_bFakePoW = true;
		node.m_Cfg.m_TestMode.m_FakePowSolveTime_ms = 100;
		node.m_Cfg.m_MiningThreads = 1;

		ECC::SetRandom(node.get_Processor().m_Kdf.m_Secret.V);

		struct MyClient
			:public proto::NodeConnection
		{
			const Height m_HeightTrg = 75;

			MiniWallet m_Wallet;

			std::vector<Block::SystemState::Full> m_vStates;

			std::set<ECC::Point> m_UtxosConfirmed;
			std::list<ECC::Point> m_queProofsExpected;

			uint32_t m_iProof;

			MyClient() {
				m_pTimer = io::Timer::create(io::Reactor::get_Current().shared_from_this());
			}

			virtual void OnConnected() override {
				SetTimer(60*1000);

				proto::Config msgCfg;
				ZeroObject(msgCfg);
				msgCfg.m_AutoSendHdr = true;
				Send(msgCfg);
			}

			virtual void OnClosed(int errorCode) override {
				fail_test("OnClosed");
				io::Reactor::get_Current().stop();
			}

			io::Timer::Ptr m_pTimer;

			void OnTimer() {

				fail_test("Blockchain height didn't reach target");
				io::Reactor::get_Current().stop();

				SetTimer(100);
			}

			virtual void OnMsg(proto::NewTip&& msg) override
			{
				printf("Tip Height=%u\n", msg.m_ID.m_Height);
				verify_test(m_vStates.size() + 1 == msg.m_ID.m_Height);

				if (msg.m_ID.m_Height >= m_HeightTrg)
					io::Reactor::get_Current().stop();
			}

			virtual void OnMsg(proto::Hdr&& msg) override
			{
				verify_test(m_vStates.size() + 1 == msg.m_Description.m_Height);
				m_vStates.push_back(msg.m_Description);

				// assume we've mined this
				m_Wallet.AddMyUtxo(Block::Rules::CoinbaseEmission, msg.m_Description.m_Height, KeyType::Coinbase);

				for (size_t i = 0; i + 1 < m_vStates.size(); i++)
				{
					proto::GetProofState msgOut;
					msgOut.m_Height = i + Block::Rules::HeightGenesis;
					Send(msgOut);
				}

				for (auto it = m_Wallet.m_MyUtxos.begin(); m_Wallet.m_MyUtxos.end() != it; it++)
				{
					const MiniWallet::MyUtxo& utxo = it->second;

					proto::GetProofUtxo msgOut;
					msgOut.m_MaturityMin = 0;
					msgOut.m_Utxo.m_Commitment = ECC::Commitment(utxo.m_Key, utxo.m_Value);
					Send(msgOut);

					m_queProofsExpected.push_back(msgOut.m_Utxo.m_Commitment);
				}

				m_iProof = 0;

				proto::NewTransaction msgTx;
				while (true)
				{
					if (!m_Wallet.MakeTx(msgTx.m_Transaction, msg.m_Description.m_Height, 2))
						break;

					assert(msgTx.m_Transaction);
					Send(msgTx);
				}
			}

			virtual void OnMsg(proto::Proof&& msg) override
			{
				uint32_t i = m_iProof++;
				if (i + 1 < m_vStates.size())
				{
					Merkle::Hash hv;
					m_vStates[i].get_Hash(hv);
					Merkle::Interpret(hv, msg.m_Proof);

					verify_test(hv == m_vStates.back().m_Definition);
					verify_test(!msg.m_Proof.empty() && msg.m_Proof.back().first);
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
							verify_test(msg.m_Proofs[j].IsValid(inp, m_vStates.back().m_Definition));

						if (m_UtxosConfirmed.end() == it)
							m_UtxosConfirmed.insert(inp.m_Commitment);
					}

					m_queProofsExpected.pop_front();
				}
				else
					fail_test("unexpected proof");
			}

			void SetTimer(uint32_t timeout_ms) {
				m_pTimer->start(timeout_ms, false, [this]() { return (this->OnTimer)(); });
			}
			void KillTimer() {
				m_pTimer->cancel();
			}
		};

		MyClient cl;
		cl.m_Wallet.m_Kdf = node.get_Processor().m_Kdf; // same key gen

		io::Address addr;
		addr.resolve("127.0.0.1");
		addr.port(Node::s_PortDefault);

		node.m_Cfg.m_vTreasury.resize(1);
		Block::Body& treasury = node.m_Cfg.m_vTreasury[0];

		treasury.ZeroInit();
		ECC::Scalar::Native offset(ECC::Zero);

		for (int i = 0; i < 10; i++)
		{
			const Amount val = Block::Rules::Coin * 10;
			const MiniWallet::MyUtxo& utxo = *cl.m_Wallet.AddMyUtxo(val, i, KeyType::Regular);
			utxo.ToOutput(treasury, offset, i);
			treasury.m_Subsidy += val;
		}

		treasury.m_Offset = offset;
		treasury.Sort();

		node.Initialize();

		cl.Connect(addr);

		Node node2;
		node2.m_Cfg.m_sPathLocal = g_sz2;
		node2.m_Cfg.m_TestMode.m_bFakePoW = true;
		node2.m_Cfg.m_Connect.resize(1);
		node2.m_Cfg.m_Connect[0].resolve("127.0.0.1");
		node2.m_Cfg.m_Connect[0].port(Node::s_PortDefault);
		node2.m_Cfg.m_Timeout = node.m_Cfg.m_Timeout;

		node2.Initialize();

		pReactor->run();
	}


}

int main()
{
	DeleteFileA(beam::g_sz);
	DeleteFileA(beam::g_sz2);

	beam::TestNodeDB();
	DeleteFileA(beam::g_sz);

	{
		std::vector<beam::BlockPlus::Ptr> blockChain;
		beam::TestNodeProcessor1(blockChain);
		DeleteFileA(beam::g_sz);
		DeleteFileA(beam::g_sz2);

		beam::TestNodeProcessor2(blockChain);
		DeleteFileA(beam::g_sz);
	}

	beam::TestNodeConversation();
	DeleteFileA(beam::g_sz);
	DeleteFileA(beam::g_sz2);

	beam::TestNodeClientProto();
	DeleteFileA(beam::g_sz);
	DeleteFileA(beam::g_sz2);

	return g_TestsFailed ? -1 : 0;
}
