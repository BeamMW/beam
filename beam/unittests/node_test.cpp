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

	void GetStateHash(Merkle::Hash& hash, Height h, uint8_t iBranch)
	{
		ECC::Hash::Processor() << h << iBranch >> hash;
	}

	void GetState(Block::SystemState::Full& s, Height h, uint8_t iBranch, uint8_t iBranchPrev)
	{
		s.m_Height = h;
		GetStateHash(s.m_Hash, h, iBranch);
		GetStateHash(s.m_HashPrev, h-1, iBranchPrev);
	}

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

		uint64_t rowLast0 = 0, rowZero = 0;

		// insert states in random order
		for (uint32_t h1 = 0; h1 < nOrd; h1++)
		{
			for (uint32_t h = h1; h < hMax; h += nOrd)
			{
				GetState(s, h, 0, 0);
				uint64_t row = db.InsertState(s);
				db.assert_valid();

				if (hMax-1 == h)
					rowLast0 = row;

				if (h)
				{
					db.SetStateFunctional(row);
					db.assert_valid();
				}
				else
					rowZero = row;
			}
		}

		tr.Commit();
		tr.Start(db);

		assert(CountTips(db, false) == 1);
		assert(CountTips(db, true) == 0);

		// a subbranch
		const uint32_t hFork0 = 70;

		GetState(s, hFork0, 1, 0);
		uint64_t r0 = db.InsertState(s);

		assert(CountTips(db, false) == 2);

		db.assert_valid();
		db.SetStateFunctional(r0);
		db.assert_valid();

		assert(CountTips(db, true) == 0);

		GetState(s, hFork0+1, 1, 1);
		uint64_t rowLast1 = db.InsertState(s);

		NodeDB::StateID sid;
		assert(CountTips(db, false, &sid) == 2);
		assert(sid.m_Height == hMax-1);

		db.SetStateFunctional(rowLast1);
		db.assert_valid();

		db.SetStateFunctional(rowZero); // this should trigger big update
		db.assert_valid();
		assert(CountTips(db, true, &sid) == 2);
		assert(sid.m_Height == hFork0 + 1);

		while (db.get_Prev(sid))
			;
		assert(sid.m_Height == 0);

		db.SetStateNotFunctional(rowZero);
		db.assert_valid();
		assert(CountTips(db, true) == 0);

		db.SetStateFunctional(rowZero);
		db.assert_valid();
		assert(CountTips(db, true) == 2);

		tr.Commit();
		tr.Start(db);

		// Delete main branch up to this tip
		uint32_t h = hMax;
		for (; ; h--)
		{
			assert(rowLast0);
			if (!db.DeleteState(rowLast0, rowLast0))
				break;
			db.assert_valid();
		}

		assert(rowLast0 && (h == hFork0));

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
