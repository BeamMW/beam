#include "node_db.h"

namespace beam {


// Literal constants
#define TblParams				"Params"
#define TblParams_ID			"ID"
#define TblParams_Int			"ParamInt"
#define TblParams_Blob			"ParamBlob"

#define TblStates				"States"
#define TblStates_Height		"Height"
#define TblStates_Hash			"Hash"
#define TblStates_HashPrev		"HashPrev"
#define TblStates_Difficulty	"Difficulty"
#define TblStates_Timestamp		"Timestamp"
#define TblStates_HashUtxos		"HashUtxos"
#define TblStates_HashKernels	"HashKernels"
#define TblStates_Flags			"Flags"
#define TblStates_RowPrev		"RowPrev"
#define TblStates_CountNext		"CountNext"
#define TblStates_CountNextF	"CountNextFunctional"
#define TblStates_PoW			"PoW"
#define TblStates_BlindOffset	"BlindOffset"
#define TblStates_Mmr			"Mmr"
#define TblStates_Body			"Body"

#define TblTips					"Tips"
#define TblTipsReachable		"TipsReachable"
#define TblTips_Height			"Height"
#define TblTips_State			"State"

NodeDB::NodeDB()
	:m_pDb(NULL)
{
	ZeroObject(m_pPrep);
}

NodeDB::~NodeDB()
{
	Close();
}

void NodeDB::TestRet(int ret)
{
	if (SQLITE_OK != ret)
		ThrowError(ret);
}

void NodeDB::ThrowError(int ret)
{
	char sz[0x1000];
	snprintf(sz, _countof(sz), "sqlite err %d, %s", ret, sqlite3_errmsg(m_pDb));
	throw std::runtime_error(sz);
}

void NodeDB::Close()
{
	if (m_pDb)
	{
		for (size_t i = 0; i < _countof(m_pPrep); i++)
		{
			sqlite3_stmt*& pStmt = m_pPrep[i];
			if (pStmt)
			{
				sqlite3_finalize(pStmt); // don't care about retval
				pStmt = NULL;
			}
		}

		verify(SQLITE_OK == sqlite3_close(m_pDb));
		m_pDb = NULL;
	}
}

NodeDB::Recordset::Recordset(NodeDB& db)
	:m_DB(db)
	, m_pStmt(NULL)
{
}

NodeDB::Recordset::Recordset(NodeDB& db, Query::Enum val, const char* sql)
	:m_DB(db)
	,m_pStmt(NULL)
{
	m_pStmt = m_DB.get_Statement(val, sql);
}

NodeDB::Recordset::~Recordset()
{
	Reset();
}

void NodeDB::Recordset::Reset()
{
	if (m_pStmt)
	{
		sqlite3_reset(m_pStmt); // don't care about retval
		sqlite3_clear_bindings(m_pStmt);
	}
}

void NodeDB::Recordset::Reset(Query::Enum val, const char* sql)
{
	Reset();
	m_pStmt = m_DB.get_Statement(val, sql);
}

bool NodeDB::Recordset::Step()
{
	return m_DB.ExecStep(m_pStmt);
}

bool NodeDB::Recordset::IsNull(int col)
{
	return SQLITE_NULL == sqlite3_column_type(m_pStmt, col);
}

void NodeDB::Recordset::putNull(int col)
{
	m_DB.TestRet(sqlite3_bind_null(m_pStmt, col+1));
}

void NodeDB::Recordset::put(int col, uint32_t x)
{
	m_DB.TestRet(sqlite3_bind_int(m_pStmt, col+1, x));
}

void NodeDB::Recordset::put(int col, uint64_t x)
{
	m_DB.TestRet(sqlite3_bind_int64(m_pStmt, col+1, x));
}

void NodeDB::Recordset::put(int col, const Blob& x)
{
	m_DB.TestRet(sqlite3_bind_blob(m_pStmt, col+1, x.p, x.n, NULL));
}

void NodeDB::Recordset::get(int col, uint32_t& x)
{
	x = sqlite3_column_int(m_pStmt, col);
}

void NodeDB::Recordset::get(int col, uint64_t& x)
{
	x = sqlite3_column_int64(m_pStmt, col);
}

void NodeDB::Recordset::get(int col, Blob& x)
{
	x.p = sqlite3_column_blob(m_pStmt, col);
	x.n = sqlite3_column_bytes(m_pStmt, col);
}

const void* NodeDB::Recordset::get_BlobStrict(int col, uint32_t n)
{
	Blob x;
	get(col, x);

	if (x.n != n)
	{
		char sz[0x80];
		snprintf(sz, sizeof(sz), "Blob size expected=%u, actual=%u", n, x.n);
		throw std::runtime_error(sz);
	}

	return x.p;
}

void NodeDB::Open(const char* szPath, bool bCreate)
{
	uint32_t nFlags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX;
	if (bCreate)
		nFlags |= SQLITE_OPEN_CREATE;

	TestRet(sqlite3_open_v2(szPath, &m_pDb, nFlags, NULL));

	Transaction t(*this);

	const uint32_t DB_VER = 8;

	if (bCreate)
	{
		Create();
		ParamIntSet(ParamID::DbVer, DB_VER);
	}
	else
	{
		// test the DB version
		if (DB_VER != ParamIntGetDef(ParamID::DbVer))
			throw std::runtime_error("wrong version");
	}

	t.Commit();
}

void NodeDB::Create()
{
	// create tables
#define TblPrefix_Any(name) "CREATE TABLE [" #name "] ("

	ExecQuick("CREATE TABLE [" TblParams "] ("
		"[" TblParams_ID	"] INTEGER NOT NULL PRIMARY KEY,"
		"[" TblParams_Int	"] INTEGER,"
		"[" TblParams_Blob	"] BLOB)");

	ExecQuick("CREATE TABLE [" TblStates "] ("
		"[" TblStates_Height		"] INTEGER NOT NULL,"
		"[" TblStates_Hash			"] BLOB NOT NULL,"
		"[" TblStates_HashPrev		"] BLOB NOT NULL,"
		"[" TblStates_Difficulty	"] INTEGER NOT NULL,"
		"[" TblStates_Timestamp		"] INTEGER NOT NULL,"
		"[" TblStates_HashUtxos		"] BLOB NOT NULL,"
		"[" TblStates_HashKernels	"] BLOB NOT NULL,"
		"[" TblStates_Flags			"] INTEGER NOT NULL,"
		"[" TblStates_RowPrev		"] INTEGER,"
		"[" TblStates_CountNext		"] INTEGER NOT NULL,"
		"[" TblStates_CountNextF	"] INTEGER NOT NULL,"
		"[" TblStates_PoW			"] BLOB,"
		"[" TblStates_BlindOffset	"] BLOB,"
		"[" TblStates_Mmr			"] BLOB,"
		"[" TblStates_Body			"] BLOB,"
		"PRIMARY KEY (" TblStates_Height "," TblStates_Hash "),"
		"FOREIGN KEY (" TblStates_RowPrev ") REFERENCES " TblStates "(OID))");

	ExecQuick("CREATE TABLE [" TblTips "] ("
		"[" TblTips_Height	"] INTEGER NOT NULL,"
		"[" TblTips_State	"] INTEGER NOT NULL,"
		"PRIMARY KEY (" TblTips_Height "," TblTips_State "),"
		"FOREIGN KEY (" TblTips_State ") REFERENCES " TblStates "(OID))");

	ExecQuick("CREATE TABLE [" TblTipsReachable "] ("
		"[" TblTips_Height	"] INTEGER NOT NULL,"
		"[" TblTips_State	"] INTEGER NOT NULL,"
		"PRIMARY KEY (" TblTips_Height "," TblTips_State "),"
		"FOREIGN KEY (" TblTips_State ") REFERENCES " TblStates "(OID))");
}

void NodeDB::ExecQuick(const char* szSql)
{
	TestRet(sqlite3_exec(m_pDb, szSql, NULL, NULL, NULL));
}

bool NodeDB::ExecStep(sqlite3_stmt* pStmt)
{
	int nVal = sqlite3_step(pStmt);
	switch (nVal)
	{

	default:
		ThrowError(nVal);
		// no break

	case SQLITE_DONE:
		return false;

	case SQLITE_ROW:
		//{
		//	int nCount = sqlite3_column_count(pStmt);
		//	for (int ii = 0; ii < nCount; ii++)
		//	{
		//		const char* sz = sqlite3_column_name(pStmt, ii);
		//		sz = sz;
		//	}
		//}
		return true;
	}
}

bool NodeDB::ExecStep(Query::Enum val, const char* sql)
{
	return ExecStep(get_Statement(val, sql));

}

sqlite3_stmt* NodeDB::get_Statement(Query::Enum val, const char* sql)
{
	assert(val < _countof(m_pPrep));
	if (!m_pPrep[val])
	{
		const char* szTail;
		int nRet = sqlite3_prepare_v2(m_pDb, sql, -1, m_pPrep + val, &szTail);
		TestRet(nRet);
		assert(m_pPrep[val]);
	}

	return m_pPrep[val];
}


int NodeDB::get_RowsChanged() const
{
	return sqlite3_changes(m_pDb);
}

uint64_t NodeDB::get_LastInsertRowID() const
{
	return sqlite3_last_insert_rowid(m_pDb);
}

void NodeDB::TestChanged1Row()
{
	if (1 != get_RowsChanged())
		throw std::runtime_error("oops1");
}

void NodeDB::ParamIntSet(uint32_t ID, uint64_t val)
{
	Recordset rs(*this, Query::ParamIntUpd, "UPDATE " TblParams " SET " TblParams_Int "=? WHERE " TblParams_ID "=?");
	rs.put(0, val);
	rs.put(1, ID);
	rs.Step();

	if (!get_RowsChanged())
	{
		rs.Reset(Query::ParamIntIns, "INSERT INTO " TblParams " (" TblParams_ID ", " TblParams_Int ") VALUES(?,?)");

		rs.put(0, ID);
		rs.put(1, val);
		rs.Step();

		TestChanged1Row();
	}
}

bool NodeDB::ParamIntGet(uint32_t ID, uint64_t& val)
{
	Recordset rs(*this, Query::ParamIntGet, "SELECT " TblParams_Int " FROM " TblParams " WHERE " TblParams_ID "=?");
	rs.put(0, ID);

	if (!rs.Step())
		return false;

	rs.get(0, val);
	return true;
}

uint64_t NodeDB::ParamIntGetDef(int ID, uint64_t def /* = 0 */)
{
	ParamIntGet(ID, def);
	return def;
}

NodeDB::Transaction::Transaction(NodeDB* pDB)
	:m_pDB(NULL)
{
	if (pDB)
		Start(*pDB);
}

NodeDB::Transaction::~Transaction()
{
	Rollback();
}

void NodeDB::Transaction::Start(NodeDB& db)
{
	assert(!m_pDB);
	db.ExecStep(Query::Begin, "BEGIN");
	m_pDB = &db;
}

void NodeDB::Transaction::Commit()
{
	assert(m_pDB);
	m_pDB->ExecStep(Query::Commit, "COMMIT");
	m_pDB = NULL;
}

void NodeDB::Transaction::Rollback()
{
	if (m_pDB)
	{
		try {
			m_pDB->ExecStep(Query::Rollback, "ROLLBACK");
		} catch (std::exception&) {
			// TODO: DB is compromised!
		}
		m_pDB = NULL;
	}
}

#define StateCvt_Fields(macro, sep) \
	macro(Height,		m_Height) sep \
	macro(Hash,			m_Hash) sep \
	macro(HashPrev,		m_HashPrev) sep \
	macro(Difficulty,	m_Difficulty) sep \
	macro(Timestamp,	m_TimeStamp) sep \
	macro(HashUtxos,	m_Utxos) sep \
	macro(HashKernels,	m_Kernels)

#define THE_MACRO_NOP0
#define THE_MACRO_COMMA_S ","

void NodeDB::get_State(uint64_t rowid, Block::SystemState::Full& out)
{
#define THE_MACRO_1(dbname, extname) TblStates_##dbname
	Recordset rs(*this, Query::StateGet, "SELECT " StateCvt_Fields(THE_MACRO_1, THE_MACRO_COMMA_S) " FROM " TblStates " WHERE rowid=?");
#undef THE_MACRO_1

	rs.put(0, rowid);

	if (!rs.Step())
		throw "State not found!";

	int iCol = 0;

#define THE_MACRO_1(dbname, extname) rs.get(iCol++, out.extname);
	StateCvt_Fields(THE_MACRO_1, THE_MACRO_NOP0)
#undef THE_MACRO_1
}

uint64_t NodeDB::InsertState(const Block::SystemState::Full& s)
{
	// Is there a prev? Is it a tip currently?
	Recordset rs(*this, Query::StateFind2, "SELECT rowid," TblStates_CountNext " FROM " TblStates " WHERE " TblStates_Height "=? AND " TblStates_Hash "=?");
	rs.put(0, s.m_Height - 1);
	rs.put(1, s.m_HashPrev);

	uint32_t nPrevCountNext, nCountNextF;
	uint64_t rowPrev;
	if (rs.Step())
	{
		rs.get(0, rowPrev);
		rs.get(1, nPrevCountNext);
	}
	else
		rowPrev = 0;

	// Count next functional
	rs.Reset(Query::StateGetNextFCount, "SELECT COUNT() FROM " TblStates " WHERE " TblStates_Height "=? AND " TblStates_HashPrev "=? AND (" TblStates_Flags " & ?)");
	rs.put(0, s.m_Height + 1);
	rs.put(1, s.m_Hash);
	rs.put(2, StateFlags::Functional);

	verify(rs.Step());
	rs.get(0, nCountNextF);

	// Insert row

#define THE_MACRO_1(dbname, extname) TblStates_##dbname ","
#define THE_MACRO_2(dbname, extname) "?,"

	rs.Reset(Query::StateIns, "INSERT INTO " TblStates
		" (" StateCvt_Fields(THE_MACRO_1, THE_MACRO_NOP0) TblStates_Flags "," TblStates_CountNext "," TblStates_CountNextF "," TblStates_RowPrev ")"
		" VALUES (" StateCvt_Fields(THE_MACRO_2, THE_MACRO_NOP0) "0,0,?,?)");

#undef THE_MACRO_1
#undef THE_MACRO_2

	int iCol = 0;

#define THE_MACRO_1(dbname, extname) rs.put(iCol++, s.extname);
	StateCvt_Fields(THE_MACRO_1, THE_MACRO_NOP0)
#undef THE_MACRO_1

	rs.put(iCol++, nCountNextF);
	if (rowPrev)
		rs.put(iCol, rowPrev); // otherwise it'd be NULL

	rs.Step();
	TestChanged1Row();

	uint64_t rowid = get_LastInsertRowID();
	assert(rowid);

	if (rowPrev)
	{
		SetNextCount(rowPrev, nPrevCountNext + 1);

		if (!nPrevCountNext)
			TipDel(rowPrev, s.m_Height - 1);
	}

	// Ancestors
	rs.Reset(Query::StateUpdPrevRow, "UPDATE " TblStates " SET " TblStates_RowPrev "=? WHERE " TblStates_Height "=? AND " TblStates_HashPrev "=?");
	rs.put(0, rowid);
	rs.put(1, s.m_Height + 1);
	rs.put(2, s.m_Hash);

	rs.Step();
	uint32_t nCountAncestors = get_RowsChanged();

	if (nCountAncestors)
		SetNextCount(rowid, nCountAncestors);
	else
		TipAdd(rowid, s.m_Height);

	return rowid;
}

bool NodeDB::DeleteState(uint64_t rowid, uint64_t& rowPrev)
{
	Recordset rs(*this, Query::StateGetHeightAndPrev, "SELECT "
		TblStates "." TblStates_Height ","
		TblStates "." TblStates_RowPrev ","
		TblStates "." TblStates_CountNext ","
		"prv." TblStates_CountNext ","
		TblStates "." TblStates_Flags ","
		"prv." TblStates_CountNextF
		" FROM " TblStates " LEFT JOIN " TblStates " prv ON " TblStates "." TblStates_RowPrev "=prv.rowid" " WHERE " TblStates ".rowid=?");

	rs.put(0, rowid);
	if (!rs.Step())
		throw "State not found!";

	if (rs.IsNull(1))
		rowPrev = 0;
	else
		rs.get(1, rowPrev);

	uint32_t nCountNext, nFlags, nCountPrevF;
	rs.get(2, nCountNext);
	if (nCountNext)
		return false;

	Height h;
	rs.get(0, h);

	rs.get(4, nFlags);

	if (!rs.IsNull(1))
	{
		rs.get(3, nCountNext);
		if (!nCountNext)
			throw "oops!";

		nCountNext--;

		SetNextCount(rowPrev, nCountNext);

		if (!nCountNext)
			TipAdd(rowPrev, h - 1);

		if (StateFlags::Functional & nFlags)
		{
			rs.get(5, nCountPrevF);

			if (!nCountPrevF)
				throw "oops";

			nCountPrevF--;
			SetNextCountFunctional(rowPrev, nCountPrevF);

			if (!nCountPrevF && (StateFlags::Reachable & nFlags))
				TipReachableAdd(rowPrev, h - 1);

		}
	}

	TipDel(rowid, h);

	if (StateFlags::Reachable & nFlags)
		TipReachableDel(rowid, h);

	rs.Reset(Query::StateDel, "DELETE FROM " TblStates " WHERE rowid=?");
	rs.put(0, rowid);

	rs.Step();
	TestChanged1Row();

	return true;
}

uint64_t NodeDB::StateFindSafe(const Block::SystemState::ID& k)
{
	Recordset rs(*this, Query::StateFind, "SELECT rowid FROM " TblStates " WHERE " TblStates_Height "=? AND " TblStates_Hash "=?");
	rs.put(0, k.m_Height);
	rs.put(1, k.m_Hash);
	if (!rs.Step())
		return 0;

	uint64_t rowid;
	rs.get(0, rowid);
	assert(rowid);
	return rowid;
}

void NodeDB::SetNextCount(uint64_t rowid, uint32_t n)
{
	Recordset rs(*this, Query::StateSetNextCount, "UPDATE " TblStates " SET " TblStates_CountNext "=? WHERE rowid=?");
	rs.put(0, n);
	rs.put(1, rowid);

	rs.Step();
	TestChanged1Row();
}

void NodeDB::SetNextCountFunctional(uint64_t rowid, uint32_t n)
{
	Recordset rs(*this, Query::StateSetNextCountF, "UPDATE " TblStates " SET " TblStates_CountNextF "=? WHERE rowid=?");
	rs.put(0, n);
	rs.put(1, rowid);

	rs.Step();
	TestChanged1Row();
}

void NodeDB::TipAdd(uint64_t rowid, Height h)
{
	Recordset rs(*this, Query::TipAdd, "INSERT INTO " TblTips " VALUES(?,?)");
	rs.put(0, h);
	rs.put(1, rowid);

	rs.Step();
}

void NodeDB::TipDel(uint64_t rowid, Height h)
{
	Recordset rs(*this, Query::TipDel, "DELETE FROM " TblTips " WHERE " TblTips_Height "=? AND " TblTips_State "=?");
	rs.put(0, h);
	rs.put(1, rowid);

	rs.Step();
	TestChanged1Row();
}

void NodeDB::TipReachableAdd(uint64_t rowid, Height h)
{
	Recordset rs(*this, Query::TipReachableAdd, "INSERT INTO " TblTipsReachable " VALUES(?,?)");
	rs.put(0, h);
	rs.put(1, rowid);

	rs.Step();
}

void NodeDB::TipReachableDel(uint64_t rowid, Height h)
{
	Recordset rs(*this, Query::TipReachableDel, "DELETE FROM " TblTipsReachable " WHERE " TblTips_Height "=? AND " TblTips_State "=?");
	rs.put(0, h);
	rs.put(1, rowid);

	rs.Step();
	TestChanged1Row();
}

void NodeDB::SetStateFunctional(uint64_t rowid)
{
	Recordset rs(*this, Query::StateGetHeightAndAux, "SELECT "
		TblStates "." TblStates_Height ","
		TblStates "." TblStates_RowPrev ","
		TblStates "." TblStates_Flags ","
		"prv." TblStates_Flags ","
		"prv." TblStates_CountNextF
		" FROM " TblStates " LEFT JOIN " TblStates " prv ON " TblStates "." TblStates_RowPrev "=prv.rowid" " WHERE " TblStates ".rowid=?");

	rs.put(0, rowid);
	if (!rs.Step())
		throw "not found";

	uint32_t nFlags, nFlagsPrev, nCountPrevF;
	rs.get(2, nFlags);
	if (StateFlags::Functional & nFlags)
		return; // ?!

	nFlags |= StateFlags::Functional;

	Height h;
	rs.get(0, h);

	if (h)
	{
		if (!rs.IsNull(1))
		{
			uint64_t rowPrev;
			rs.get(1, rowPrev);
			rs.get(3, nFlagsPrev);
			rs.get(4, nCountPrevF);

			SetNextCountFunctional(rowPrev, nCountPrevF + 1);

			if (StateFlags::Reachable & nFlagsPrev)
			{
				nFlags |= StateFlags::Reachable;

				if (!nCountPrevF)
					TipReachableDel(rowPrev, h - 1);
			}
		}

	} else
	{
		assert(rs.IsNull(1));
		nFlags |= StateFlags::Reachable;
	}

	SetFlags(rowid, nFlags);

	if (StateFlags::Reachable & nFlags)
		OnStateReachable(rowid, h, true);
}

void NodeDB::SetStateNotFunctional(uint64_t rowid)
{
	Recordset rs(*this, Query::StateGetFlags1, "SELECT "
		TblStates "." TblStates_Height ","
		TblStates "." TblStates_RowPrev ","
		TblStates "." TblStates_Flags ","
		"prv." TblStates_CountNextF
		" FROM " TblStates " LEFT JOIN " TblStates " prv ON " TblStates "." TblStates_RowPrev "=prv.rowid" " WHERE " TblStates ".rowid=?");

	rs.put(0, rowid);
	if (!rs.Step())
		throw "State not found!";

	uint32_t nFlags, nCountPrevF;
	rs.get(2, nFlags);

	if (!(StateFlags::Functional & nFlags))
		return; // ?!
	nFlags &= ~StateFlags::Functional;

	Height h;
	rs.get(0, h);

	bool bReachable = (StateFlags::Reachable & nFlags) != 0;
	if (bReachable)
		nFlags &= ~StateFlags::Reachable;

	if (h)
	{
		if (rs.IsNull(1))
			assert(!bReachable); // orphan
		else
		{
			uint64_t rowPrev;
			rs.get(1, rowPrev);
			rs.get(3, nCountPrevF);

			if (!nCountPrevF)
				throw "oops";

			nCountPrevF--;
			SetNextCountFunctional(rowPrev, nCountPrevF);

			if (!nCountPrevF && bReachable)
				TipReachableAdd(rowPrev, h - 1);
		}
	} else
		assert(rs.IsNull(1) && bReachable);

	SetFlags(rowid, nFlags);

	if (bReachable)
		OnStateReachable(rowid, h, false);
}

void NodeDB::OnStateReachable(uint64_t rowid, Height h, bool b)
{
	typedef std::pair<uint64_t, uint32_t> RowAndFlags;
	std::vector<RowAndFlags> rows;

	while (true)
	{
		{
			Recordset rs(*this, Query::StateGetNextFunctional, "SELECT rowid," TblStates_Flags " FROM " TblStates " WHERE " TblStates_Height "=? AND " TblStates_RowPrev "=? AND (" TblStates_Flags " & ?)");
			rs.put(0, h + 1);
			rs.put(1, rowid);
			rs.put(2, StateFlags::Functional);

			while (rs.Step())
			{
				rs.get(0, rowid);
				uint32_t nFlags;
				rs.get(1, nFlags);
				assert(StateFlags::Functional & nFlags);
				assert(!(StateFlags::Reachable & nFlags) == b);
				rows.push_back(RowAndFlags(rowid, nFlags));
			}
		}

		if (rows.empty())
		{
			if (b)
				TipReachableAdd(rowid, h);
			else
				TipReachableDel(rowid, h);

			break;
		}

		for (size_t i = 0; i < rows.size(); i++)
			SetFlags(rows[i].first, rows[i].second ^ StateFlags::Reachable);

		rowid = rows[0].first;
		h++;

		for (size_t i = 1; i < rows.size(); i++)
			OnStateReachable(rows[i].first, h, b);

		rows.clear();
	}
}

void NodeDB::SetFlags(uint64_t rowid, uint32_t n)
{
	Recordset rs(*this, Query::StateSetFlags, "UPDATE " TblStates " SET " TblStates_Flags "=? WHERE rowid=?");
	rs.put(0, n);
	rs.put(1, rowid);

	rs.Step();
	TestChanged1Row();
}

void NodeDB::assert_valid()
{
	uint32_t nTips = 0, nTipsReachable = 0;

	Recordset rs(*this, Query::Dbg0, "SELECT "
		TblStates ".rowid,"
		TblStates "." TblStates_Height ","
		TblStates "." TblStates_Flags ","
		TblStates "." TblStates_RowPrev ","
		TblStates "." TblStates_CountNext ","
		TblStates "." TblStates_CountNextF ","
		"prv.rowid,"
		"prv." TblStates_Flags
		" FROM " TblStates " LEFT JOIN " TblStates " prv ON (" TblStates "." TblStates_Height "=prv." TblStates_Height "+1) AND (" TblStates "." TblStates_HashPrev "=prv." TblStates_Hash ")");

	while (rs.Step())
	{
		uint64_t rowid, rowPrev, rowPrev2;
		uint32_t nFlags, nFlagsPrev, nNext, nNextF;
		Height h;

		rs.get(0, rowid);
		rs.get(1, h);
		rs.get(2, nFlags);
		rs.get(4, nNext);
		rs.get(5, nNextF);

		if (StateFlags::Reachable & nFlags)
			assert(StateFlags::Functional & nFlags);

		assert(rs.IsNull(3) == rs.IsNull(6));
		if (!rs.IsNull(3))
		{
			rs.get(3, rowPrev);
			rs.get(6, rowPrev2);
			rs.get(7, nFlagsPrev);
			assert(rowPrev == rowPrev2);

			if (StateFlags::Reachable & nFlags)
				assert(StateFlags::Reachable & nFlagsPrev);
			else
				if (StateFlags::Functional & nFlags)
					assert(!(StateFlags::Reachable & nFlagsPrev));


		} else
		{
			if (StateFlags::Reachable & nFlags)
				assert(!h);
		}

		assert(nNext >= nNextF);

		if (!nNext)
			nTips++;

		if (!nNextF && (StateFlags::Reachable & nFlags))
			nTipsReachable++;
	}
	
	rs.Reset(Query::Dbg1, "SELECT "
		TblTips "." TblTips_Height ","
		TblStates "." TblStates_Height ","
		TblStates "." TblStates_CountNext
		" FROM " TblTips " LEFT JOIN " TblStates " ON " TblTips "." TblTips_State "=" TblStates ".rowid");

	for (; rs.Step(); nTips--)
	{
		Height h0, h1;
		rs.get(0, h0);
		rs.get(1, h1);
		assert(h0 == h1);

		uint32_t nNext;
		rs.get(2, nNext);
		assert(!nNext);
	}

	assert(!nTips);

	rs.Reset(Query::Dbg2, "SELECT "
		TblTipsReachable "." TblTips_Height ","
		TblStates "." TblStates_Height ","
		TblStates "." TblStates_CountNextF ","
		TblStates "." TblStates_Flags
		" FROM " TblTipsReachable " LEFT JOIN " TblStates " ON " TblTipsReachable "." TblTips_State "=" TblStates ".rowid");

	for (; rs.Step(); nTipsReachable--)
	{
		Height h0, h1;
		rs.get(0, h0);
		rs.get(1, h1);
		assert(h0 == h1);

		uint32_t nNextF, nFlags;
		rs.get(2, nNextF);
		rs.get(3, nFlags);
		assert(!nNextF);
		assert(StateFlags::Reachable & nFlags);
	}

	assert(!nTipsReachable);

	rs.Reset(Query::Dbg3, "SELECT "
		TblStates ".rowid," TblStates "." TblStates_CountNext ",COUNT(nxt.rowid) FROM " TblStates
		" LEFT JOIN " TblStates " nxt ON (" TblStates "." TblStates_Height "=nxt." TblStates_Height "-1) AND (" TblStates "." TblStates_Hash "=nxt." TblStates_HashPrev ")"
		"GROUP BY " TblStates ".rowid");

	while (rs.Step())
	{
		uint64_t rowid;
		uint32_t n0, n1;
		rs.get(0, rowid);
		rs.get(1, n0);
		rs.get(2, n1);
		assert(n0 == n1);
	}

	rs.Reset(Query::Dbg4, "SELECT "
		TblStates ".rowid," TblStates "." TblStates_CountNextF ",COUNT(nxt.rowid) FROM " TblStates
		" LEFT JOIN " TblStates " nxt ON (" TblStates "." TblStates_Height "=nxt." TblStates_Height "-1) AND (" TblStates "." TblStates_Hash "=nxt." TblStates_HashPrev ") AND (nxt." TblStates_Flags " & 1) "
		"GROUP BY " TblStates ".rowid");

	while (rs.Step())
	{
		uint64_t rowid;
		uint32_t n0, n1;
		rs.get(0, rowid);
		rs.get(1, n0);
		rs.get(2, n1);
		assert(n0 == n1);
	}
}

bool NodeDB::EnumTips(IEnumTip& x)
{
	Recordset rs(*this, Query::EnumTips, "SELECT " TblTips_Height "," TblTips_State " FROM " TblTips " ORDER BY "  TblTips_Height " ASC," TblTips_State " ASC");
	return EnumTipsEx(rs, x);
}

bool NodeDB::EnumFunctionalTips(IEnumTip& x)
{
	Recordset rs(*this, Query::EnumFunctionalTips, "SELECT " TblTips_Height "," TblTips_State " FROM " TblTipsReachable " ORDER BY "  TblTips_Height " DESC," TblTips_State " DESC");
	return EnumTipsEx(rs, x);
}

bool NodeDB::EnumTipsEx(Recordset& rs, IEnumTip& x)
{
	while (rs.Step())
	{
		StateID sid;
		rs.get(0, sid.m_Height);
		rs.get(1, sid.m_Row);
		if (x.OnTip(sid))
			return true;
	}
	return false;
}

bool NodeDB::get_Prev(StateID& sid)
{
	assert(sid.m_Row);
	Recordset rs(*this, Query::StateGetPrev, "SELECT " TblStates_RowPrev " FROM " TblStates " WHERE rowid=?");
	rs.put(0, sid.m_Row);

	if (!rs.Step())
		throw "oops";

	if (rs.IsNull(0))
		return false;

	rs.get(0, sid.m_Row);
	sid.m_Height--;
	return true;
}

bool NodeDB::get_Cursor(StateID& sid)
{
	sid.m_Row = ParamIntGetDef(ParamID::CursorRow);
	sid.m_Height = ParamIntGetDef(ParamID::CursorHeight);
	return (sid.m_Row > 0);
}

void NodeDB::put_Cursor(const StateID& sid)
{
	ParamIntSet(ParamID::CursorRow, sid.m_Row);
	ParamIntGetDef(ParamID::CursorHeight, sid.m_Height);
}

} // namespace beam
