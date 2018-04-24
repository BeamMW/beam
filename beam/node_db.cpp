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
#define TblStates_StateFlags	"StateFlags"
#define TblStates_RowPrev		"RowPrev"
#define TblStates_CountNext		"CountNext"
#define TblStates_PoW			"PoW"
#define TblStates_BlindOffset	"BlindOffset"
#define TblStates_Mmr			"Mmr"
#define TblStates_Body			"Body"




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
	TestRet(sqlite3_config(SQLITE_CONFIG_SINGLETHREAD));
	TestRet(sqlite3_open(szPath, &m_pDb));

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
		"[" TblStates_StateFlags	"] INTEGER NOT NULL,"
		"[" TblStates_RowPrev		"] INTEGER,"
		"[" TblStates_CountNext		"] INTEGER NOT NULL,"
		"[" TblStates_PoW			"] BLOB,"
		"[" TblStates_BlindOffset	"] BLOB,"
		"[" TblStates_Mmr			"] BLOB,"
		"[" TblStates_Body			"] BLOB,"
		"PRIMARY KEY (" TblStates_Height "," TblStates_Hash "),"
		"FOREIGN KEY (" TblStates_RowPrev ") REFERENCES " TblStates "(OID))");
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

void NodeDB::ParamIntSet(uint32_t ID, uint32_t val)
{
	Recordset rs(*this, Query::ParamIntUpd, "UPDATE " TblParams " SET " TblParams_Int "=? WHERE " TblParams_ID "=?");
	rs.put(0, val);
	rs.put(1, ID);
	rs.FetchRow();

	if (!get_RowsChanged())
	{
		rs.Reset(Query::ParamIntIns, "INSERT INTO " TblParams " (" TblParams_ID ", " TblParams_Int ") VALUES(?,?)");

		rs.put(0, ID);
		rs.put(1, val);
		rs.FetchRow();
	}
}

bool NodeDB::ParamIntGet(uint32_t ID, uint32_t& val)
{
	Recordset rs(*this, Query::ParamIntGet, "SELECT " TblParams_Int " FROM " TblParams " WHERE " TblParams_ID "=?");
	rs.put(0, ID);

	if (!rs.FetchRow())
		return false;

	rs.get(0, val);
	return true;
}

uint32_t NodeDB::ParamIntGetDef(int ID, uint32_t def /* = 0 */)
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

#define StateCvt_Key(macro, sep) \
	macro(Height,		m_Height) sep \
	macro(Hash,			m_Hash)

#define StateCvt_Fields(macro, sep) \
	macro(HashPrev,		m_HashPrev) sep \
	macro(Difficulty,	m_Difficulty) sep \
	macro(Timestamp,	m_TimeStamp) sep \
	macro(HashUtxos,	m_Utxos) sep \
	macro(HashKernels,	m_Kernels)

#define StateCvt_All(macro, sep) \
	StateCvt_Key(macro, sep) sep \
	StateCvt_Fields(macro, sep)

#define THE_MACRO_NOP0
#define THE_MACRO_AND " AND "

uint64_t NodeDB::get_State(const Block::SystemState::ID& id, Block::SystemState::Full* pOut /* = NULL */)
{
#define THE_MACRO_1(dbname, extname) "," TblStates_##dbname
#define THE_MACRO_2(dbname, extname) TblStates_##dbname "=?"

	Recordset rs(*this, Query::StateGet, "SELECT rowid" StateCvt_Fields(THE_MACRO_1, THE_MACRO_NOP0)
		" FROM " TblStates
		" WHERE " StateCvt_Key(THE_MACRO_2, THE_MACRO_AND));

#undef THE_MACRO_1
#undef THE_MACRO_2

	int iCol = 0;

#define THE_MACRO_1(dbname, extname) rs.put(iCol++, id.extname);
	StateCvt_Key(THE_MACRO_1, THE_MACRO_NOP0)
#undef THE_MACRO_1

	if (!rs.FetchRow())
		return 0;

	uint64_t rowid;
	rs.get(0, rowid);
	assert(rowid);

	if (pOut)
	{
		iCol = 1;

#define THE_MACRO_1(dbname, extname) rs.get(iCol++, pOut->extname);
		StateCvt_Fields(THE_MACRO_1, THE_MACRO_NOP0)
#undef THE_MACRO_1

		// common fields
		Block::SystemState::ID& trg = *pOut;
		trg = id;

	}
	return rowid;
}

uint64_t NodeDB::InsertState(const Block::SystemState::Full& s)
{
#define THE_MACRO_1(dbname, extname) TblStates_##dbname ","
#define THE_MACRO_2(dbname, extname) "?,"

	Recordset rs(*this, Query::StateIns, "INSERT INTO " TblStates
		" (" StateCvt_All(THE_MACRO_1, THE_MACRO_NOP0) TblStates_StateFlags "," TblStates_CountNext ")"
		" VALUES (" StateCvt_All(THE_MACRO_2, THE_MACRO_NOP0) "0,0)");

#undef THE_MACRO_1
#undef THE_MACRO_2

	int iCol = 0;

#define THE_MACRO_1(dbname, extname) rs.put(iCol++, s.extname);
	StateCvt_All(THE_MACRO_1, THE_MACRO_NOP0)
#undef THE_MACRO_1
		
	rs.FetchRow();
	return get_LastInsertRowID();
}

} // namespace beam
