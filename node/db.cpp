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

#include "db.h"

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
#define TblStates_Timestamp		"Timestamp"
#define TblStates_Kernels		"Kernels"
#define TblStates_Definition	"Definition"
#define TblStates_Flags			"Flags"
#define TblStates_RowPrev		"RowPrev"
#define TblStates_CountNext		"CountNext"
#define TblStates_CountNextF	"CountNextFunctional"
#define TblStates_PoW			"PoW"
#define TblStates_Mmr			"Mmr"
#define TblStates_BodyP			"Perishable"
#define TblStates_BodyE			"Ethernal"
#define TblStates_Rollback		"Rollback"
#define TblStates_Peer			"Peer"
#define TblStates_ChainWork		"ChainWork"

#define TblTips					"Tips"
#define TblTipsReachable		"TipsReachable"
#define TblTips_Height			"Height"
#define TblTips_State			"State"
#define TblTips_ChainWork		"ChainWork"

#define TblKernels				"Kernels"
#define TblKernels_Key			"Commitment"
#define TblKernels_Height		"Height"

#define TblEvents				"Events"
#define TblEvents_Height		"Height"
#define TblEvents_Body			"Body"
#define TblEvents_Key			"Key"

#define TblCompressed			"Macroblocks"
#define TblCompressed_Row1		"RowLast"

#define TblPeer					"Peers"
#define TblPeer_Key				"Key"
#define TblPeer_Rating			"Rating"
#define TblPeer_Addr			"Address"
#define TblPeer_LastSeen		"LastSeen"

#define TblBbs					"Bbs"
#define TblBbs_Key				"Key"
#define TblBbs_Channel			"Channel"
#define TblBbs_Time				"Time"
#define TblBbs_Msg				"Message"

#define TblDummy				"Dummies"
#define TblDummy_Key			"Key"
#define TblDummy_SpendHeight	"SpendHeight"

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
		ThrowSqliteError(ret);
}

void NodeDB::ThrowSqliteError(int ret)
{
	char sz[0x1000];
	snprintf(sz, _countof(sz), "sqlite err %d, %s", ret, sqlite3_errmsg(m_pDb));
	ThrowError(sz);
}

void NodeDB::ThrowError(const char* sz)
{
	throw std::runtime_error(sz);
}

