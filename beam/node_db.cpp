#include "node_db.h"

namespace beam {


const char* const NodeDB::s_szSql[Query::count] = {
	#define MacroSqlDecl(name, sql) sql,
	AllQueries(MacroSqlDecl)
};

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

NodeDB::Recordset::Recordset(NodeDB& db, Query::Enum val)
	:m_DB(db)
	,m_pStmt(NULL)
{
	m_pStmt = m_DB.get_Statement(val);
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

void NodeDB::Recordset::Reset(Query::Enum val)
{
	Reset();
	m_pStmt = m_DB.get_Statement(val);
}

bool NodeDB::Recordset::FetchRow()
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

void NodeDB::Recordset::put(int col, int x)
{
	m_DB.TestRet(sqlite3_bind_int(m_pStmt, col+1, x));
}

void NodeDB::Recordset::put(int col, int64_t x)
{
	m_DB.TestRet(sqlite3_bind_int64(m_pStmt, col+1, x));
}

void NodeDB::Recordset::put(int col, const Blob& x)
{
	m_DB.TestRet(sqlite3_bind_blob(m_pStmt, col+1, x.p, x.n, NULL));
}

void NodeDB::Recordset::put(int col, const Merkle::Hash& x)
{
	static_assert(sizeof(x) == sizeof(x.m_pData), "");
	put(col, Blob(&x, sizeof(x)));
}

void NodeDB::Recordset::get(int col, int& x)
{
	x = sqlite3_column_int(m_pStmt, col);
}

void NodeDB::Recordset::get(int col, int64_t& x)
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
	TestRet(sqlite3_config(SQLITE_CONFIG_SINGLETHREAD));
	TestRet(sqlite3_open(szPath, &m_pDb));

	ExecStep(Query::Begin);

	const int DB_VER = 8;

	if (bCreate)
	{
		Create();
		ParamIntSet(Query::Params::ID::DbVer, DB_VER);
	}
	else
	{
		// test the DB version
		if (DB_VER != ParamIntGetDef(Query::Params::ID::DbVer))
			throw std::runtime_error("wrong version");
	}

	ExecStep(Query::Commit);
}

void NodeDB::Create()
{
	// create tables
#define TblPrefix_Any(name) "CREATE TABLE [" #name "] ("

#define THE_MACRO(name, type) ",[" #name "]" type
	ExecQuick(TblPrefix_Any(Params) "[ID] INTEGER NOT NULL PRIMARY KEY" NodeDb_Table_Params(THE_MACRO, NOP0) ")");
#undef THE_MACRO

#define THE_MACRO(name, type) "[" #name "]" type
	ExecQuick(TblPrefix_Any(States) NodeDb_Table_States(THE_MACRO, M0_Comma_Str)
		",FOREIGN KEY (RowPrev) REFERENCES States(OID)"
		")");
#undef THE_MACRO

	ExecQuick("CREATE UNIQUE INDEX [IdxStateID] ON [States] ([Height] ASC, [Hash] ASC)");
	
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

bool NodeDB::ExecStep(Query::Enum val)
{
	return ExecStep(get_Statement(val));

}

sqlite3_stmt* NodeDB::get_Statement(Query::Enum val)
{
	assert(val < _countof(m_pPrep));
	if (!m_pPrep[val])
	{
		const char* szTail;
		int nRet = sqlite3_prepare_v2(m_pDb, s_szSql[val], -1, m_pPrep + val, &szTail);
		TestRet(nRet);
		assert(m_pPrep[val]);
	}

	return m_pPrep[val];
}


int NodeDB::get_RowsChanged() const
{
	return sqlite3_changes(m_pDb);
}

int64_t NodeDB::get_LastInsertRowID() const
{
	return sqlite3_last_insert_rowid(m_pDb);
}

void NodeDB::ParamIntSet(int ID, int val)
{
	Recordset rs(*this, Query::ParamUpd);
	rs.put(Query::Params::count, ID);
	rs.put(Query::Params::ParamInt, val);
	rs.FetchRow();

	if (!get_RowsChanged())
	{
		rs.Reset(Query::ParamIns);

		rs.put(0, ID);
		rs.put(Query::Params::ParamInt + 1, val);
		rs.FetchRow();
	}
}

bool NodeDB::ParamIntGet(int ID, int& val)
{
	Recordset rs(*this, Query::ParamGet);
	rs.put(0, ID);

	if (!rs.FetchRow())
		return false;

	rs.get(Query::Params::ParamInt, val);
	return true;
}

int NodeDB::ParamIntGetDef(int ID, int def /* = 0 */)
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
	db.ExecStep(Query::Begin);
	m_pDB = &db;
}

void NodeDB::Transaction::Commit()
{
	assert(m_pDB);
	m_pDB->ExecStep(Query::Commit);
	m_pDB = NULL;
}

void NodeDB::Transaction::Rollback()
{
	if (m_pDB)
	{
		try {
			m_pDB->ExecStep(Query::Rollback);
		} catch (std::exception&) {
			// TODO: DB is compromised!
		}
		m_pDB = NULL;
	}
}

int64_t NodeDB::get_State(const Block::SystemState::ID& id, Block::SystemState::Full* pOut /* = NULL */)
{
	Recordset rs(*this, Query::StateGet);
	rs.put(Query::States::Height, (int64_t) id.m_Height);
	rs.put_As(Query::States::Hash, id.m_Hash);

	if (!rs.FetchRow())
		return 0;

	if (pOut)
	{
		rs.get(Query::States::Height, (int64_t&) pOut->m_Height);
		rs.get_As(Query::States::Hash, pOut->m_Hash);
		rs.get_As(Query::States::HashPrev, pOut->m_HashPrev);
		rs.get(Query::States::Difficulty, (int64_t&) pOut->m_Difficulty);
		rs.get(Query::States::Timestamp, (int64_t&) pOut->m_TimeStamp);
		rs.get_As(Query::States::HashUtxos, pOut->m_Utxos);
		rs.get_As(Query::States::HashKernels, pOut->m_Kernels);
		//rs.get(Query::States::StateFlags, 0);
		//rs.get(Query::States::CountNext, 0);
	}

	int64_t rowid;
	rs.get(Query::States::count, rowid);
	assert(rowid);
	return rowid;
}

int64_t NodeDB::InsertState(const Block::SystemState::Full& s)
{
	Recordset rs(*this, Query::StateIns);
	rs.put(Query::States::Height, (int64_t) s.m_Height);
	rs.put_As(Query::States::Hash, s.m_Hash);
	rs.put_As(Query::States::HashPrev, s.m_HashPrev);
	rs.put(Query::States::Difficulty, (int64_t) s.m_Difficulty);
	rs.put(Query::States::Timestamp, (int64_t) s.m_TimeStamp);
	rs.put_As(Query::States::HashUtxos, s.m_Utxos);
	rs.put_As(Query::States::HashKernels, s.m_Kernels);
	rs.put(Query::States::StateFlags, 0);
	rs.put(Query::States::CountNext, 0);

	rs.FetchRow();
	return get_LastInsertRowID();
}

} // namespace beam
