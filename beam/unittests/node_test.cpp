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
		struct MyTipEnum :public NodeDB::IEnumTip {
			uint32_t m_Tips;
			NodeDB::StateID* m_pLast;
			virtual bool OnTip(const NodeDB::StateID& sid) override {
				m_Tips++;
				if (m_pLast)
					*m_pLast = sid;
				return false;
			}
		};

		MyTipEnum mte;
		mte.m_pLast = pLast;
		mte.m_Tips = 0;
		if (bFunctional)
			db.EnumFunctionalTips(mte);
		else
			db.EnumTips(mte);

		return mte.m_Tips;
	}

	void TestNodeDB(const char* sz, bool bCreate)
	{
		NodeDB db;
		db.Open(sz, bCreate);

		NodeDB::Transaction tr(db);

		const uint32_t hMax = 250;
		const uint32_t nOrd = 3;

		Block::SystemState::Full s;
		ZeroObject(s);

		Merkle::Hash pHashes[hMax];

		for (int i = 0; i < hMax; i++)
		{
			s.m_Height = i;
			s.get_Hash(pHashes[i]);
			s.m_Prev = pHashes[i];
		}

		uint64_t pRows[hMax];

		// insert states in random order
		for (uint32_t h1 = 0; h1 < nOrd; h1++)
		{
			for (uint32_t h = h1; h < hMax; h += nOrd)
			{
				s.m_Height = h;
				if (h)
					s.m_Prev = pHashes[h-1];
				else
					ZeroObject(s.m_Prev);

				pRows[h] = db.InsertState(s);
				db.assert_valid();

				if (h)
				{
					db.SetStateFunctional(pRows[h]);
					db.assert_valid();
				}
			}
		}

		tr.Commit();
		tr.Start(db);

		assert(CountTips(db, false) == 1);
		assert(CountTips(db, true) == 0);

		// a subbranch
		const uint32_t hFork0 = 70;

		s.m_Height = hFork0;
		s.m_Prev = pHashes[hFork0-1];
		s.m_Kernels.Inc(); // alter

		uint64_t r0 = db.InsertState(s);

		assert(CountTips(db, false) == 2);

		db.assert_valid();
		db.SetStateFunctional(r0);
		db.assert_valid();

		assert(CountTips(db, true) == 0);

		s.get_Hash(s.m_Prev);
		s.m_Height++;

		uint64_t rowLast1 = db.InsertState(s);

		NodeDB::StateID sid;
		assert(CountTips(db, false, &sid) == 2);
		assert(sid.m_Height == hMax-1);

		db.SetStateFunctional(rowLast1);
		db.assert_valid();

		db.SetStateFunctional(pRows[0]); // this should trigger big update
		db.assert_valid();
		assert(CountTips(db, true, &sid) == 2);
		assert(sid.m_Height == hFork0 + 1);

		while (db.get_Prev(sid))
			;
		assert(sid.m_Height == 0);

		db.SetStateNotFunctional(pRows[0]);
		db.assert_valid();
		assert(CountTips(db, true) == 0);

		db.SetStateFunctional(pRows[0]);
		db.assert_valid();
		assert(CountTips(db, true) == 2);

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
			assert(row);
			if (!db.DeleteState(row, row))
				break;
			db.assert_valid();
		}

		assert(row && (h == hFork0));

		for (h += 2; ; h--)
		{
			if (!rowLast1)
				break;
			verify_test(db.DeleteState(rowLast1, rowLast1));
			db.assert_valid();
		}
		
		verify_test(!h);

		tr.Commit();
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
