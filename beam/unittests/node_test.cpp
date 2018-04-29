//#include "../node.h"

#include "../node_db.h"
#include "../../core/ecc_native.h"

namespace ECC {

	Context g_Ctx;
	const Context& Context::get() { return g_Ctx; }

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

void DeleteFile(const char* szPath)
{
#ifdef WIN32
	DeleteFileA(szPath);
#else // WIN32
	unlink(szPath);
#endif // WIN32
}

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

	void TestNodeDB(const char* sz, bool bCreate)
	{
		NodeDB db;
		db.Open(sz, bCreate);

		if (!bCreate)
			return; // DB successfully opened. Skip the rest.

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

			cmmr.get_Hash(s.m_States);
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
		Merkle::Hash peerID;
		memset(peerID.m_pData, 0x66, sizeof(peerID.m_pData));

		db.SetStateBlock(pRows[0], bBody, peerID);

		ByteBuffer bbBody;
		ZeroObject(peerID);
		db.GetStateBlock(pRows[0], bbBody, peerID);
		db.DelStateBlock(pRows[0]);
		ZeroObject(peerID);
		db.GetStateBlock(pRows[0], bbBody, peerID);

		tr.Commit();
		tr.Start(db);

		verify_test(CountTips(db, false) == 1);
		verify_test(CountTips(db, true) == 0);

		// a subbranch
		Block::SystemState::Full s = vStates[hFork0];
		s.m_Kernels.Inc(); // alter

		uint64_t r0 = db.InsertState(s);

		verify_test(CountTips(db, false) == 2);

		db.assert_valid();
		db.SetStateFunctional(r0);
		db.assert_valid();

		verify_test(CountTips(db, true) == 0);

		s.get_Hash(s.m_Prev);
		cmmrFork.Append(s.m_Prev);
		cmmrFork.get_Hash(s.m_States);
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
				verify_test(hv == vStates[(size_t) sid2.m_Height + 1].m_States);
			}

			const Merkle::Hash& hvRoot = vStates[(size_t) sid2.m_Height].m_States;

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

		db.AddUtxo(b0, NodeDB::Blob("hello, world!", 13), 5, 3);

		NodeDB::WalkerUtxo wutxo(db);
		for (db.EnumLiveUtxos(wutxo); wutxo.MoveNext(); )
			;
		db.ModifyUtxo(b0, 0, -3);
		for (db.EnumLiveUtxos(wutxo); wutxo.MoveNext(); )
			;

		db.ModifyUtxo(b0, 0, 2);
		for (db.EnumLiveUtxos(wutxo); wutxo.MoveNext(); )
			;

		db.DeleteUtxo(b0);
		for (db.EnumLiveUtxos(wutxo); wutxo.MoveNext(); )
			;

		db.AddKernel(b0, NodeDB::Blob("hello, world!", 13), 1);

		NodeDB::WalkerKernel wkrn(db);
		for (db.EnumLiveKernels(wkrn); wkrn.MoveNext(); )
			;
		db.ModifyKernel(b0, -1);
		for (db.EnumLiveKernels(wkrn); wkrn.MoveNext(); )
			;

		db.ModifyKernel(b0, 1);
		for (db.EnumLiveKernels(wkrn); wkrn.MoveNext(); )
			;

		db.DeleteKernel(b0);
		for (db.EnumLiveKernels(wkrn); wkrn.MoveNext(); )
			;
	}

	void TestNodeDB()
	{
#ifdef WIN32
		const char* sz = "mytest.db";
#else // WIN32
		const char* sz = "/tmp/mytest.db";
#endif // WIN32

		DeleteFile(sz);
		TestNodeDB(sz, true);
		TestNodeDB(sz, false);
		DeleteFile(sz);
	}

}

int main()
{
	beam::TestNodeDB();

    //beam::Node node;
    
    return 0;
}
