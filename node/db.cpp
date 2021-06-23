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
#include <algorithm> // sort
#include "../core/peer_manager.h"
#include "../utility/logger.h"
#include "../utility/byteorder.h"
#include <algorithm>

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
#define TblStates_Rollback		"Mmr" // For historical reasons it was used for states MMR. Not it's a rollback data
#define TblStates_BodyP			"Perishable"
#define TblStates_BodyE			"Ethernal"
#define TblStates_Peer			"Peer"
#define TblStates_ChainWork		"ChainWork"
#define TblStates_Txos			"Txos"
#define TblStates_Extra			"Extra"
#define TblStates_Inputs		"Inputs"

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

#define TblPeer					"Peers"
#define TblPeer_Key				"Key"
#define TblPeer_Rating			"Rating"
#define TblPeer_Addr			"Address"
#define TblPeer_LastSeen		"LastSeen"

#define TblBbs					"Bbs"
#define TblBbs_ID				"ID"
#define TblBbs_Key				"Key"
#define TblBbs_Channel			"Channel"
#define TblBbs_Time				"Time"
#define TblBbs_Msg				"Message"
#define TblBbs_Nonce			"Nonce"

#define TblDummy				"Dummies"
#define TblDummy_ID				"ID"
#define TblDummy_SpendHeight	"SpendHeight"

#define TblTxo					"Txo"
#define TblTxo_ID				"ID"
#define TblTxo_Value			"Value"
#define TblTxo_SpendHeight		"SpendHeight"

#define TblStreams				"Streams"
#define TblStream_ID			"ID"
#define TblStream_Value			"Value"

#define TblUnique				"UniqueStorage"
#define TblUnique_Key			"Key"
#define TblUnique_Value			"Value"

#define TblAssets				"Assets"
#define TblAssets_ID			"ID"
#define TblAssets_Owner			"Owner"
#define TblAssets_Value			"Value"
#define TblAssets_Data			"MetaData"
#define TblAssets_LockHeight	"LockHeight"

#define TblAssetEvts			"AssetsEvents"
#define TblAssetEvts_ID			"ID"
#define TblAssetEvts_Height		"Height"
#define TblAssetEvts_Index		"Seq"
#define TblAssetEvts_Data		"Data"

#define TblShieldedStatistic			"ShieldedStatistic"
#define TblShieldedStatistic_Height		"Height"
#define TblShieldedStatistic_OutCount	"OutCount"

#define TblContracts			"Contracts"
#define TblContracts_Key		"Key"
#define TblContracts_Value		"Value"

#define TblContractLogs			"ContractLogs"
#define TblContractLogs_Pos		"Pos"
#define TblContractLogs_Key		"Key"
#define TblContractLogs_Data	"Data"

#define TblCache				"Cache"
#define TblCache_Key			"Key"
#define TblCache_Data			"Data"
#define TblCache_LastHit		"Hit"

#define TblKrnInfo				"KrnInfo"
#define TblKrnInfo_Key			"Key"
#define TblKrnInfo_Data			"Data"

NodeDB::NodeDB()
	:m_pDb(nullptr)
{

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
	// Currently all DB errors are defined as corruption
	CorruptionException exc;
	exc.m_sErr = sz;
	throw exc;
}

void NodeDB::ThrowInconsistent()
{
	ThrowError("data inconcistent");
}

void NodeDB::Statement::Close()
{
	if (m_pStmt)
	{
		sqlite3_finalize(m_pStmt); // don't care about retval
		m_pStmt = nullptr;
	}
}

void NodeDB::Close()
{
	if (m_pDb)
	{
		for (size_t i = 0; i < _countof(m_pPrep); i++)
			m_pPrep[i].Close();

        BEAM_VERIFY(SQLITE_OK == sqlite3_close(m_pDb));
		m_pDb = NULL;
	}
}

NodeDB::Recordset::Recordset()
	:m_pStmt(nullptr)
	,m_pDB(nullptr)
{
}

NodeDB::Recordset::Recordset(NodeDB& db, Query::Enum val, const char* sql)
	:m_pStmt(nullptr)
{
	InitInternal(db, val, sql);
}

void NodeDB::Recordset::InitInternal(NodeDB& db, Query::Enum val, const char* sql)
{
	m_pDB = &db;
	m_pStmt = db.get_Statement(val, sql);
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

void NodeDB::Recordset::Reset(NodeDB& db, Query::Enum val, const char* sql)
{
	Reset();
	InitInternal(db, val, sql);
}

bool NodeDB::Recordset::Step()
{
	return m_pDB->ExecStep(m_pStmt);
}

void NodeDB::Recordset::StepStrict()
{
	if (!Step())
		ThrowError("not found");
}

bool NodeDB::Recordset::StepModifySafe()
{
	int nVal = m_pDB->ExecStepRaw(m_pStmt);
	switch (nVal)
	{

	default:
		m_pDB->ThrowSqliteError(nVal);
		// no break

	case SQLITE_DONE:
		return true;

	case SQLITE_CONSTRAINT:
		return false;
	}
}

bool NodeDB::Recordset::IsNull(int col)
{
	return SQLITE_NULL == sqlite3_column_type(m_pStmt, col);
}

void NodeDB::Recordset::putNull(int col)
{
	m_pDB->TestRet(sqlite3_bind_null(m_pStmt, col+1));
}

void NodeDB::Recordset::put(int col, uint32_t x)
{
	m_pDB->TestRet(sqlite3_bind_int(m_pStmt, col+1, x));
}

void NodeDB::Recordset::put(int col, uint64_t x)
{
	m_pDB->TestRet(sqlite3_bind_int64(m_pStmt, col+1, x));
}

void NodeDB::Recordset::put(int col, const Blob& x)
{
	// According to our convention empty blob is NOT NULL, it should be an empty BLOB field.
	// During initialization from buffer, if the buffer size is 0 - the x.p is left uninitialized.
	//
	// In sqlite code if x.p is NULL - it would treat the field as NULL, rather than an empty blob.
	// And if the uninitialized x.p is occasionally NULL - we get wrong behavior.
	//
	// Hence - we work this around, use `this`, as an arbitrary non-NULL pointer
	const void* pPtr = x.n ? x.p : this;
	m_pDB->TestRet(sqlite3_bind_blob(m_pStmt, col+1, pPtr, x.n, NULL));
}

void NodeDB::Recordset::put(int col, const char* sz)
{
	m_pDB->TestRet(sqlite3_bind_text(m_pStmt, col+1, sz, -1, NULL));
}

void NodeDB::Recordset::put(int col, const Key::ID& kid, Key::ID::Packed& p)
{
	p = kid;
	put(col, Blob(&p, sizeof(p)));
}

void NodeDB::Recordset::putZeroBlob(int col, uint32_t nSize)
{
	m_pDB->TestRet(sqlite3_bind_zeroblob(m_pStmt, col + 1, nSize));
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

void NodeDB::Recordset::get(int col, Key::ID& kid)
{
	Key::ID::Packed p;
	get_As(col, p);
	kid = p;
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
	// Attempt to fix the "busy" error when PC goes to sleep and then awakes. Try the busy handler with non-zero timeout (maybe a single retry would be enough)
	sqlite3_busy_timeout(m_pDb, 5000);

	ExecTextOut("PRAGMA locking_mode = EXCLUSIVE");
	ExecTextOut("PRAGMA journal_size_limit=1048576"); // limit journal file, otherwise it may remain huge even after tx commit, until the app is closed

	bool bCreate;
	{
		Recordset rs(*this, Query::Scheme, "SELECT name FROM sqlite_master WHERE type='table' AND name=?");
		rs.put(0, TblParams);
		bCreate = !rs.Step();
	}

	const uint64_t nVersionTop = 30;


	Transaction t(*this);

	if (bCreate)
	{
		Create();
		ParamIntSet(ParamID::DbVer, nVersionTop);
	}
	else
	{
		uint64_t nVer = ParamIntGetDef(ParamID::DbVer);
		switch (nVer)
		{
		case 17: // before UTXO image
			// no break;

		case 18: // ridiculous rating values, no States.Inputs column, Txo.SpendHeight is still indexed

			LOG_INFO() << "DB migrate from " << 18;
			MigrateFrom18();
			// no break;

		case 19: // before Shielded shards
			// ignore
			// no break;

		case 20: // Deprecated Shielded table created.
			CreateTables20();

			LOG_INFO() << "DB migrate from " << 20;
			MigrateFrom20();
			// no break;

		case 21:
			CreateTables21();
			// no break;

		case 22:
			CreateTables22();
			// no break;

		case 23:
			CreateTables23();
			// no break;

		case 24:
		case 25:
			ParamDelSafe(ParamID::Deprecated_1);
			ParamDelSafe(ParamID::Deprecated_2);
			ParamDelSafe(ParamID::Deprecated_3);
			// no break;

		case 26: // ShieldedState stream added
			// no break;

		case 27:
			CreateTables27();
			// no break;

		case 28:
			CreateTables28();
			// no break;

		case 29: // Block interpretation nKrnIdx fixed to match KrnWalker's
			ParamIntSet(ParamID::Flags1, ParamIntGetDef(ParamID::Flags1) | Flags1::PendingRebuildNonStd);
			CreateTables29();

			// no break;

			ParamIntSet(ParamID::DbVer, nVersionTop);

		case nVersionTop:
			break;

		default:
			if (nVer < nVersionTop)
				throw NodeDBUpgradeException("Node upgrade is not supported. Please, remove node.db and tempmb files");

			if (nVer > nVersionTop)
				throw NodeDBUpgradeException("Unsupported db version");
		}
	}

	t.Commit();
}

void NodeDB::CheckIntegrity()
{
	std::string s = ExecTextOut("PRAGMA integrity_check");
	if (s != "ok")
		ThrowError(("sqlite integrity: " + s).c_str());
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
		"[" TblStates_Rollback		"] BLOB,"
		"[" TblStates_BodyP			"] BLOB,"
		"[" TblStates_BodyE			"] BLOB,"
		"[" TblStates_Peer			"] BLOB,"
		"[" TblStates_ChainWork		"] BLOB,"
		"[" TblStates_Txos			"] INTEGER,"
		"[" TblStates_Extra			"] BLOB,"
		"[" TblStates_Inputs		"] BLOB,"
		"PRIMARY KEY (" TblStates_Height "," TblStates_Hash "),"
		"FOREIGN KEY (" TblStates_RowPrev ") REFERENCES " TblStates "(OID))");

	ExecQuick("CREATE INDEX [Idx" TblStates "Wrk] ON [" TblStates "] ([" TblStates_ChainWork "]);");
	ExecQuick("CREATE INDEX [Idx" TblStates TblStates_Txos "] ON [" TblStates "] ([" TblStates_Txos "]);");

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
		"[" TblEvents_Key		"] BLOB NOT NULL)");

	ExecQuick("CREATE INDEX [Idx" TblEvents "] ON [" TblEvents "] ([" TblEvents_Height "],[" TblEvents_Body "]);");
	ExecQuick("CREATE INDEX [Idx" TblEvents TblEvents_Key "] ON [" TblEvents "] ([" TblEvents_Key "]);");

	ExecQuick("CREATE TABLE [" TblPeer "] ("
		"[" TblPeer_Key			"] BLOB NOT NULL,"
		"[" TblPeer_Rating		"] INTEGER NOT NULL,"
		"[" TblPeer_Addr		"] INTEGER NOT NULL,"
		"[" TblPeer_LastSeen	"] INTEGER NOT NULL)");

	ExecQuick("CREATE TABLE [" TblBbs "] ("
		"[" TblBbs_ID		"] INTEGER PRIMARY KEY AUTOINCREMENT,"
		"[" TblBbs_Key		"] BLOB NOT NULL,"
		"[" TblBbs_Channel	"] INTEGER NOT NULL,"
		"[" TblBbs_Time		"] INTEGER NOT NULL,"
		"[" TblBbs_Msg		"] BLOB NOT NULL,"
		"[" TblBbs_Nonce	"] INTEGER)");

	ExecQuick("CREATE INDEX [Idx" TblBbs "CSeq] ON [" TblBbs "] ([" TblBbs_Channel "],[" TblBbs_ID "]);");
	ExecQuick("CREATE INDEX [Idx" TblBbs "TSeq] ON [" TblBbs "] ([" TblBbs_Time "],[" TblBbs_ID "]);");
	ExecQuick("CREATE INDEX [Idx" TblBbs "Key] ON [" TblBbs "] ([" TblBbs_Key "]);");

	ExecQuick("CREATE TABLE [" TblDummy "] ("
		"[" TblDummy_ID				"] BLOB NOT NULL PRIMARY KEY,"
		"[" TblDummy_SpendHeight	"] INTEGER NOT NULL)");

	ExecQuick("CREATE INDEX [Idx" TblDummy "H] ON [" TblDummy "] ([" TblDummy_SpendHeight "])");

	ExecQuick("CREATE TABLE [" TblTxo "] ("
		"[" TblTxo_ID				"] INTEGER NOT NULL PRIMARY KEY,"
		"[" TblTxo_Value			"] BLOB NOT NULL,"
		"[" TblTxo_SpendHeight		"] INTEGER)");

	CreateTables20();
	CreateTables21();
	CreateTables22();
	CreateTables23();
	CreateTables27();
	CreateTables28();
	CreateTables29();
}