void NodeDB::ThrowInconsistent()
{
	ThrowError("data inconcistent");
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
	:m_pStmt(NULL)
	,m_DB(db)
{
}

NodeDB::Recordset::Recordset(NodeDB& db, Query::Enum val, const char* sql)
	:m_pStmt(NULL)
	,m_DB(db)
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

void NodeDB::Recordset::StepStrict()
{
	if (!Step())
		ThrowError("not found");
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

void NodeDB::Recordset::put(int col, const char* sz)
{
	m_DB.TestRet(sqlite3_bind_text(m_pStmt, col+1, sz, -1, NULL));
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

void NodeDB::Recordset::get(int col, ByteBuffer& x)
{
	Blob b;
	get(col, b);
	b.Export(x);
}

const void* NodeDB::Recordset::get_BlobStrict(int col, uint32_t n)
{
	Blob x;
	get(col, x);

	if (x.n != n)
	{
		char sz[0x80];
		snprintf(sz, sizeof(sz), "Blob size expected=%u, actual=%u", n, x.n);
		ThrowError(sz);
	}

	return x.p;
}

void NodeDB::Open(const char* szPath)
{
	TestRet(sqlite3_open_v2(szPath, &m_pDb, SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX | SQLITE_OPEN_CREATE, NULL));

	bool bCreate;
	{
		Recordset rs(*this, Query::Scheme, "SELECT name FROM sqlite_master WHERE type='table' AND name=?");
		rs.put(0, TblParams);
		bCreate = !rs.Step();
	}

	const uint64_t nVersion = 13;

	if (bCreate)
	{
		Transaction t(*this);
		Create();
		ParamSet(ParamID::DbVer, &nVersion, NULL);
		t.Commit();
	}
	else
	{
		// test the DB version
		if (nVersion != ParamIntGetDef(ParamID::DbVer))
			ThrowError("wrong version");
	}
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
		"[" TblStates_Timestamp		"] INTEGER NOT NULL,"
		"[" TblStates_Kernels		"] BLOB NOT NULL,"
		"[" TblStates_Definition	"] BLOB NOT NULL,"
		"[" TblStates_Flags			"] INTEGER NOT NULL,"
		"[" TblStates_RowPrev		"] INTEGER,"
		"[" TblStates_CountNext		"] INTEGER NOT NULL,"
		"[" TblStates_CountNextF	"] INTEGER NOT NULL,"
		"[" TblStates_PoW			"] BLOB,"
		"[" TblStates_Mmr			"] BLOB,"
		"[" TblStates_BodyP			"] BLOB,"
		"[" TblStates_BodyE			"] BLOB,"
		"[" TblStates_Rollback		"] BLOB,"
		"[" TblStates_Peer			"] BLOB,"
		"[" TblStates_ChainWork		"] BLOB,"
		"PRIMARY KEY (" TblStates_Height "," TblStates_Hash "),"
		"FOREIGN KEY (" TblStates_RowPrev ") REFERENCES " TblStates "(OID))");

	ExecQuick("CREATE INDEX [Idx" TblStates "Wrk] ON [" TblStates "] ([" TblStates_ChainWork "]);");

	ExecQuick("CREATE TABLE [" TblTips "] ("
		"[" TblTips_Height	"] INTEGER NOT NULL,"
		"[" TblTips_State	"] INTEGER NOT NULL,"
		"PRIMARY KEY (" TblTips_Height "," TblTips_State "),"
		"FOREIGN KEY (" TblTips_State ") REFERENCES " TblStates "(OID))");

	ExecQuick("CREATE TABLE [" TblTipsReachable "] ("
		"[" TblTips_State		"] INTEGER NOT NULL,"
		"[" TblTips_ChainWork	"] BLOB NOT NULL,"
		"PRIMARY KEY (" TblTips_State "),"
		"FOREIGN KEY (" TblTips_State ") REFERENCES " TblStates "(OID),"
		"FOREIGN KEY (" TblTips_ChainWork ") REFERENCES " TblStates "(" TblStates_ChainWork "))");

	ExecQuick("CREATE INDEX [Idx" TblTipsReachable "Wrk] ON [" TblTipsReachable "] ([" TblTips_ChainWork "]);");

	ExecQuick("CREATE TABLE [" TblKernels "] ("
		"[" TblKernels_Key		"] BLOB NOT NULL,"
		"[" TblKernels_Height	"] INTEGER NOT NULL)");

	ExecQuick("CREATE INDEX [Idx" TblKernels "] ON [" TblKernels "] ([" TblKernels_Key "],[" TblKernels_Height "]  DESC);");

	ExecQuick("CREATE TABLE [" TblEvents "] ("
		"[" TblEvents_Height	"] INTEGER NOT NULL,"
		"[" TblEvents_Body		"] BLOB NOT NULL,"
		"[" TblEvents_Key		"] BLOB)");

	ExecQuick("CREATE INDEX [Idx" TblEvents "] ON [" TblEvents "] ([" TblEvents_Height "],[" TblEvents_Body "]);");
	ExecQuick("CREATE INDEX [Idx" TblEvents TblEvents_Key "] ON [" TblEvents "] ([" TblEvents_Key "]);");

	ExecQuick("CREATE TABLE [" TblCompressed "] ("
		"[" TblCompressed_Row1	"] INTEGER NOT NULL,"
		"PRIMARY KEY (" TblCompressed_Row1 "),"
		"FOREIGN KEY (" TblCompressed_Row1 ") REFERENCES " TblStates "(OID))");

	ExecQuick("CREATE TABLE [" TblPeer "] ("
		"[" TblPeer_Key			"] BLOB NOT NULL,"
		"[" TblPeer_Rating		"] INTEGER NOT NULL,"
		"[" TblPeer_Addr		"] INTEGER NOT NULL,"
		"[" TblPeer_LastSeen	"] INTEGER NOT NULL)");

	ExecQuick("CREATE TABLE [" TblBbs "] ("
		"[" TblBbs_Key		"] BLOB NOT NULL,"
		"[" TblBbs_Channel	"] INTEGER NOT NULL,"
		"[" TblBbs_Time		"] INTEGER NOT NULL,"
		"[" TblBbs_Msg		"] BLOB NOT NULL,"
		"PRIMARY KEY (" TblBbs_Key "))");

	ExecQuick("CREATE INDEX [Idx" TblBbs "CT] ON [" TblBbs "] ([" TblBbs_Channel "],[" TblBbs_Time "]);"); // fetch messages for specific channel within time range, ordered by time
	ExecQuick("CREATE INDEX [Idx" TblBbs "T] ON [" TblBbs "] ([" TblBbs_Time "]);"); // delete old messages

	ExecQuick("CREATE TABLE [" TblDummy "] ("
		"[" TblDummy_Key			"] BLOB NOT NULL,"
		"[" TblDummy_SpendHeight	"] INTEGER NOT NULL)");

	ExecQuick("CREATE INDEX [Idx" TblDummy "H] ON [" TblDummy "] ([" TblDummy_SpendHeight "]);");
}

void NodeDB::ExecQuick(const char* szSql)
{
	int n = sqlite3_total_changes(m_pDb);
	TestRet(sqlite3_exec(m_pDb, szSql, NULL, NULL, NULL));

	if (sqlite3_total_changes(m_pDb) != n)
		OnModified();
}

bool NodeDB::ExecStep(sqlite3_stmt* pStmt)
{
	int n = sqlite3_total_changes(m_pDb);

	int nVal = sqlite3_step(pStmt);

	if (sqlite3_total_changes(m_pDb) != n)
		OnModified();

	switch (nVal)
	{

	default:
		ThrowSqliteError(nVal);
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
		ThrowError("1row change failed");
}

void NodeDB::ParamSet(uint32_t ID, const uint64_t* p0, const Blob* p1)
{
	Recordset rs(*this, Query::ParamUpd, "UPDATE " TblParams " SET " TblParams_Int "=?," TblParams_Blob "=? WHERE " TblParams_ID "=?");
	if (p0)
		rs.put(0, *p0);
	if (p1)
		rs.put(1, *p1);
	rs.put(2, ID);
	rs.Step();

	if (!get_RowsChanged())
	{
		rs.Reset(Query::ParamIns, "INSERT INTO " TblParams " (" TblParams_ID "," TblParams_Int "," TblParams_Blob ") VALUES(?,?,?)");

		rs.put(0, ID);
		if (p0)
			rs.put(1, *p0);
		if (p1)
			rs.put(2, *p1);
		rs.Step();

		TestChanged1Row();
	}
}

bool NodeDB::ParamGet(uint32_t ID, uint64_t* p0, Blob* p1)
{
	Recordset rs(*this, Query::ParamGet, "SELECT " TblParams_Int "," TblParams_Blob " FROM " TblParams " WHERE " TblParams_ID "=?");
	rs.put(0, ID);

	if (!rs.Step())
		return false;

	if (p0)
		rs.get(0, *p0);
	if (p1)
	{
		const void* pPtr = rs.get_BlobStrict(1, p1->n);
		memcpy((void*) p1->p, pPtr, p1->n);
	}

	return true;
}

uint64_t NodeDB::ParamIntGetDef(int ID, uint64_t def /* = 0 */)
{
	ParamGet(ID, &def, NULL);
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
	macro(HashPrev,		m_Prev) sep \
	macro(Timestamp,	m_TimeStamp) sep \
	macro(PoW,			m_PoW) sep \
	macro(ChainWork,	m_ChainWork) sep \
	macro(Kernels,		m_Kernels) sep \
	macro(Definition,	m_Definition)

#define THE_MACRO_NOP0
#define THE_MACRO_COMMA_S ","

void NodeDB::get_State(uint64_t rowid, Block::SystemState::Full& out)
{
#define THE_MACRO_1(dbname, extname) TblStates_##dbname
	Recordset rs(*this, Query::StateGet, "SELECT " StateCvt_Fields(THE_MACRO_1, THE_MACRO_COMMA_S) " FROM " TblStates " WHERE rowid=?");
#undef THE_MACRO_1

	rs.put(0, rowid);

	rs.StepStrict();

	int iCol = 0;

#define THE_MACRO_1(dbname, extname) rs.get(iCol++, out.extname);
	StateCvt_Fields(THE_MACRO_1, THE_MACRO_NOP0)
#undef THE_MACRO_1
}

uint64_t NodeDB::InsertState(const Block::SystemState::Full& s)
{
	assert(s.m_Height >= Rules::HeightGenesis);

	// Is there a prev? Is it a tip currently?
	Recordset rs(*this, Query::StateFind2, "SELECT rowid," TblStates_CountNext " FROM " TblStates " WHERE " TblStates_Height "=? AND " TblStates_Hash "=?");
	rs.put(0, s.m_Height - 1);
	rs.put(1, s.m_Prev);

	uint32_t nPrevCountNext = 0, nCountNextF = 0; // initialized to suppress warning, not really needed
	uint64_t rowPrev;

	if (rs.Step())
	{
		rs.get(0, rowPrev);
		rs.get(1, nPrevCountNext);
	}
	else
		rowPrev = 0;

	Merkle::Hash hash;
	s.get_Hash(hash);

	// Count next functional
	rs.Reset(Query::StateGetNextFCount, "SELECT COUNT() FROM " TblStates " WHERE " TblStates_Height "=? AND " TblStates_HashPrev "=? AND (" TblStates_Flags " & ?)");
	rs.put(0, s.m_Height + 1);
	rs.put(1, hash);
	rs.put(2, StateFlags::Functional);

	verify(rs.Step());
	rs.get(0, nCountNextF);

	// Insert row

#define THE_MACRO_1(dbname, extname) TblStates_##dbname ","
#define THE_MACRO_2(dbname, extname) "?,"

	rs.Reset(Query::StateIns, "INSERT INTO " TblStates
		" (" TblStates_Hash "," StateCvt_Fields(THE_MACRO_1, THE_MACRO_NOP0) TblStates_Flags "," TblStates_CountNext "," TblStates_CountNextF "," TblStates_RowPrev ")"
		" VALUES(?," StateCvt_Fields(THE_MACRO_2, THE_MACRO_NOP0) "0,0,?,?)");

#undef THE_MACRO_1
#undef THE_MACRO_2

	int iCol = 0;
	rs.put(iCol++, hash);

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
	rs.put(2, hash);

	rs.Step();
	uint32_t nCountAncestors = get_RowsChanged();

	if (nCountAncestors)
		SetNextCount(rowid, nCountAncestors);
	else
		TipAdd(rowid, s.m_Height);

	return rowid;
}

void NodeDB::get_StateHash(uint64_t rowid, Merkle::Hash& hv)
{
	Recordset rs(*this, Query::StateGetHash, "SELECT " TblStates_Hash " FROM " TblStates " WHERE rowid=?");
	rs.put(0, rowid);

	rs.StepStrict();

	rs.get(0, hv);
}

void NodeDB::get_StateID(const StateID& sid, Block::SystemState::ID& id)
{
	get_StateHash(sid.m_Row, id.m_Hash);
	id.m_Height = sid.m_Height;
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
	rs.StepStrict();

	if (rs.IsNull(1))
		rowPrev = 0;
	else
		rs.get(1, rowPrev);

	uint32_t nCountNext, nFlags, nCountPrevF;
	rs.get(2, nCountNext);
	if (nCountNext)
		return false;

	rs.get(4, nFlags);
	if (StateFlags::Active & nFlags)
		ThrowError("attempt to delete an active state");

	Height h;
	rs.get(0, h);

	if (!rs.IsNull(1))
	{
		rs.get(3, nCountNext);
		if (!nCountNext)
			ThrowInconsistent();

		nCountNext--;

		SetNextCount(rowPrev, nCountNext);

		if (!nCountNext)
			TipAdd(rowPrev, h - 1);

		if (StateFlags::Functional & nFlags)
		{
			rs.get(5, nCountPrevF);

			if (!nCountPrevF)
				ThrowInconsistent();

			nCountPrevF--;
			SetNextCountFunctional(rowPrev, nCountPrevF);

			if (!nCountPrevF && (StateFlags::Reachable & nFlags))
				TipReachableAdd(rowPrev);

		}
	}

	TipDel(rowid, h);

	if (StateFlags::Reachable & nFlags)
		TipReachableDel(rowid);

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

void NodeDB::TipReachableAdd(uint64_t rowid)
{
	Difficulty::Raw wrk;
	get_ChainWork(rowid, wrk);

	Recordset rs(*this, Query::TipReachableAdd, "INSERT INTO " TblTipsReachable " VALUES(?,?)");
	rs.put(0, rowid);
	rs.put(1, wrk);

	rs.Step();
}

void NodeDB::TipReachableDel(uint64_t rowid)
{
	Recordset rs(*this, Query::TipReachableDel, "DELETE FROM " TblTipsReachable " WHERE " TblTips_State "=?");
	rs.put(0, rowid);

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
	rs.StepStrict();

	uint32_t nFlags, nFlagsPrev, nCountPrevF;
	rs.get(2, nFlags);
	if (StateFlags::Functional & nFlags)
		return; // ?!

	nFlags |= StateFlags::Functional;

	Height h;
	rs.get(0, h);
	assert(h >= Rules::HeightGenesis);

	uint64_t rowPrev = 0;

	if (h > Rules::HeightGenesis)
	{
		if (!rs.IsNull(1))
		{
			rs.get(1, rowPrev);
			rs.get(3, nFlagsPrev);
			rs.get(4, nCountPrevF);

			SetNextCountFunctional(rowPrev, nCountPrevF + 1);

			if (StateFlags::Reachable & nFlagsPrev)
			{
				nFlags |= StateFlags::Reachable;

				if (!nCountPrevF)
					TipReachableDel(rowPrev);
			}
		}

	} else
	{
		assert(rs.IsNull(1));
		nFlags |= StateFlags::Reachable;
	}

	SetFlags(rowid, nFlags);

	if (StateFlags::Reachable & nFlags)
		OnStateReachable(rowid, rowPrev, h, true);
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
	rs.StepStrict();

	uint32_t nFlags, nCountPrevF;
	rs.get(2, nFlags);

	if (!(StateFlags::Functional & nFlags))
		return; // ?!
	nFlags &= ~StateFlags::Functional;

	Height h;
	rs.get(0, h);
	assert(h >= Rules::HeightGenesis);

	uint64_t rowPrev = 0;

	bool bReachable = (StateFlags::Reachable & nFlags) != 0;
	if (bReachable)
		nFlags &= ~StateFlags::Reachable;

	if (h > Rules::HeightGenesis)
	{
		if (rs.IsNull(1))
			assert(!bReachable); // orphan
		else
		{
			rs.get(1, rowPrev);
			rs.get(3, nCountPrevF);

			if (!nCountPrevF)
				ThrowInconsistent();

			nCountPrevF--;
			SetNextCountFunctional(rowPrev, nCountPrevF);

			if (!nCountPrevF && bReachable)
				TipReachableAdd(rowPrev);
		}
	} else
		assert(rs.IsNull(1) && bReachable);

	SetFlags(rowid, nFlags);

	if (bReachable)
		OnStateReachable(rowid, rowPrev, h, false);
}

void NodeDB::OnStateReachable(uint64_t rowid, uint64_t rowPrev, Height h, bool b)
{
	typedef std::pair<uint64_t, uint32_t> RowAndFlags;
	std::vector<RowAndFlags> rows;

	while (true)
	{
		if (b)
			BuildMmr(rowid, rowPrev, h);

		rowPrev = rowid;

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
				TipReachableAdd(rowid);
			else
				TipReachableDel(rowid);

			break;
		}

		for (size_t i = 0; i < rows.size(); i++)
			SetFlags(rows[i].first, rows[i].second ^ StateFlags::Reachable);

		rowid = rows[0].first;
		h++;

		for (size_t i = 1; i < rows.size(); i++)
			OnStateReachable(rows[i].first, rowPrev, h, b);

		rows.clear();
	}
}

void NodeDB::set_Peer(uint64_t rowid, const PeerID* pPeer)
{
	Recordset rs(*this, Query::StateSetPeer, "UPDATE " TblStates " SET " TblStates_Peer "=? WHERE rowid=?");
	if (pPeer)
		rs.put_As(0, *pPeer);
	rs.put(1, rowid);
	rs.Step();
	TestChanged1Row();
}

bool NodeDB::get_Peer(uint64_t rowid, PeerID& peer)
{
	Recordset rs(*this, Query::StateGetPeer, "SELECT " TblStates_Peer " FROM " TblStates " WHERE rowid=?");
	rs.put(0, rowid);
	rs.StepStrict();

	if (rs.IsNull(0))
		return false;

	rs.get_As(0, peer);

	return true;
}

void NodeDB::SetStateBlock(uint64_t rowid, const Blob& bodyP, const Blob& bodyE)
{
	Recordset rs(*this, Query::StateSetBlock, "UPDATE " TblStates " SET " TblStates_BodyP "=?," TblStates_BodyE "=? WHERE rowid=?");
	if (bodyP.n)
		rs.put(0, bodyP);
	if (bodyE.n)
		rs.put(1, bodyE);
	rs.put(2, rowid);

	rs.Step();
	TestChanged1Row();
}

void NodeDB::GetStateBlock(uint64_t rowid, ByteBuffer* pP, ByteBuffer* pE, ByteBuffer* pRollback)
{
	Recordset rs(*this, Query::StateGetBlock, "SELECT " TblStates_BodyP "," TblStates_BodyE "," TblStates_Rollback " FROM " TblStates " WHERE rowid=?");
	rs.put(0, rowid);
	rs.StepStrict();

	if (pP && !rs.IsNull(0))
		rs.get(0, *pP);
	if (pE && !rs.IsNull(1))
		rs.get(1, *pE);
	if (pRollback && !rs.IsNull(2))
		rs.get(2, *pRollback);
}

void NodeDB::SetStateRollback(uint64_t rowid, const Blob& rollback)
{
	Recordset rs(*this, Query::StateSetRollback, "UPDATE " TblStates " SET " TblStates_Rollback "=? WHERE rowid=?");
	rs.put(0, rollback);
	rs.put(1, rowid);

	rs.Step();
	TestChanged1Row();
}

//void NodeDB::DelStateBlockPRB(uint64_t rowid)
//{
//	Recordset rs(*this, Query::StateDelBlock, "UPDATE " TblStates " SET " TblStates_BodyP "=?," TblStates_Rollback "=? WHERE rowid=?");
//	rs.put(2, rowid);
//	rs.Step();
//	TestChanged1Row();
//}

void NodeDB::DelStateBlockAll(uint64_t rowid)
{
	Blob bEmpty(NULL, 0);
	SetStateBlock(rowid, bEmpty, bEmpty);
	SetStateRollback(rowid, bEmpty);
}

void NodeDB::SetFlags(uint64_t rowid, uint32_t n)
{
	Recordset rs(*this, Query::StateSetFlags, "UPDATE " TblStates " SET " TblStates_Flags "=? WHERE rowid=?");
	rs.put(0, n);
	rs.put(1, rowid);

	rs.Step();
	TestChanged1Row();
}

uint32_t NodeDB::GetStateFlags(uint64_t rowid)
{
	Recordset rs(*this, Query::StateGetFlags0, "SELECT " TblStates_Flags " FROM " TblStates " WHERE rowid=?");
	rs.put(0, rowid);

	rs.StepStrict();
	
	uint32_t nFlags;
	rs.get(0, nFlags);
	return nFlags;
}

void NodeDB::get_ChainWork(uint64_t rowid, Difficulty::Raw& wrk)
{
	Recordset rs(*this, Query::StateGetChainWork, "SELECT " TblStates_ChainWork " FROM " TblStates " WHERE rowid=?");
	rs.put(0, rowid);

	rs.StepStrict();

	rs.get_As(0, wrk);
}

uint32_t NodeDB::GetStateNextCount(uint64_t rowid)
{
	Recordset rs(*this, Query::StateGetNextCount, "SELECT " TblStates_CountNext " FROM " TblStates " WHERE rowid=?");
	rs.put(0, rowid);

	rs.StepStrict();

	uint32_t nCount;
	rs.get(0, nCount);
	return nCount;
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
				assert(Rules::HeightGenesis == h);
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
		TblStates "." TblStates_CountNextF ","
		TblStates "." TblStates_Flags
		" FROM " TblTipsReachable " LEFT JOIN " TblStates " ON " TblTipsReachable "." TblTips_State "=" TblStates ".rowid");

	for (; rs.Step(); nTipsReachable--)
	{
		uint32_t nNextF, nFlags;
		rs.get(0, nNextF);
		rs.get(1, nFlags);
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

void NodeDB::EnumTips(WalkerState& x)
{
	x.m_Rs.Reset(Query::EnumTips, "SELECT " TblTips_Height "," TblTips_State " FROM " TblTips " ORDER BY "  TblTips_Height " ASC," TblTips_State " ASC");
}

void NodeDB::EnumFunctionalTips(WalkerState& x)
{
	x.m_Rs.Reset(Query::EnumFunctionalTips, "SELECT "
		TblStates "." TblStates_Height ","
		TblStates ".rowid"
		" FROM " TblTipsReachable
		" LEFT JOIN " TblStates " ON (" TblTipsReachable "." TblTips_State "=" TblStates ".rowid) "
		" ORDER BY "  TblTipsReachable "." TblTips_ChainWork " DESC");
}

void NodeDB::EnumStatesAt(WalkerState& x, Height h)
{
	x.m_Rs.Reset(Query::EnumAtHeight, "SELECT " TblStates_Height ",rowid FROM " TblStates " WHERE " TblStates_Height "=? ORDER BY " TblStates_Hash);
	x.m_Rs.put(0, h);
}

void NodeDB::EnumAncestors(WalkerState& x, const StateID& sid)
{
	x.m_Rs.Reset(Query::EnumAncestors, "SELECT " TblStates_Height ",rowid FROM " TblStates " WHERE " TblStates_Height "=? AND " TblStates_RowPrev "=? ORDER BY " TblStates_Hash);
	x.m_Rs.put(0, sid.m_Height + 1);
	x.m_Rs.put(1, sid.m_Row);
}

bool NodeDB::WalkerState::MoveNext()
{
	if (!m_Rs.Step())
		return false;
	m_Rs.get(0, m_Sid.m_Height);
	m_Rs.get(1, m_Sid.m_Row);
	return true;
}

bool NodeDB::get_Prev(uint64_t& rowid)
{
	assert(rowid);
	Recordset rs(*this, Query::StateGetPrev, "SELECT " TblStates_RowPrev " FROM " TblStates " WHERE rowid=?");
	rs.put(0, rowid);

	rs.StepStrict();

	if (rs.IsNull(0))
		return false;

	rs.get(0, rowid);
	return true;
}

bool NodeDB::get_Prev(StateID& sid)
{
	if (!get_Prev(sid.m_Row))
		return false;

	sid.m_Height--;
	return true;
}

bool NodeDB::get_Cursor(StateID& sid)
{
	sid.m_Row = ParamIntGetDef(ParamID::CursorRow);
	if (!sid.m_Row)
	{
		sid.m_Height = Rules::HeightGenesis - 1;
		return false;
	}

	sid.m_Height = ParamIntGetDef(ParamID::CursorHeight);
	assert(sid.m_Height >= Rules::HeightGenesis);
	return true;
}

void NodeDB::put_Cursor(const StateID& sid)
{
	ParamSet(ParamID::CursorRow, &sid.m_Row, NULL);
	ParamSet(ParamID::CursorHeight, &sid.m_Height, NULL);
}

void NodeDB::StateID::SetNull()
{
	m_Row = 0;
	m_Height = Rules::HeightGenesis - 1;
}

void NodeDB::MoveBack(StateID& sid)
{
	Recordset rs(*this, Query::Unactivate, "UPDATE " TblStates " SET " TblStates_Flags "=" TblStates_Flags " & ? WHERE rowid=?");
	rs.put(0, ~uint32_t(StateFlags::Active));
	rs.put(1, sid.m_Row);
	rs.Step();
	TestChanged1Row();

	if (!get_Prev(sid))
		sid.SetNull();

	put_Cursor(sid);
}

void NodeDB::MoveFwd(const StateID& sid)
{
	Recordset rs(*this, Query::Activate, "UPDATE " TblStates " SET " TblStates_Flags "=" TblStates_Flags " | ? WHERE rowid=?");
	rs.put(0, StateFlags::Active);
	rs.put(1, sid.m_Row);
	rs.Step();
	TestChanged1Row();

	put_Cursor(sid);
}

struct NodeDB::Dmmr
	:public Merkle::DistributedMmr
{
	NodeDB& m_This;
	Recordset m_Rs;
	uint64_t m_RowLast;

	Dmmr(NodeDB& x)
		:m_This(x)
		,m_Rs(x)
		,m_RowLast(0)
	{}

	void Goto(uint64_t rowid);
	void get_NodeHashInternal(Merkle::Hash&, Key);

	// DistributedMmr
	virtual const void* get_NodeData(Key) const override;
	virtual void get_NodeHash(Merkle::Hash&, Key) const override;
};

void NodeDB::Dmmr::Goto(uint64_t rowid)
{
	if (m_RowLast == rowid)
		return;
	m_RowLast = rowid;

	m_Rs.Reset(Query::MmrGet, "SELECT " TblStates_Mmr " FROM " TblStates " WHERE rowid=?");
	m_Rs.put(0, rowid);
	m_Rs.StepStrict();
}

const void* NodeDB::Dmmr::get_NodeData(Key rowid) const
{
	Dmmr* pThis = Cast::NotConst(this);
	pThis->Goto(rowid);

	Blob b;
	pThis->m_Rs.get(0, b);
	return b.p;
}

void NodeDB::Dmmr::get_NodeHash(Merkle::Hash& hv, Key rowid) const
{
	Dmmr* pThis = Cast::NotConst(this);

	if (!pThis->m_This.get_Prev(rowid))
		ThrowInconsistent();

	pThis->get_NodeHashInternal(hv, rowid);
}

void NodeDB::Dmmr::get_NodeHashInternal(Merkle::Hash& hv, Key rowid)
{
	Recordset rs(m_This, Query::HashForHist, "SELECT " TblStates_Hash " FROM " TblStates " WHERE rowid=?");
	rs.put(0, rowid);

	rs.StepStrict();

	rs.get(0, hv);
}

void NodeDB::BuildMmr(uint64_t rowid, uint64_t rowPrev, Height h)
{
	if (Rules::HeightGenesis == h)
	{
		assert(!rowPrev);
		return;
	}

	assert((h > Rules::HeightGenesis) && rowPrev && (rowid != rowPrev));

	Dmmr dmmr(*this);
	dmmr.Goto(rowid);

	if (!dmmr.m_Rs.IsNull(0))
		return;

	dmmr.m_Count = h - (Rules::HeightGenesis + 1);
	dmmr.m_kLast = rowPrev;

	Merkle::Hash hv;
	dmmr.get_NodeHashInternal(hv, rowPrev);

	Blob b;
	b.n = dmmr.get_NodeSize(dmmr.m_Count);
	std::unique_ptr<uint8_t[]> pRes(new uint8_t[b.n]);
	b.p = pRes.get();

	dmmr.Append(rowid, pRes.get(), hv);

	dmmr.m_Rs.Reset();

	Recordset rs(*this, Query::MmrSet, "UPDATE " TblStates " SET " TblStates_Mmr "=? WHERE rowid=?");
	rs.put(0, b);
	rs.put(1, rowid);
	rs.Step();
	TestChanged1Row();
}

void NodeDB::get_Proof(Merkle::IProofBuilder& bld, const StateID& sid, Height hPrev)
{
	assert((hPrev >= Rules::HeightGenesis) && (hPrev < sid.m_Height));

    Dmmr dmmr(*this);
    dmmr.m_Count = sid.m_Height - Rules::HeightGenesis;
    dmmr.m_kLast = sid.m_Row;

    dmmr.get_Proof(bld, hPrev - Rules::HeightGenesis);
}

void NodeDB::get_PredictedStatesHash(Merkle::Hash& hv, const StateID& sid)
{
	get_StateHash(sid.m_Row, hv);

    Dmmr dmmr(*this);
    dmmr.m_Count = sid.m_Height - Rules::HeightGenesis;
    dmmr.m_kLast = sid.m_Row;

    dmmr.get_PredictedHash(hv, hv);
}

void NodeDB::InsertEvent(Height h, const Blob& b, const Blob& key)
{
	Recordset rs(*this, Query::EventIns, "INSERT INTO " TblEvents "(" TblEvents_Height "," TblEvents_Body "," TblEvents_Key ") VALUES (?,?,?)");
	rs.put(0, h);
	rs.put(1, b);
	if (key.n)
		rs.put(2, key);
	rs.Step();
	TestChanged1Row();
}

void NodeDB::DeleteEventsAbove(Height h)
{
	Recordset rs(*this, Query::EventDel, "DELETE FROM " TblEvents " WHERE " TblEvents_Height ">?");
	rs.put(0, h);
	rs.Step();
}

void NodeDB::EnumEvents(WalkerEvent& x, Height hMin)
{
	x.m_Rs.Reset(Query::EventEnum, "SELECT " TblEvents_Height "," TblEvents_Body "," TblEvents_Key " FROM " TblEvents " WHERE " TblEvents_Height ">=? ORDER BY "  TblEvents_Height " ASC," TblEvents_Body " ASC");
	x.m_Rs.put(0, hMin);
}

void NodeDB::FindEvents(WalkerEvent& x, const Blob& key)
{
	x.m_Rs.Reset(Query::EventFind, "SELECT " TblEvents_Height "," TblEvents_Body "," TblEvents_Key " FROM " TblEvents " WHERE " TblEvents_Key "=?");
	x.m_Rs.put(0, key);
}

bool NodeDB::WalkerEvent::MoveNext()
{
	if (!m_Rs.Step())
		return false;
	m_Rs.get(0, m_Height);
	m_Rs.get(1, m_Body);
	if (m_Rs.IsNull(2))
		ZeroObject(m_Key);
	else
		m_Rs.get(2, m_Key);
	return true;
}

void NodeDB::EnumMacroblocks(WalkerState& x)
{
	x.m_Rs.Reset(Query::MacroblockEnum, "SELECT " TblStates "." TblTips_Height "," TblCompressed_Row1
		" FROM " TblCompressed " LEFT JOIN " TblStates " ON " TblCompressed_Row1 "=" TblStates ".rowid"
		" ORDER BY " TblStates "." TblTips_Height " DESC");
}

void NodeDB::MacroblockIns(uint64_t rowid)
{
	Recordset rs(*this, Query::MacroblockIns, "INSERT INTO " TblCompressed " VALUES(?)");
	rs.put(0, rowid);
	rs.Step();
	TestChanged1Row();
}

void NodeDB::MacroblockDel(uint64_t rowid)
{
	Recordset rs(*this, Query::MacroblockDel, "DELETE FROM " TblCompressed " WHERE " TblCompressed_Row1 "=?");
	rs.put(0, rowid);
	rs.Step();
	TestChanged1Row();
}

void NodeDB::EnumPeers(WalkerPeer& x)
{
	x.m_Rs.Reset(Query::PeerEnum, "SELECT " TblPeer_Key "," TblPeer_Rating "," TblPeer_Addr "," TblPeer_LastSeen " FROM " TblPeer);
}

bool NodeDB::WalkerPeer::MoveNext()
{
	if (!m_Rs.Step())
		return false;
	m_Rs.get(0, m_Data.m_ID);
	m_Rs.get(1, m_Data.m_Rating);
	m_Rs.get(2, m_Data.m_Address);
	m_Rs.get(3, m_Data.m_LastSeen);
	return true;
}

void NodeDB::PeersDel()
{
	Recordset rs(*this, Query::PeerDel, "DELETE FROM " TblPeer);
	rs.Step();
}

void NodeDB::PeerIns(const WalkerPeer::Data& d)
{
	Recordset rs(*this, Query::PeerAdd, "INSERT INTO " TblPeer "(" TblPeer_Key "," TblPeer_Rating "," TblPeer_Addr "," TblPeer_LastSeen ") VALUES(?,?,?,?)");
	rs.put(0, d.m_ID);
	rs.put(1, d.m_Rating);
	rs.put(2, d.m_Address);
	rs.put(3, d.m_LastSeen);
	rs.Step();
	TestChanged1Row();
}

#define TblBbs_AllFieldsListed TblBbs_Key "," TblBbs_Channel "," TblBbs_Time "," TblBbs_Msg

void NodeDB::EnumBbs(WalkerBbs& x)
{
	x.m_Rs.Reset(Query::BbsEnum, "SELECT " TblBbs_AllFieldsListed " FROM " TblBbs " WHERE " TblBbs_Channel "=? AND " TblBbs_Time ">=? ORDER BY " TblBbs_Time);

	x.m_Rs.put(0, x.m_Data.m_Channel);
	x.m_Rs.put(1, x.m_Data.m_TimePosted);
}

void NodeDB::EnumAllBbs(WalkerBbs& x)
{
	x.m_Rs.Reset(Query::BbsEnumAll, "SELECT " TblBbs_AllFieldsListed " FROM " TblBbs " ORDER BY " TblBbs_Channel " ASC," TblBbs_Time " ASC");
}

bool NodeDB::WalkerBbs::MoveNext()
{
	if (!m_Rs.Step())
		return false;
	m_Rs.get(0, m_Data.m_Key);
	m_Rs.get(1, m_Data.m_Channel);
	m_Rs.get(2, m_Data.m_TimePosted);
	m_Rs.get(3, m_Data.m_Message);
	return true;
}

bool NodeDB::BbsFind(WalkerBbs& x)
{
	x.m_Rs.Reset(Query::BbsFind, "SELECT " TblBbs_AllFieldsListed " FROM " TblBbs " WHERE " TblBbs_Key "=?");

	x.m_Rs.put(0, x.m_Data.m_Key);
	return x.MoveNext();
}

void NodeDB::BbsDelOld(Timestamp tMinToRemain)
{
	Recordset rs(*this, Query::BbsDelOld, "DELETE FROM " TblBbs " WHERE " TblBbs_Time "<?");
	rs.put(0, tMinToRemain);
	rs.Step();
}

void NodeDB::BbsIns(const WalkerBbs::Data& d)
{
	Recordset rs(*this, Query::BbsIns, "INSERT INTO " TblBbs "(" TblBbs_AllFieldsListed ") VALUES(?,?,?,?)");
	rs.put(0, d.m_Key);
	rs.put(1, d.m_Channel);
	rs.put(2, d.m_TimePosted);
	rs.put(3, d.m_Message);
	rs.Step();
	TestChanged1Row();
}

uint64_t NodeDB::FindStateWorkGreater(const Difficulty::Raw& d)
{
	Recordset rs(*this, Query::StateFindWorkGreater, "SELECT rowid FROM " TblStates " WHERE " TblStates_ChainWork ">? AND " TblStates_Flags "& ? != 0 ORDER BY " TblStates_ChainWork " ASC LIMIT 1");
	rs.put_As(0, d);
	rs.put(1, StateFlags::Active);

	rs.StepStrict();

	uint64_t res;
	rs.get(0, res);
	return res;
}

void NodeDB::InsertDummy(Height h, const Blob& key)
{
	Recordset rs(*this, Query::DummyIns, "INSERT INTO " TblDummy "(" TblDummy_Key "," TblDummy_SpendHeight ") VALUES(?,?)");
	rs.put(0, key);
	rs.put(1, h);
	rs.Step();
	TestChanged1Row();
}

uint64_t NodeDB::FindDummy(Height& h, Blob& key)
{
	Recordset rs(*this, Query::DummyFind, "SELECT rowid," TblDummy_Key "," TblDummy_SpendHeight " FROM " TblDummy " ORDER BY " TblDummy_SpendHeight " ASC LIMIT 1");
	if (!rs.Step())
		return 0;

	uint64_t res;
	rs.get(0, res);
	memcpy((void*) key.p, rs.get_BlobStrict(1, key.n), key.n);
	rs.get(2, h);

	return res;
}

void NodeDB::DeleteDummy(uint64_t rowid)
{
	Recordset rs(*this, Query::DummyDel, "DELETE FROM " TblDummy " WHERE rowid=?");
	rs.put(0, rowid);
	rs.Step();
	TestChanged1Row();
}

void NodeDB::SetDummyHeight(uint64_t rowid, Height h)
{
	Recordset rs(*this, Query::DummyUpdHeight, "UPDATE " TblDummy " SET " TblDummy_SpendHeight "=? WHERE rowid=?");
	rs.put(0, h);
	rs.put(1, rowid);
	rs.Step();
	TestChanged1Row();
}

void NodeDB::ResetCursor()
{
	Recordset rs(*this, Query::UnactivateAll, "UPDATE " TblStates " SET " TblStates_Flags "=" TblStates_Flags " & ?");
	rs.put(0, ~uint32_t(StateFlags::Active));
	rs.Step();

	rs.Reset(Query::KernelDelAll, "DELETE FROM " TblKernels);
	rs.Step();

	DeleteEventsAbove(Rules::HeightGenesis - 1);

	StateID sid;
	sid.m_Row = 0;
	sid.m_Height = Rules::HeightGenesis - 1;
	put_Cursor(sid);
}

void NodeDB::InsertKernel(const Blob& key, Height h)
{
	assert(h >= Rules::HeightGenesis);

	Recordset rs(*this, Query::KernelIns, "INSERT INTO " TblKernels "(" TblKernels_Key "," TblKernels_Height ") VALUES(?,?)");
	rs.put(0, key);
	rs.put(1, h);
	rs.Step();
	TestChanged1Row();
}

void NodeDB::DeleteKernel(const Blob& key, Height h)
{
	assert(h >= Rules::HeightGenesis);

	Recordset rs(*this, Query::KernelDel, "DELETE FROM " TblKernels " WHERE " TblKernels_Key "=? AND " TblKernels_Height "=?");
	rs.put(0, key);
	rs.put(1, h);
	rs.Step();

	uint32_t nRows = get_RowsChanged();
	if (!nRows)
		ThrowError("no krn");
	else
		// in the *very* unlikely case of kernel duplicate at the same height (!!!) - just re-insert it
		while (--nRows)
			InsertKernel(key, h);
}

Height NodeDB::FindKernel(const Blob& key)
{
	Recordset rs(*this, Query::KernelFind, "SELECT " TblKernels_Height " FROM " TblKernels " WHERE " TblKernels_Key "=? ORDER BY " TblKernels_Height " DESC LIMIT 1");
	rs.put(0, key);
	if (!rs.Step())
		return Rules::HeightGenesis - 1;

	Height h;
	rs.get(0, h);

	assert(h >= Rules::HeightGenesis);
	return h;
}

} // namespace beam