void NodeDB::CreateTables20()
{
	ExecQuick("CREATE TABLE [" TblStreams "] ("
		"[" TblStream_ID			"] INTEGER NOT NULL PRIMARY KEY,"
		"[" TblStream_Value			"] BLOB NOT NULL)");

	ExecQuick("CREATE TABLE [" TblUnique "] ("
		"[" TblUnique_Key			"] BLOB NOT NULL PRIMARY KEY,"
		"[" TblUnique_Value			"] BLOB) WITHOUT ROWID");

	ExecQuick("CREATE TABLE [" TblAssets "] ("
		"[" TblAssets_ID			"] INTEGER NOT NULL PRIMARY KEY,"
		"[" TblAssets_Owner			"] BLOB,"
		"[" TblAssets_Data			"] BLOB,"
		"[" TblAssets_LockHeight	"] INTEGER,"
		"[" TblAssets_Value			"] BLOB)");

	ExecQuick("CREATE INDEX [Idx" TblAssets "Own] ON [" TblAssets "] ([" TblAssets_Owner "])");
}

void NodeDB::CreateTables21()
{
	ExecQuick("CREATE TABLE [" TblAssetEvts "] ("
		"[" TblAssetEvts_ID			"] INTEGER NOT NULL,"
		"[" TblAssetEvts_Height		"] INTEGER NOT NULL,"
		"[" TblAssetEvts_Index		"] INTEGER NOT NULL,"
		"[" TblAssetEvts_Data		"] BLOB)");

	ExecQuick("CREATE INDEX [Idx" TblAssetEvts "_1" "] ON [" TblAssetEvts "] ([" TblAssetEvts_ID "],[" TblAssetEvts_Height  "],[" TblAssetEvts_Index "]);");
	ExecQuick("CREATE INDEX [Idx" TblAssetEvts "_2" "] ON [" TblAssetEvts "] ([" TblAssetEvts_Height  "],[" TblAssetEvts_Index "]);");
}

void NodeDB::CreateTables22()
{
	ExecQuick("CREATE TABLE [" TblShieldedStatistic "] ("
		"[" TblShieldedStatistic_Height"] INTEGER NOT NULL PRIMARY KEY,"
		"[" TblShieldedStatistic_OutCount"] INTEGER NOT NULL)");
}

void NodeDB::CreateTables23()
{
	ExecQuick("CREATE TABLE [" TblContracts "] ("
		"[" TblContracts_Key		"] BLOB NOT NULL PRIMARY KEY,"
		"[" TblContracts_Value		"] BLOB NOT NULL)");
}

void NodeDB::CreateTables27()
{
	ExecQuick("CREATE TABLE [" TblContractLogs "] ("
		"[" TblContractLogs_Pos			"] BLOB NOT NULL PRIMARY KEY,"
		"[" TblContractLogs_Key			"] BLOB NOT NULL,"
		"[" TblContractLogs_Data		"] BLOB NOT NULL"
		") WITHOUT ROWID");

	ExecQuick("CREATE INDEX [Idx" TblContractLogs "_Key" "] ON [" TblContractLogs "] ([" TblContractLogs_Key "],[" TblContractLogs_Pos "]);");
}

void NodeDB::CreateTables28()
{
	ExecQuick("CREATE TABLE [" TblCache "] ("
		"[" TblCache_Key		"] BLOB NOT NULL,"
		"[" TblCache_Data		"] BLOB NOT NULL,"
		"[" TblCache_LastHit	"] INTEGER NOT NULL)");

	ExecQuick("CREATE INDEX [Idx" TblCache "_Key" "] ON [" TblCache "] ([" TblCache_Key "]);");
	ExecQuick("CREATE INDEX [Idx" TblCache "_Hit" "] ON [" TblCache "] ([" TblCache_LastHit "]);");
}

void NodeDB::CreateTables29()
{
	ExecQuick("CREATE TABLE [" TblKrnInfo "] ("
		"[" TblKrnInfo_Key		"] INTEGER NOT NULL PRIMARY KEY,"
		"[" TblKrnInfo_Data		"] BLOB NOT NULL)");
}

void NodeDB::Vacuum()
{
	ExecQuick("VACUUM");
}

void NodeDB::ExecQuick(const char* szSql)
{
	int n = sqlite3_total_changes(m_pDb);
	TestRet(sqlite3_exec(m_pDb, szSql, NULL, NULL, NULL));

	if (sqlite3_total_changes(m_pDb) != n)
		OnModified();
}

std::string NodeDB::ExecTextOut(const char* szSql)
{
	int n = sqlite3_total_changes(m_pDb);
	TestRet(sqlite3_exec(m_pDb, szSql, NULL, NULL, NULL));

	Statement s;
	Prepare(s, szSql);

	std::string sRes;

	if (ExecStep(s.m_pStmt))
		sRes = (const char*) sqlite3_column_text(s.m_pStmt, 0);

	if (sqlite3_total_changes(m_pDb) != n)
		OnModified();

	return sRes;
}

int NodeDB::ExecStepRaw(sqlite3_stmt* pStmt)
{
	int n = sqlite3_total_changes(m_pDb);

	int nVal = sqlite3_step(pStmt);

	if (sqlite3_total_changes(m_pDb) != n)
		OnModified();

	return nVal;
}

bool NodeDB::ExecStep(sqlite3_stmt* pStmt)
{
	int nVal = ExecStepRaw(pStmt);
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

void NodeDB::Prepare(Statement& s, const char* szSql)
{
	assert(!s.m_pStmt);

	const char* szTail;
	int nRet = sqlite3_prepare_v2(m_pDb, szSql, -1, &s.m_pStmt, &szTail);
	TestRet(nRet);
	assert(s.m_pStmt);
}

sqlite3_stmt* NodeDB::get_Statement(Query::Enum val, const char* sql)
{
	assert(val < _countof(m_pPrep));
	Statement& s = m_pPrep[val];

	if (!s.m_pStmt)
		Prepare(s, sql);
	return s.m_pStmt;
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
	Recordset rs(*this, Query::ParamSet, "INSERT OR REPLACE INTO " TblParams " (" TblParams_ID "," TblParams_Int "," TblParams_Blob ") VALUES(?,?,?)");
	rs.put(0, ID);
	if (p0)
		rs.put(1, *p0);
	if (p1)
		rs.put(2, *p1);
	rs.Step();
}

void NodeDB::ParamIntSet(uint32_t ID, uint64_t val)
{
	ParamSet(ID, &val, nullptr);
}

bool NodeDB::ParamGet(uint32_t ID, uint64_t* p0, Blob* p1, ByteBuffer* p2 /* = NULL */)
{
	Recordset rs(*this, Query::ParamGet, "SELECT " TblParams_Int "," TblParams_Blob " FROM " TblParams " WHERE " TblParams_ID "=?");
	rs.put(0, ID);

	if (!rs.Step())
		return false;

	if (p0)
		rs.get(0, *p0);
	if (p1)
	{
		if (rs.IsNull(1))
			return false;

		memcpy(Cast::NotConst(p1->p), rs.get_BlobStrict(1, p1->n), p1->n);
	}
	if (p2)
		rs.get(1, *p2);

	return true;
}

uint64_t NodeDB::ParamIntGetDef(uint32_t ID, uint64_t def /* = 0 */)
{
	ParamGet(ID, &def, NULL);
	return def;
}

bool NodeDB::ParamDelSafe(uint32_t ID)
{
	Recordset rs(*this, Query::ParamDel, "DELETE FROM " TblParams " WHERE " TblParams_ID "=?");
	rs.put(0, ID);
	rs.Step();

	return !!get_RowsChanged();
}

NodeDB::Transaction::Transaction(NodeDB* pDB)
	:m_pDB(NULL)
{
	if (pDB)
		Start(*pDB);
}

NodeDB::Transaction::~Transaction()
{
	if (std::uncaught_exceptions())
	{
		try {
			Rollback();
		}
		catch (...) {
			// ignore
		}
	}
	else
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
		m_pDB->ExecStep(Query::Rollback, "ROLLBACK");
		m_pDB = nullptr;
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

uint64_t NodeDB::InsertState(const Block::SystemState::Full& s, const PeerID& peer)
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
	rs.Reset(*this, Query::StateGetNextFCount, "SELECT COUNT() FROM " TblStates " WHERE " TblStates_Height "=? AND " TblStates_HashPrev "=? AND (" TblStates_Flags " & ?)");
	rs.put(0, s.m_Height + 1);
	rs.put(1, hash);
	rs.put(2, StateFlags::Functional);

    BEAM_VERIFY(rs.Step());
	rs.get(0, nCountNextF);

	// Insert row

#define THE_MACRO_1(dbname, extname) TblStates_##dbname ","
#define THE_MACRO_2(dbname, extname) "?,"

	rs.Reset(*this, Query::StateIns, "INSERT INTO " TblStates
		" (" TblStates_Hash "," StateCvt_Fields(THE_MACRO_1, THE_MACRO_NOP0) TblStates_Flags "," TblStates_CountNext "," TblStates_CountNextF "," TblStates_RowPrev "," TblStates_Peer ")"
		" VALUES(?," StateCvt_Fields(THE_MACRO_2, THE_MACRO_NOP0) "0,0,?,?,?)");

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
	iCol++;

	rs.put(iCol, peer);

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
	rs.Reset(*this, Query::StateUpdPrevRow, "UPDATE " TblStates " SET " TblStates_RowPrev "=? WHERE " TblStates_Height "=? AND " TblStates_HashPrev "=?");
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

	rs.Reset(*this, Query::StateDel, "DELETE FROM " TblStates " WHERE rowid=?");
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

uint64_t NodeDB::FindActiveStateStrict(Height h)
{
	Recordset rs(*this, Query::StateFindWithFlag, "SELECT rowid FROM " TblStates " WHERE " TblStates_Height "=? AND (" TblStates_Flags " & ?)");
	rs.put(0, h);
	rs.put(1, StateFlags::Active);
	rs.StepStrict();

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

void NodeDB::EnumSystemStatesBkwd(WalkerSystemState& x, const StateID& sid)
{
#define THE_MACRO_1(dbname, extname) TblStates_##dbname
	x.m_Rs.Reset(*this, Query::EnumSystemStatesBkwd,
		"SELECT rowid," TblStates_RowPrev "," StateCvt_Fields(THE_MACRO_1, THE_MACRO_COMMA_S)
		" FROM " TblStates " WHERE " TblStates_Height "<=? ORDER BY " TblStates_Height " DESC");
#undef THE_MACRO_1

	x.m_RowTrg = sid.m_Row;

	x.m_Rs.put(0, sid.m_Height);
}

bool NodeDB::WalkerSystemState::MoveNext()
{
	while (true)
	{
		if (!m_Rs.Step())
			return false;

		uint64_t rowID;
		m_Rs.get(0, rowID);

		if (m_RowTrg == rowID)
			break;
	}

	m_Rs.get(1, m_RowTrg);

	int iCol = 2;

#define THE_MACRO_1(dbname, extname) m_Rs.get(iCol++, m_State.extname);
	StateCvt_Fields(THE_MACRO_1, THE_MACRO_NOP0)
#undef THE_MACRO_1

	return true;
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

uint32_t NodeDB::get_StateExtra(uint64_t rowid, void* pOut, uint32_t nSize)
{
	Recordset rs(*this, Query::StateGetExtra, "SELECT " TblStates_Extra " FROM " TblStates " WHERE rowid=?");
	rs.put(0, rowid);
	rs.StepStrict();

	memset0(pOut, nSize);

	if (rs.IsNull(0))
		return 0;

	// the actual data size may be less than requested
	Blob b;
	rs.get(0, b);

	memcpy(pOut, b.p, std::min(b.n, nSize));
	return b.n;
}

void NodeDB::set_StateInputs(uint64_t rowid, StateInput* p, size_t n)
{
	Recordset rs(*this, Query::StateSetInputs, "UPDATE " TblStates " SET " TblStates_Inputs "=? WHERE rowid=?");
	if (n)
		rs.put(0, Blob(p, static_cast<uint32_t>(sizeof(StateInput) * n)));
	rs.put(1, rowid);
	rs.Step();
	TestChanged1Row();
}


bool NodeDB::get_StateInputs(uint64_t rowid, std::vector<StateInput>& v)
{
	Recordset rs(*this, Query::StateGetInputs, "SELECT " TblStates_Inputs " FROM " TblStates " WHERE rowid=?");
	rs.put(0, rowid);
	rs.StepStrict();

	Blob blob;
	rs.get(0, blob); // if NULL empty blob will be returned

	v.resize(blob.n / sizeof(StateInput));
	if (v.empty())
		return false;

	memcpy(&v.front(), blob.p, v.size() * sizeof(StateInput)); // don't use blob size, it may be bigger if blob.n isn't multiple of sizeof(StateInput)
	return true;
}

void NodeDB::StateInput::Set(TxoID id, const ECC::Point& pt)
{
	Set(id, pt.m_X, pt.m_Y);
}

void NodeDB::StateInput::Set(TxoID id, const ECC::uintBig& x, uint8_t y)
{
	m_Txo_AndY = id;
	m_CommX = x;
	if (y)
		m_Txo_AndY |= s_Y;
}

TxoID NodeDB::StateInput::get_ID() const
{
	return m_Txo_AndY & ~s_Y;
}

void NodeDB::StateInput::Get(ECC::Point& pt) const
{
	pt.m_X = m_CommX;
	pt.m_Y = (s_Y & m_Txo_AndY) ? 1 : 0;
}

bool NodeDB::StateInput::IsLess(const StateInput& x1, const StateInput& x2)
{
	ECC::Point pt1, pt2;
	x1.Get(pt1);
	x2.Get(pt2);
	return pt1 < pt2;
}

void NodeDB::set_StateTxosAndExtra(uint64_t rowid, const TxoID* pId, const Blob* pExtra, const Blob* pRB)
{
	Recordset rs(*this, Query::StateSetTxosAndExtra, "UPDATE " TblStates " SET " TblStates_Txos "=?," TblStates_Extra "=?," TblStates_Rollback "=? WHERE rowid=?");
	if (pId)
		rs.put(0, *pId);
	if (pExtra)
		rs.put(1, *pExtra);
	if (pRB)
		rs.put(2, *pRB);
	rs.put(3, rowid);
	rs.Step();
	TestChanged1Row();
}

TxoID NodeDB::get_StateTxos(uint64_t rowid)
{
	Recordset rs(*this, Query::StateGetTxos, "SELECT " TblStates_Txos " FROM " TblStates " WHERE rowid=?");
	rs.put(0, rowid);
	rs.StepStrict();

	if (rs.IsNull(0))
		return MaxHeight;

	TxoID id;
	rs.get(0, id);
	return id;
}

TxoID NodeDB::FindStateByTxoID(StateID& sid, TxoID id0)
{
	Recordset rs(*this, Query::StateFindByTxos, "SELECT rowid," TblStates_Height "," TblStates_Txos " FROM " TblStates
		" WHERE " TblStates_Txos ">? AND " TblStates_Flags "& ? != 0  ORDER BY " TblStates_Txos " ASC LIMIT 1");
	rs.put(0, id0);
	rs.put(1, StateFlags::Active);
	rs.StepStrict();

	rs.get(0, sid.m_Row);
	rs.get(1, sid.m_Height);
	rs.get(2, id0);

	return id0;
}

void NodeDB::SetStateBlock(uint64_t rowid, const Blob& bodyP, const Blob& bodyE, const PeerID& peer)
{
	Recordset rs(*this, Query::StateSetBlock, "UPDATE " TblStates " SET " TblStates_BodyP "=?," TblStates_BodyE "=?," TblStates_Peer "=? WHERE rowid=?");
	if (bodyP.n)
		rs.put(0, bodyP);
	if (bodyE.n)
		rs.put(1, bodyE);
	rs.put(2, peer);
	rs.put(3, rowid);

	rs.Step();
	TestChanged1Row();
}

void NodeDB::GetStateBlock(uint64_t rowid, ByteBuffer* pP, ByteBuffer* pE, ByteBuffer* pRB)
{
	Recordset rs(*this, Query::StateGetBlock, "SELECT " TblStates_BodyP "," TblStates_BodyE "," TblStates_Rollback " FROM " TblStates " WHERE rowid=?");
	rs.put(0, rowid);
	rs.StepStrict();

	if (pP && !rs.IsNull(0))
		rs.get(0, *pP);
	if (pE && !rs.IsNull(1))
		rs.get(1, *pE);
	if (pRB && !rs.IsNull(2))
		rs.get(2, *pRB);
}

void NodeDB::DelStateBlockPP(uint64_t rowid)
{
	Recordset rs(*this, Query::StateDelBlockPP, "UPDATE " TblStates " SET " TblStates_BodyP "=NULL," TblStates_Peer "=NULL WHERE rowid=?");
	rs.put(0, rowid);
	rs.Step();
	TestChanged1Row();
}

void NodeDB::DelStateBlockPPR(uint64_t rowid)
{
	Recordset rs(*this, Query::StateDelBlockPPR, "UPDATE " TblStates " SET " TblStates_BodyP "=NULL," TblStates_Rollback "=NULL," TblStates_Peer "=NULL WHERE rowid=?");
	rs.put(0, rowid);
	rs.Step();
	TestChanged1Row();
}

void NodeDB::DelStateBlockAll(uint64_t rowid)
{
	Recordset rs(*this, Query::StateDelBlockAll, "UPDATE " TblStates
		" SET " TblStates_BodyP "=NULL," TblStates_BodyE "=NULL," TblStates_Rollback "=NULL," TblStates_Peer "=NULL," TblStates_Extra "=NULL," TblStates_Txos "=NULL WHERE rowid=?");
	rs.put(0, rowid);
	rs.Step();
	TestChanged1Row();
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
	
	rs.Reset(*this, Query::Dbg1, "SELECT "
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

	rs.Reset(*this, Query::Dbg2, "SELECT "
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

	rs.Reset(*this, Query::Dbg3, "SELECT "
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

	rs.Reset(*this, Query::Dbg4, "SELECT "
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
	x.m_Rs.Reset(*this, Query::EnumTips, "SELECT " TblTips_Height "," TblTips_State " FROM " TblTips " ORDER BY "  TblTips_Height " ASC," TblTips_State " ASC");
}

void NodeDB::EnumFunctionalTips(WalkerState& x)
{
	x.m_Rs.Reset(*this, Query::EnumFunctionalTips, "SELECT "
		TblStates "." TblStates_Height ","
		TblStates ".rowid"
		" FROM " TblTipsReachable
		" LEFT JOIN " TblStates " ON (" TblTipsReachable "." TblTips_State "=" TblStates ".rowid) "
		" ORDER BY "  TblTipsReachable "." TblTips_ChainWork " DESC");
}

Height NodeDB::get_HeightBelow(Height h)
{
	Recordset rs(*this, Query::FindHeightBelow, "SELECT " TblStates_Height " FROM " TblStates " WHERE " TblStates_Height "<? ORDER BY " TblStates_Height " DESC LIMIT 1");
	rs.put(0, h);

	if (!rs.Step())
		return Rules::HeightGenesis - 1;

	rs.get(0, h);
	return h;
}

void NodeDB::EnumStatesAt(WalkerState& x, Height h)
{
	x.m_Rs.Reset(*this, Query::EnumAtHeight, "SELECT " TblStates_Height ",rowid FROM " TblStates " WHERE " TblStates_Height "=? ORDER BY " TblStates_Hash);
	x.m_Rs.put(0, h);
}

void NodeDB::EnumAncestors(WalkerState& x, const StateID& sid)
{
	x.m_Rs.Reset(*this, Query::EnumAncestors, "SELECT " TblStates_Height ",rowid FROM " TblStates " WHERE " TblStates_Height "=? AND " TblStates_RowPrev "=? ORDER BY " TblStates_Hash);
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
	ParamIntSet(ParamID::CursorRow, sid.m_Row);
	ParamIntSet(ParamID::CursorHeight, sid.m_Height);
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

void NodeDB::InsertEvent(Height h, const Blob& b, const Blob& key)
{
	assert(b.n >= sizeof(EventIndexType));

	Recordset rs(*this, Query::EventIns, "INSERT INTO " TblEvents "(" TblEvents_Height "," TblEvents_Body "," TblEvents_Key ") VALUES (?,?,?)");
	rs.put(0, h);
	rs.put(1, b);
	rs.put(2, key);
	rs.Step();
	TestChanged1Row();
}

void NodeDB::DeleteEventsFrom(Height h)
{
	Recordset rs(*this, Query::EventDel, "DELETE FROM " TblEvents " WHERE " TblEvents_Height ">=?");
	rs.put(0, h);
	rs.Step();
}

void NodeDB::EnumEvents(WalkerEvent& x, Height hMin)
{
	x.m_Rs.Reset(*this, Query::EventEnum, "SELECT " TblEvents_Height "," TblEvents_Body "," TblEvents_Key " FROM " TblEvents " WHERE " TblEvents_Height ">=? ORDER BY " TblEvents_Height " ASC," TblEvents_Body " ASC");
	x.m_Rs.put(0, hMin);
}

void NodeDB::FindEvents(WalkerEvent& x, const Blob& key)
{
	x.m_Rs.Reset(*this, Query::EventFind, "SELECT " TblEvents_Height "," TblEvents_Body "," TblEvents_Key " FROM " TblEvents " WHERE " TblEvents_Key "=? ORDER BY " TblEvents_Height " DESC," TblEvents_Body " DESC");
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

	if (m_Body.n < m_Index.nBytes)
		ThrowInconsistent();

	memcpy(m_Index.m_pData, m_Body.p, m_Index.nBytes);
	((const uint8_t*&) m_Body.p) += m_Index.nBytes;
	m_Body.n -= m_Index.nBytes;

	return true;
}

void NodeDB::EnumPeers(WalkerPeer& x)
{
	x.m_Rs.Reset(*this, Query::PeerEnum, "SELECT " TblPeer_Key "," TblPeer_Rating "," TblPeer_Addr "," TblPeer_LastSeen " FROM " TblPeer);
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

bool NodeDB::EnumBbs(IBbsHistogram& x)
{
	Recordset rs(*this, Query::BbsHistogram, "SELECT " TblBbs_Channel ",COUNT(*) FROM " TblBbs " GROUP BY " TblBbs_Channel " ORDER BY " TblBbs_Channel);
	while (rs.Step())
	{
		BbsChannel ch;
		uint64_t nCount;

		rs.get(0, ch);
		rs.get(1, nCount);

		if (!x.OnChannel(ch, nCount))
			return false;
	}

	return true;
}

void NodeDB::EnumAllBbsSeq(WalkerBbsLite& x)
{
	x.m_Rs.Reset(*this, Query::BbsEnumAllSeq, "SELECT " TblBbs_ID "," TblBbs_Key ",LENGTH(" TblBbs_Msg ") FROM " TblBbs " WHERE " TblBbs_ID ">? ORDER BY " TblBbs_ID);
	x.m_Rs.put(0, x.m_ID);
}

bool NodeDB::WalkerBbsLite::MoveNext()
{
	if (!m_Rs.Step())
		return false;
	m_Rs.get(0, m_ID);
	m_Rs.get(1, m_Key);
	m_Rs.get(2, m_Size);
	return true;
}

void NodeDB::EnumAllBbs(WalkerBbsTimeLen& x)
{
	x.m_Rs.Reset(*this, Query::BbsEnumAll, "SELECT " TblBbs_ID "," TblBbs_Time ",LENGTH(" TblBbs_Msg ") FROM " TblBbs " ORDER BY " TblBbs_ID);
}

bool NodeDB::WalkerBbsTimeLen::MoveNext()
{
	if (!m_Rs.Step())
		return false;
	m_Rs.get(0, m_ID);
	m_Rs.get(1, m_Time);
	m_Rs.get(2, m_Size);
	return true;
}

void NodeDB::get_BbsTotals(BbsTotals& x)
{
	Recordset rs(*this, Query::BbsTotals, "SELECT COUNT(*), SUM(Length(" TblBbs_Msg ")) FROM " TblBbs);
	rs.StepStrict();

	rs.get(0, x.m_Count);
	rs.get(1, x.m_Size);
}

#define TblBbs_InsFieldsListed TblBbs_Key "," TblBbs_Channel "," TblBbs_Time "," TblBbs_Msg "," TblBbs_Nonce
#define TblBbs_AllFieldsListed TblBbs_ID "," TblBbs_InsFieldsListed

void NodeDB::EnumBbsCSeq(WalkerBbs& x)
{
	x.m_Rs.Reset(*this, Query::BbsEnumCSeq, "SELECT " TblBbs_AllFieldsListed " FROM " TblBbs " WHERE " TblBbs_Channel "=? AND " TblBbs_ID ">? ORDER BY " TblBbs_ID);

	x.m_Rs.put(0, x.m_Data.m_Channel);
	x.m_Rs.put(1, x.m_ID);
}

uint64_t NodeDB::get_AutoincrementID(const char* szTable)
{
	Recordset rs(*this, Query::AutoincrementID, "SELECT seq FROM sqlite_sequence WHERE name=?");
	rs.put(0, szTable);
	if (!rs.Step())
		return 0;

	if (rs.IsNull(0))
		return 0;

	uint64_t id;
	rs.get(0, id);
	return id;
}

uint64_t NodeDB::get_BbsLastID()
{
	return get_AutoincrementID(TblBbs);
}

bool NodeDB::WalkerBbs::MoveNext()
{
	if (!m_Rs.Step())
		return false;
	m_Rs.get(0, m_ID);
	m_Rs.get(1, m_Data.m_Key);
	m_Rs.get(2, m_Data.m_Channel);
	m_Rs.get(3, m_Data.m_TimePosted);
	m_Rs.get(4, m_Data.m_Message);
	m_Rs.get(5, m_Data.m_Nonce); // don't care if NULL, would be 0

	return true;
}

bool NodeDB::BbsFind(WalkerBbs& x)
{
	x.m_Rs.Reset(*this, Query::BbsFind, "SELECT " TblBbs_AllFieldsListed " FROM " TblBbs " WHERE " TblBbs_Key "=?");

	x.m_Rs.put(0, x.m_Data.m_Key);
	return x.MoveNext();
}

uint64_t NodeDB::BbsFind(const WalkerBbs::Key& key)
{
	Recordset rs(*this,Query::BbsFindRaw, "SELECT rowid FROM " TblBbs " WHERE " TblBbs_Key "=?");
	rs.put(0, key);
	if (!rs.Step())
		return 0;

	uint64_t id;
	rs.get(0, id);
	return id;
}

void NodeDB::BbsDel(uint64_t id)
{
	Recordset rs(*this, Query::BbsDel, "DELETE FROM " TblBbs " WHERE " TblBbs_ID "=?");
	rs.put(0, id);
	rs.Step();
	TestChanged1Row();
}

uint64_t NodeDB::BbsIns(const WalkerBbs::Data& d)
{
	Recordset rs(*this, Query::BbsIns, "INSERT INTO " TblBbs "(" TblBbs_InsFieldsListed ") VALUES(?,?,?,?,?)");
	rs.put(0, d.m_Key);
	rs.put(1, d.m_Channel);
	rs.put(2, d.m_TimePosted);
	rs.put(3, d.m_Message);
	rs.put(4, d.m_Nonce);

	rs.Step();
	TestChanged1Row();

	return sqlite3_last_insert_rowid(m_pDb);
}

uint64_t NodeDB::BbsFindCursor(Timestamp t)
{
	Recordset rs(*this, Query::BbsFindCursor, "SELECT " TblBbs_ID " FROM " TblBbs " WHERE " TblBbs_Time ">=? ORDER BY " TblBbs_ID " ASC LIMIT 1");
	rs.put(0, t);

	if (!rs.Step())
		return get_BbsLastID() + 1;

	if (rs.IsNull(0))
		return 0;

	uint64_t id;
	rs.get(0, id);
	return id;
}

Timestamp NodeDB::get_BbsMaxTime()
{
	Recordset rs(*this, Query::BbsMaxTime, "SELECT MAX(" TblBbs_Time ") FROM " TblBbs);
	if (!rs.Step())
		return 0;

	Timestamp ret;
	rs.get(0, ret);
	return ret;
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

void NodeDB::InsertDummy(Height h, const Key::ID& kid)
{
	Recordset rs(*this, Query::DummyIns, "INSERT INTO " TblDummy "(" TblDummy_ID "," TblDummy_SpendHeight ") VALUES(?,?)");
	Key::ID::Packed p;
	rs.put(0, kid, p);
	rs.put(1, h);
	rs.Step();
	TestChanged1Row();
}

Height NodeDB::GetLowestDummy(Key::ID& kid)
{
	Recordset rs(*this, Query::DummyFindLowest, "SELECT " TblDummy_ID "," TblDummy_SpendHeight " FROM " TblDummy " ORDER BY " TblDummy_SpendHeight " ASC LIMIT 1");
	if (!rs.Step())
		return MaxHeight;

	Height h;
	rs.get(0, kid);
	rs.get(1, h);

	return h;
}

Height NodeDB::GetDummyHeight(const Key::ID& kid)
{
	Recordset rs(*this, Query::DummyFind, "SELECT " TblDummy_SpendHeight " FROM " TblDummy " WHERE " TblDummy_ID "=?");
	Key::ID::Packed p;
	rs.put(0, kid, p);
	if (!rs.Step())
		return MaxHeight;

	Height h;
	rs.get(0, h);
	return h;
}

void NodeDB::DeleteDummy(const Key::ID& kid)
{
	Recordset rs(*this, Query::DummyDel, "DELETE FROM " TblDummy " WHERE " TblDummy_ID "=?");
	Key::ID::Packed p;
	rs.put(0, kid, p);
	rs.Step();
	TestChanged1Row();
}

void NodeDB::SetDummyHeight(const Key::ID& kid, Height h)
{
	Recordset rs(*this, Query::DummyUpdHeight, "UPDATE " TblDummy " SET " TblDummy_SpendHeight "=? WHERE " TblDummy_ID "=?");
	rs.put(0, h);
	Key::ID::Packed p;
	rs.put(1, kid, p);
	rs.Step();
	TestChanged1Row();
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

Height NodeDB::FindBlock(const Blob& hash)
{
    Recordset rs(*this, Query::BlockFind, "SELECT " TblStates_Height " FROM " TblStates" WHERE " TblStates_Hash "=? ORDER BY " TblStates_Height " DESC LIMIT 1");
    rs.put(0, hash);
    if (!rs.Step())
        return Rules::HeightGenesis - 1;

    Height h;
    rs.get(0, h);

    assert(h >= Rules::HeightGenesis);
    return h;
}

void NodeDB::TxoAdd(TxoID id, const Blob& b)
{
	Recordset rs(*this, Query::TxoAdd, "INSERT INTO " TblTxo "(" TblTxo_ID "," TblTxo_Value ") VALUES(?,?)");
	rs.put(0, id);
	rs.put(1, b);
	rs.Step();
}

void NodeDB::TxoDel(TxoID id)
{
	Recordset rs(*this, Query::TxoDel, "DELETE FROM " TblTxo " WHERE " TblTxo_ID "=?");
	rs.put(0, id);
	rs.Step();
	TestChanged1Row();
}

void NodeDB::TxoDelFrom(TxoID id)
{
	Recordset rs(*this, Query::TxoDelFrom, "DELETE FROM " TblTxo " WHERE " TblTxo_ID ">=?");
	rs.put(0, id);
	rs.Step();
}

void NodeDB::TxoSetSpent(TxoID id, Height h)
{
	Recordset rs(*this, Query::TxoSetSpent, "UPDATE " TblTxo " SET " TblTxo_SpendHeight "=? WHERE " TblTxo_ID "=?");
	if (MaxHeight != h)
		rs.put(0, h);
	rs.put(1, id);

	rs.Step();
	TestChanged1Row();
}

void NodeDB::EnumTxos(WalkerTxo& wlk, TxoID id0)
{
	wlk.m_Rs.Reset(*this, Query::TxoEnum, "SELECT " TblTxo_ID "," TblTxo_Value "," TblTxo_SpendHeight " FROM " TblTxo " WHERE " TblTxo_ID ">=? ORDER BY " TblTxo_ID);
	wlk.m_Rs.put(0, id0);
}

bool NodeDB::WalkerTxo::MoveNext()
{
	if (!m_Rs.Step())
		return false;

	m_Rs.get(0, m_ID);
	m_Rs.get(1, m_Value);

	if (m_Rs.IsNull(2))
		m_SpendHeight = MaxHeight;
	else
		m_Rs.get(2, m_SpendHeight);

	return true;
}

void NodeDB::TxoSetValue(TxoID id, const Blob& v)
{
	Recordset rs(*this, Query::TxoSetValue, "UPDATE " TblTxo " SET " TblTxo_Value "=? WHERE " TblTxo_ID "=?");
	rs.put(0, v);
	rs.put(1, id);
	rs.Step();
	TestChanged1Row();
}

void NodeDB::TxoGetValue(WalkerTxo& wlk, TxoID id0)
{
	wlk.m_Rs.Reset(*this, Query::TxoGetValue, "SELECT " TblTxo_Value " FROM " TblTxo " WHERE " TblTxo_ID "=?");
	wlk.m_Rs.put(0, id0);

	wlk.m_Rs.StepStrict();
	wlk.m_Rs.get(0, wlk.m_Value);
}

NodeDB::StreamMmr::StreamMmr(NodeDB& db, StreamType::Enum eType, bool bStoreH0)
	:m_hStoreFrom(!bStoreH0)
	,m_eType(eType)
	,m_DB(db)
{
	for (size_t i = 0; i < _countof(m_pCache); i++)
		m_pCache[i].m_X = static_cast<uint64_t>(-1);

	m_LastOut.m_Pos.H = Merkle::Position::HMax;
	m_LastOut.m_Pos.X = static_cast<uint64_t>(-1);
}

void NodeDB::StreamMmr::Append(const Merkle::Hash& hv)
{
	uint64_t n = m_Count;
	ResizeTo(n + 1);
	Mmr::Replace(n, hv);
}

void NodeDB::StreamMmr::ShrinkTo(uint64_t nCount)
{
	assert(m_Count >= nCount);
	ResizeTo(nCount);
}

void NodeDB::StreamMmr::ResizeTo(uint64_t nCount)
{
	m_DB.StreamResize(m_eType, get_TotalHashes(nCount, m_hStoreFrom) * sizeof(Merkle::Hash), get_TotalHashes(m_Count, m_hStoreFrom) * sizeof(Merkle::Hash));
	m_Count = nCount;
}

void NodeDB::StreamMmr::LoadElement(Merkle::Hash& hv, const Merkle::Position& pos) const
{
	if (CacheFind(hv, pos))
		return;

	m_DB.StreamIO(m_eType, Pos2Idx(pos, m_hStoreFrom) * sizeof(Merkle::Hash), hv.m_pData, hv.nBytes, false);
	Cast::NotConst(this)->CacheAdd(hv, pos);
}

void NodeDB::StreamMmr::SaveElement(const Merkle::Hash& hv, const Merkle::Position& pos)
{
	m_DB.StreamIO(m_eType, Pos2Idx(pos, m_hStoreFrom) * sizeof(Merkle::Hash), Cast::NotConst(hv.m_pData), hv.nBytes, true);
	CacheAdd(hv, pos);
}

bool NodeDB::StreamMmr::CacheFind(Merkle::Hash& hv, const Merkle::Position& pos) const
{
	// Note: ALWAYS test the main cache BEFORE m_LastOut, coz that element could already be overwritten
	if (pos.H < _countof(m_pCache)) // 'if' is needed only if we decide to reduce the cache size
	{
		const CacheEntry& ce = m_pCache[pos.H];
		if (ce.m_X == pos.X)
		{
			hv = ce.m_Value;
			return true;
		}
	}

	if ((m_LastOut.m_Pos.H == pos.H) && (m_LastOut.m_Pos.X == pos.X))
	{
		hv = m_LastOut.m_Value;
		return true;
	}

	return false;
}

void NodeDB::StreamMmr::CacheAdd(const Merkle::Hash& hv, const Merkle::Position& pos)
{
	if (pos.H < _countof(m_pCache)) // 'if' is needed only if we decide to reduce the cache size
	{
		CacheEntry& ce = m_pCache[pos.H];

		if ((ce.m_X != pos.X) && (ce.m_X != static_cast<uint64_t>(-1)))
		{
			m_LastOut.m_Pos.X = ce.m_X;
			m_LastOut.m_Pos.H = pos.H;
			m_LastOut.m_Value = ce.m_Value;
		}

		ce.m_Value = hv;
		ce.m_X = pos.X;
	}
}

NodeDB::StatesMmr::StatesMmr(NodeDB& db)
	:StreamMmr(db, StreamType::StatesMmr, false)
{
}

uint64_t NodeDB::StatesMmr::H2I(Height h)
{
	return (h <= Rules::HeightGenesis) ? 0 : (h - Rules::HeightGenesis);
}

void NodeDB::StatesMmr::LoadElement(Merkle::Hash& hv, const Merkle::Position& pos) const
{
	if (pos.H)
		StreamMmr::LoadElement(hv, pos);
	else
	{
		if (CacheFind(hv, pos))
			return;

		LoadStateHash(hv, pos.X + Rules::HeightGenesis);
		Cast::NotConst(this)->CacheAdd(hv, pos);
	}
}

void NodeDB::StatesMmr::LoadStateHash(Merkle::Hash& hv, Height h) const
{
	uint64_t row = m_DB.FindActiveStateStrict(h);
	m_DB.get_StateHash(row, hv);
}

void NodeDB::StatesMmr::SaveElement(const Merkle::Hash& hv, const Merkle::Position& pos)
{
	if (pos.H)
		StreamMmr::SaveElement(hv, pos);
	else
		CacheAdd(hv, pos);
}

const uint32_t NodeDB::s_StreamBlob = 1024*1024; // arbitrary, but should not be changed after DB is created

uint64_t NodeDB::StreamType::Key(uint64_t idx, Enum eType)
{
	return idx | (static_cast<uint64_t>(eType) << 32);
}


void NodeDB::StreamResize(StreamType::Enum eType, uint64_t n, uint64_t n0)
{
	uint64_t nBlobs0 = (n0 + s_StreamBlob - 1) / s_StreamBlob;
	uint64_t nBlobs1 = (n + s_StreamBlob - 1) / s_StreamBlob;

	for (; nBlobs0 < nBlobs1; nBlobs0++)
	{
		Recordset rs(*this, Query::StreamIns, "INSERT INTO " TblStreams "(" TblStream_ID "," TblStream_Value ") VALUES (?,?)");
		rs.put(0, StreamType::Key(nBlobs0, eType));
		rs.putZeroBlob(1, s_StreamBlob);
		rs.Step();
		TestChanged1Row();
	}

	if (nBlobs0 > nBlobs1)
	{
		StreamShrinkInternal(StreamType::Key(nBlobs1, eType), StreamType::Key(nBlobs0, eType));

		uint64_t ret = get_RowsChanged();
		if (ret != nBlobs0 - nBlobs1)
			ThrowInconsistent();
	}
}

void NodeDB::StreamShrinkInternal(uint64_t k0, uint64_t k1)
{
	Recordset rs(*this, Query::StreamDel, "DELETE FROM " TblStreams " WHERE " TblStream_ID ">=? AND " TblStream_ID "<?");
	rs.put(0, k0);
	rs.put(1, k1);
	rs.Step();
}

void NodeDB::StreamsDelAll(StreamType::Enum t0, StreamType::Enum t1)
{
	StreamShrinkInternal(StreamType::Key(0, t0), StreamType::Key(std::numeric_limits<uint32_t>::max(), t1));
}

struct NodeDB::BlobGuard
{
	sqlite3_blob* m_pPtr = nullptr;

	~BlobGuard()
	{
		if (m_pPtr)
			BEAM_VERIFY(SQLITE_OK == sqlite3_blob_close(m_pPtr));
	}
};

void NodeDB::OpenBlob(BlobGuard& blob, const char* szTable, const char* szColumn, uint64_t rowid, bool bRW)
{
	TestRet(sqlite3_blob_open(m_pDb, "main", szTable, szColumn, rowid, bRW ? 1 : 0, &blob.m_pPtr));
}

void NodeDB::StreamIO(StreamType::Enum eType, uint64_t pos, uint8_t* p, uint64_t nCount, bool bWrite)
{
	uint64_t nBlob0 = pos / s_StreamBlob;
	uint32_t nOffs = static_cast<uint32_t>(pos % s_StreamBlob);

	while (nCount)
	{
		BlobGuard blob;
		OpenBlob(blob, TblStreams, TblStream_Value, StreamType::Key(nBlob0, eType), bWrite);

		uint32_t nPortion = s_StreamBlob - nOffs;
		if (nPortion > nCount)
			nPortion = static_cast<uint32_t>(nCount);

		int nRes = bWrite ?
			sqlite3_blob_write(blob.m_pPtr, p, nPortion, nOffs) :
			sqlite3_blob_read(blob.m_pPtr, p, nPortion, nOffs);

		TestRet(nRes);

		nCount -= nPortion;
		p += nPortion;
		nOffs = 0;
		nBlob0++;
	}
}

void NodeDB::ShieldedOutpSet(Height h, uint64_t count)
{
	Recordset rs(*this, Query::ShieldedStatisticIns, "INSERT INTO " TblShieldedStatistic " (" TblShieldedStatistic_Height "," TblShieldedStatistic_OutCount ") VALUES(?,?)");
	rs.put(0, h);
	rs.put(1, count);
	rs.Step();
}

uint64_t NodeDB::ShieldedOutpGet(Height h)
{
	Recordset rs(*this, Query::ShieldedStatisticSel, "SELECT " TblShieldedStatistic_OutCount " FROM " TblShieldedStatistic " WHERE " TblShieldedStatistic_Height "<=? ORDER BY " TblShieldedStatistic_Height " DESC LIMIT 1");
	rs.put(0, h);

	if (!rs.Step())
		return 0;

	uint64_t ret;
	rs.get(0, ret);
	return ret;
}

void NodeDB::ShieldedOutpDelFrom(Height h)
{
	Recordset rs(*this, Query::ShieldedStatisticDel, "DELETE FROM " TblShieldedStatistic " WHERE " TblShieldedStatistic_Height ">=?");
	rs.put(0, h);
	rs.Step();
}

bool NodeDB::UniqueInsertSafe(const Blob& key, const Blob* pVal)
{
	Recordset rs(*this, Query::UniqueIns, "INSERT INTO " TblUnique " (" TblUnique_Key "," TblUnique_Value ") VALUES(?,?)");
	rs.put(0, key);
	if (pVal)
		rs.put(1, *pVal);

	return rs.StepModifySafe();
}

bool NodeDB::UniqueFind(const Blob& key, Recordset& rs)
{
	rs.Reset(*this, Query::UniqueFind, "SELECT " TblUnique_Value " FROM " TblUnique " WHERE " TblUnique_Key "=?");
	rs.put(0, key);
	return rs.Step();
}

void NodeDB::UniqueDeleteStrict(const Blob& key)
{
	Recordset rs(*this, Query::UniqueDel, "DELETE FROM " TblUnique " WHERE " TblUnique_Key "=?");
	rs.put(0, key);

	rs.Step();
	TestChanged1Row();
}

void NodeDB::UniqueDeleteAll()
{
	Recordset rs(*this, Query::UniqueDelAll, "DELETE FROM " TblUnique);
	rs.Step();
}

void NodeDB::get_CacheState(CacheState& cs)
{
	Blob blob(&cs, sizeof(cs));
	if (ParamGet(ParamID::CacheState, nullptr, &blob))
	{
		cs.m_HitCounter = ByteOrder::from_le(cs.m_HitCounter);
		cs.m_SizeMax = ByteOrder::from_le(cs.m_SizeMax);
		cs.m_SizeCurrent = ByteOrder::from_le(cs.m_SizeCurrent);
	}
	else
	{
		ZeroObject(cs);
		cs.m_SizeMax = 512U * 1024U * 1024U; // default cache size is 512MB
	}
}

void NodeDB::set_CacheState(CacheState& cs)
{
	if (cs.m_SizeCurrent > cs.m_SizeMax)
	{
		for (Recordset rs(*this, Query::CacheEnumByHit, "SELECT rowid,LENGTH(" TblCache_Data ") FROM " TblCache " ORDER BY " TblCache_LastHit); rs.Step(); )
		{
			uint64_t rowid, nSize;
			rs.get(0, rowid);
			rs.get(1, nSize);

			if (nSize > cs.m_SizeCurrent)
				ThrowInconsistent();

			Recordset rs2(*this, Query::CacheDel, "DELETE FROM " TblCache " WHERE rowid=?");
			rs2.put(0, rowid);
			rs2.Step();
			TestChanged1Row();

			cs.m_SizeCurrent -= nSize;
			if (cs.m_SizeCurrent <= cs.m_SizeMax)
				break;
		}

	}

	cs.m_HitCounter = ByteOrder::to_le(cs.m_HitCounter);
	cs.m_SizeMax = ByteOrder::to_le(cs.m_SizeMax);
	cs.m_SizeCurrent = ByteOrder::to_le(cs.m_SizeCurrent);

	Blob blob(&cs, sizeof(cs));
	ParamSet(ParamID::CacheState, nullptr, &blob);
}

void NodeDB::CacheSetMaxSize(uint64_t nSize)
{
	CacheState cs;
	get_CacheState(cs);

	cs.m_SizeMax = nSize;
	set_CacheState(cs);
}


void NodeDB::CacheInsert(const Blob& key, const Blob& data)
{
	CacheState cs;
	get_CacheState(cs);
	if (data.n > cs.m_SizeMax)
		return;

	Recordset rs(*this, Query::CacheIns, "INSERT INTO " TblCache " (" TblCache_Key "," TblCache_Data "," TblCache_LastHit ") VALUES(?,?,?)");
	rs.put(0, key);
	rs.put(1, data);
	rs.put(2, ++cs.m_HitCounter);

	rs.Step();
	TestChanged1Row();

	cs.m_SizeCurrent += data.n;
	set_CacheState(cs);
}

bool NodeDB::CacheFind(const Blob& key, ByteBuffer& res)
{
	Recordset rs(*this, Query::CacheFind, "SELECT rowid FROM " TblCache " WHERE " TblCache_Key "=?");
	rs.put(0, key);

	if (!rs.Step())
		return false;

	uint64_t rowid;
	rs.get(0, rowid);

	CacheState cs;
	get_CacheState(cs);

	rs.Reset(*this, Query::CacheUpdateHit, "UPDATE " TblCache " SET " TblCache_LastHit "=? WHERE rowid=?");
	rs.put(0, ++cs.m_HitCounter);
	rs.put(1, rowid);

	rs.Step();
	TestChanged1Row();

	BlobGuard blob;
	OpenBlob(blob, TblCache, TblCache_Data, rowid, false);

	uint32_t nSize = sqlite3_blob_bytes(blob.m_pPtr);
	res.resize(nSize);

	if (nSize)
		TestRet(sqlite3_blob_read(blob.m_pPtr, &res.front(), nSize, 0));

	set_CacheState(cs);
	return true;
}


const Asset::ID NodeDB::s_AssetEmpty0 = Asset::s_MaxCount;

Asset::ID NodeDB::AssetFindByOwner(const PeerID& owner)
{
	Recordset rs(*this, Query::AssetFindOwner, "SELECT " TblAssets_ID " FROM " TblAssets " WHERE " TblAssets_Owner "=?");
	rs.put_As(0, owner);
	if (!rs.Step())
		return false;

	Asset::ID ret;
	rs.get(0, ret);
	return ret;
}

void NodeDB::AssetDeleteRaw(Asset::ID id)
{
	Recordset rs(*this, Query::AssetDel, "DELETE FROM " TblAssets " WHERE " TblAssets_ID "=?");
	rs.put(0, id);
	rs.Step();
	TestChanged1Row();
}

void NodeDB::AssetInsertRaw(Asset::ID id, const Asset::Full* pAi)
{
	Recordset rs(*this, Query::AssetAdd, "INSERT INTO " TblAssets "(" TblAssets_ID "," TblAssets_Owner "," TblAssets_Data "," TblAssets_Value "," TblAssets_LockHeight ") VALUES(?,?,?,?,?)");
	rs.put(0, id);

	if (pAi)
	{
		rs.put(1, pAi->m_Owner);
		rs.put(2, Blob(pAi->m_Metadata.m_Value));
		rs.put_As(3, pAi->m_Value);
		rs.put(4, pAi->m_LockHeight);
	}

	rs.Step();
	TestChanged1Row();
}

Asset::ID NodeDB::AssetFindMinFree(Asset::ID nMin)
{
	// find free index
	Recordset rs(*this, Query::AssetFindMin, "SELECT " TblAssets_ID " FROM " TblAssets " WHERE " TblAssets_ID ">=? ORDER BY " TblAssets_ID " ASC LIMIT 1");
	rs.put(0, nMin);

	if (!rs.Step())
		return 0;

	Asset::ID ret;
	rs.get(0, ret);
	return ret;
}

void NodeDB::AssetAdd(Asset::Full& ai)
{
	Asset::ID aidMin = ai.m_ID;
	// find free index
	ai.m_ID = AssetFindMinFree(ai.m_ID + s_AssetEmpty0);
	if (ai.m_ID)
	{
		assert(ai.m_ID > s_AssetEmpty0);
		assert(ai.m_ID >= aidMin);

		AssetDeleteRaw(ai.m_ID);
		ai.m_ID -= s_AssetEmpty0;
	}
	else
	{
		ai.m_ID = static_cast<Asset::ID>(ParamIntGetDef(ParamID::AssetsCount) + 1);

		for ( ; ai.m_ID < aidMin; ai.m_ID++)
			AssetInsertRaw(ai.m_ID + s_AssetEmpty0, nullptr);

		ParamIntSet(ParamID::AssetsCount, ai.m_ID);
	}

	AssetInsertRaw(ai.m_ID, &ai);

	ParamIntSet(ParamID::AssetsCountUsed, ParamIntGetDef(ParamID::AssetsCountUsed) + 1);
}

Asset::ID NodeDB::AssetDelete(Asset::ID id)
{
	AssetDeleteRaw(id);

	Asset::ID nCount = static_cast<Asset::ID>(ParamIntGetDef(ParamID::AssetsCount));
	if (nCount == id)
	{
		// last erased.
		while (--nCount)
		{
			id = nCount + s_AssetEmpty0;
			if (!AssetFindMinFree(id))
				break;

			AssetDeleteRaw(id);
		}

		ParamIntSet(ParamID::AssetsCount, nCount);
	}
	else
		AssetInsertRaw(id + s_AssetEmpty0, nullptr);

	ParamIntSet(ParamID::AssetsCountUsed, ParamIntGetDef(ParamID::AssetsCountUsed) - 1);

	return nCount;
}

void NodeDB::AssetsDelAll()
{
	Recordset rs(*this, Query::AssetsDelAll, "DELETE FROM " TblAssets);
	rs.Step();

	ParamDelSafe(ParamID::AssetsCountUsed);
	ParamDelSafe(ParamID::AssetsCount);
}

bool NodeDB::AssetGetSafe(Asset::Full& ai)
{
	Recordset rs(*this, Query::AssetGet, "SELECT " TblAssets_Value "," TblAssets_Owner "," TblAssets_Data "," TblAssets_LockHeight " FROM " TblAssets " WHERE " TblAssets_ID "=?");
	rs.put(0, ai.m_ID);
	if (!rs.Step())
		return false;

	rs.get_As(0, ai.m_Value);
	rs.get_As(1, ai.m_Owner);
	rs.get(2, ai.m_Metadata.m_Value);
	rs.get(3, ai.m_LockHeight);

	ai.m_Metadata.UpdateHash();

	return true;
}

bool NodeDB::AssetGetNext(Asset::Full& ai)
{
	assert(ai.m_ID < Asset::s_MaxCount);

	ai.m_ID = AssetFindMinFree(ai.m_ID + 1);
	if (ai.m_ID > Asset::s_MaxCount)
		return false;

	return AssetGetSafe(ai);
}

void NodeDB::AssetSetValue(Asset::ID id, const AmountBig::Type& val, Height hLockHeight)
{
	Recordset rs(*this, Query::AssetSetVal, "UPDATE " TblAssets " SET " TblAssets_Value "=?," TblAssets_LockHeight "=? WHERE " TblAssets_ID "=?");
	rs.put_As(0, val);
	rs.put(1, hLockHeight);
	rs.put(2, id);
	rs.Step();
	TestChanged1Row();
}

void NodeDB::MigrateFrom18()
{
	{
		LOG_INFO() << "Resetting peer ratings...";

		std::vector<WalkerPeer::Data> v;

		{
			WalkerPeer wlk;
			for (EnumPeers(wlk); wlk.MoveNext(); )
				v.push_back(wlk.m_Data);
		}

		PeersDel();

		for (size_t i = 0; i < v.size(); i++)
		{
			WalkerPeer::Data& d = v[i];
			d.m_Rating = PeerManager::Rating::Initial;
			PeerIns(d);
		}
	}

	LOG_INFO() << "Migrating inputs...";

	ExecQuick("ALTER TABLE " TblStates " ADD COLUMN "  "[" TblStates_Inputs	"] BLOB");

	std::vector<StateInput> vInps;
	Height h = 0;

	WalkerTxo wlk;
	wlk.m_Rs.Reset(*this, Query::TxoEnumBySpentMigrate, "SELECT " TblTxo_ID "," TblTxo_Value "," TblTxo_SpendHeight " FROM " TblTxo " WHERE " TblTxo_SpendHeight " IS NOT NULL ORDER BY " TblTxo_SpendHeight "," TblTxo_ID);
	while (true)
	{
		bool bNext = wlk.MoveNext();

		bool bFlush = !vInps.empty() && (!bNext || (wlk.m_SpendHeight != h));
		if (bFlush)
		{
			std::sort(vInps.begin(), vInps.end(), StateInput::IsLess);

			uint64_t rowid = FindActiveStateStrict(h);
			set_StateInputs(rowid, &vInps.front(), vInps.size());
			vInps.clear();
		}

		if (!bNext)
			break;

		h = wlk.m_SpendHeight;

		// extract input from output (which may be naked already)
		if (wlk.m_Value.n < sizeof(ECC::Point))
			ThrowInconsistent();
		const uint8_t* pSrc = reinterpret_cast<const uint8_t*>(wlk.m_Value.p);

		StateInput& x = vInps.emplace_back();

		x.m_Txo_AndY = wlk.m_ID;
		memcpy(x.m_CommX.m_pData, pSrc + 1, x.m_CommX.nBytes);
		if (1 & pSrc[0])
			x.m_Txo_AndY |= StateInput::s_Y;
	}

	ExecQuick("DROP INDEX [Idx" TblTxo "SH]");
}

void NodeDB::MigrateFrom20()
{
	LOG_INFO() << "Rebuilding states MMR...";

	ExecQuick("UPDATE " TblStates " SET " TblStates_Rollback "=NULL"); // was used for states MMR. Prepare it for the new use

	StateID sid;
	get_Cursor(sid);

	StatesMmr smmr(*this);
	for (Height h = Rules::HeightGenesis; h < sid.m_Height; h++)
	{
		Merkle::Hash hv;
		smmr.LoadStateHash(hv, h); // there's a more effective way to select hashes of all active states. But it's just a migration.
		smmr.Append(hv);

	}
}

bool NodeDB::WalkerAssetEvt::MoveNext()
{
	if (!m_Rs.Step())
		return false;

	m_Rs.get(0, m_ID);
	m_Rs.get(1, m_Height);
	m_Rs.get(2, m_Index);
	m_Rs.get(3, m_Body);

	return true;
}

void NodeDB::AssetEvtsInsert(const AssetEvt& x)
{
	Recordset rs(*this, Query::AssetEvtsInsert, "INSERT INTO " TblAssetEvts " (" TblAssetEvts_ID "," TblAssetEvts_Height "," TblAssetEvts_Index "," TblAssetEvts_Data ") VALUES(?,?,?,?)");
	rs.put(0, x.m_ID);
	rs.put(1, x.m_Height);
	rs.put(2, x.m_Index);
	rs.put(3, x.m_Body);
	rs.Step();
}

void NodeDB::AssetEvtsEnumBwd(WalkerAssetEvt& wlk, Asset::ID id, Height h)
{
	wlk.m_Rs.Reset(*this, Query::AssetEvtsEnumBwd, "SELECT * FROM " TblAssetEvts " WHERE " TblAssetEvts_ID "=? AND " TblAssetEvts_Height "<=? ORDER BY " TblAssetEvts_Height " DESC," TblAssetEvts_Index " DESC");
	wlk.m_Rs.put(0, id);
	wlk.m_Rs.put(1, h);
}

void NodeDB::AssetEvtsGetStrict(WalkerAssetEvt& wlk, Height h, uint64_t nIdx)
{
	wlk.m_Rs.Reset(*this, Query::AssetEvtsGet, "SELECT * FROM " TblAssetEvts " WHERE " TblAssetEvts_Height "=? AND " TblAssetEvts_Index "=?");
	wlk.m_Rs.put(0, h);
	wlk.m_Rs.put(1, nIdx);
	if (!wlk.MoveNext())
		ThrowInconsistent();
}

void NodeDB::AssetEvtsDeleteFrom(Height h)
{
	Recordset rs(*this, Query::AssetEvtsDeleteFrom, "DELETE FROM " TblAssetEvts " WHERE " TblAssetEvts_Height ">=?");
	rs.put(0, h);
	rs.Step();
}

bool NodeDB::ContractDataFind(const Blob& key, Blob& data, Recordset& rs)
{
	rs.Reset(*this, Query::ContractDataFind, "SELECT " TblContracts_Value " FROM " TblContracts " WHERE " TblContracts_Key "=?");
	rs.put(0, key);
	if (!rs.Step())
		return false;

	rs.get(0, data);
	return true;
}

bool NodeDB::ContractDataFindNext(Blob& key, Recordset& rs)
{
	rs.Reset(*this, Query::ContractDataFindNext, "SELECT " TblContracts_Key " FROM " TblContracts " WHERE " TblContracts_Key ">?");
	rs.put(0, key);
	if (!rs.Step())
		return false;

	rs.get(0, key);
	return true;
}

void NodeDB::ContractDataInsert(const Blob& key, const Blob& data)
{
	Recordset rs(*this, Query::ContractDataInsert, "INSERT INTO " TblContracts " (" TblContracts_Key "," TblContracts_Value ") VALUES(?,?)");
	rs.put(0, key);
	rs.put(1, data);

	rs.Step();
	TestChanged1Row();
}

void NodeDB::ContractDataUpdate(const Blob& key, const Blob& data)
{
	Recordset rs(*this, Query::ContractDataUpdate, "UPDATE " TblContracts " SET " TblContracts_Value "=? WHERE " TblContracts_Key "=?");
	rs.put(0, data);
	rs.put(1, key);

	rs.Step();
	TestChanged1Row();
}

void NodeDB::ContractDataDel(const Blob& key)
{
	Recordset rs(*this, Query::ContractDataDel, "DELETE FROM " TblContracts " WHERE " TblContracts_Key "=?");
	rs.put(0, key);

	rs.Step();
	TestChanged1Row();
}

void NodeDB::ContractDataDelAll()
{
	Recordset rs(*this, Query::ContractDataDelAll, "DELETE FROM " TblContracts);
	rs.Step();
}

void NodeDB::ContractDataEnum(WalkerContractData& wlk, const Blob& keyMin, const Blob& keyMax)
{
	wlk.m_Rs.Reset(*this, Query::ContractDataEnum, "SELECT " TblContracts_Key "," TblContracts_Value " FROM " TblContracts " WHERE " TblContracts_Key ">=? AND " TblContracts_Key "<=? ORDER BY " TblContracts_Key);
	wlk.m_Rs.put(0, keyMin);
	wlk.m_Rs.put(1, keyMax);
}

void NodeDB::ContractDataEnum(WalkerContractData& wlk)
{
	wlk.m_Rs.Reset(*this, Query::ContractDataEnumAll, "SELECT " TblContracts_Key "," TblContracts_Value " FROM " TblContracts " ORDER BY " TblContracts_Key);
}

bool NodeDB::WalkerContractData::MoveNext()
{
	if (!m_Rs.Step())
		return false;

	m_Rs.get(0, m_Key);
	m_Rs.get(1, m_Val);
	return true;
}

void put_ContractLogPos(NodeDB::Recordset& rs, int iCol, const HeightPos& pos, NodeDB::ContractLog::PosPacked& buf)
{
	buf.m_Height = pos.m_Height;
	buf.m_Idx = pos.m_Pos;

	rs.put(iCol, Blob(&buf, sizeof(buf)));
}

void NodeDB::ContractLogInsert(const ContractLog::Entry& x)
{
	Recordset rs(*this, Query::ContractLogInsert, "INSERT INTO " TblContractLogs " (" TblContractLogs_Pos "," TblContractLogs_Key "," TblContractLogs_Data ") VALUES(?,?,?)");

	ContractLog::PosPacked buf;
	put_ContractLogPos(rs, 0, x.m_Pos, buf);

	rs.put(1, x.m_Key);
	rs.put(2, x.m_Val);

	rs.Step();
	TestChanged1Row();
}

void NodeDB::ContractLogDel(const HeightPos& posMin, const HeightPos& posMax)
{
	Recordset rs(*this, Query::ContractLogDel, "DELETE FROM " TblContractLogs " WHERE " TblContractLogs_Pos " BETWEEN ? AND ?");

	NodeDB::ContractLog::PosPacked bufMin, bufMax;
	put_ContractLogPos(rs, 0, posMin, bufMin);
	put_ContractLogPos(rs, 1, posMax, bufMax);

	rs.Step();
}

void NodeDB::ContractLogEnum(ContractLog::Walker& wlk, const HeightPos& posMin, const HeightPos& posMax)
{
	wlk.m_Rs.Reset(*this, Query::ContractLogEnum, "SELECT * FROM " TblContractLogs " WHERE " TblContractLogs_Pos " BETWEEN ? AND ? ORDER BY " TblContractLogs_Pos);

	put_ContractLogPos(wlk.m_Rs, 0, posMin, wlk.m_bufMin);
	put_ContractLogPos(wlk.m_Rs, 1, posMax, wlk.m_bufMax);
}

void NodeDB::ContractLogEnum(ContractLog::Walker& wlk, const Blob& keyMin, const Blob& keyMax, const HeightPos& posMin, const HeightPos& posMax)
{
	wlk.m_Rs.Reset(*this, Query::ContractLogEnumCid, "SELECT * FROM " TblContractLogs
		" WHERE (" TblContractLogs_Key " BETWEEN ? AND ?) AND (" TblContractLogs_Pos " BETWEEN ? AND ?) ORDER BY " TblContractLogs_Key "," TblContractLogs_Pos);
	wlk.m_Rs.put(0, keyMin);
	wlk.m_Rs.put(1, keyMax);

	put_ContractLogPos(wlk.m_Rs, 2, posMin, wlk.m_bufMin);
	put_ContractLogPos(wlk.m_Rs, 3, posMax, wlk.m_bufMax);
}

bool NodeDB::ContractLog::Walker::MoveNext()
{
	if (!m_Rs.Step())
		return false;

	const auto& pos = m_Rs.get_As<PosPacked>(0);
	pos.m_Height.Export(m_Entry.m_Pos.m_Height);
	pos.m_Idx.Export(m_Entry.m_Pos.m_Pos);

	m_Rs.get(1, m_Entry.m_Key);
	m_Rs.get(2, m_Entry.m_Val);
	return true;
}

} // namespace beam
