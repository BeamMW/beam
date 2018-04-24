#pragma once

#include "../core/common.h"
#include "../core/storage.h"
#include "../sqlite/sqlite3.h"

namespace beam {


#define NOP(...)
#define NOP0
#define M0_Comma ,
#define M_1(a, ...) a
#define M_1_Comma(a, ...) a,
#define M0_Comma_Str ","


#define NodeDb_Table_States(macro, sep) \
	macro(Height,		"INTEGER NOT NULL") sep \
	macro(Hash,			"BLOB NOT NULL") sep \
	macro(HashPrev,		"BLOB NOT NULL") sep \
	macro(Difficulty,	"INTEGER NOT NULL") sep \
	macro(Timestamp,	"INTEGER NOT NULL") sep \
	macro(HashUtxos,	"BLOB NOT NULL") sep \
	macro(HashKernels,	"BLOB NOT NULL") sep \
	macro(StateFlags,	"INTEGER NOT NULL") sep \
	macro(RowPrev,		"INTEGER") sep \
	macro(CountNext,	"INTEGER NOT NULL") sep \
	macro(PoW,			"BLOB") sep \
	macro(BlindOffset,	"BLOB") sep \
	macro(Mmr,			"BLOB") sep \
	macro(Body,			"BLOB")


#define NodeDb_Table_Params(macro, sep) \
	macro(ParamInt,		"INTEGER") sep \
	macro(ParamBlob,	"BLOB")

class NodeDB
{
public:

#define THE_MACRO_Ins(...) "?"
#define THE_MACRO_Upd(name, ...) #name "=?"

#define AllQueries(macro) \
	macro(Begin,	"BEGIN") \
	macro(Commit,	"COMMIT") \
	macro(Rollback,	"ROLLBACK") \
	macro(ParamGet,	"SELECT * FROM Params WHERE ID=?") \
	macro(ParamIns,	"INSERT INTO Params VALUES(?," NodeDb_Table_Params(THE_MACRO_Ins, M0_Comma_Str) ")") \
	macro(ParamUpd,	"UPDATE Params SET " NodeDb_Table_Params(THE_MACRO_Upd, M0_Comma_Str) " WHERE ID=?") \
	macro(StateIns,	"INSERT INTO States VALUES(" NodeDb_Table_States(THE_MACRO_Ins, M0_Comma_Str) ")") \

	struct Query
	{
		enum Enum
		{
			AllQueries(M_1_Comma)
			count
		};

		struct States
		{
			enum Enum {
				NodeDb_Table_States(M_1_Comma, NOP0)
				count
			};
		};

		struct Params
		{
			enum Enum {
				NodeDb_Table_Params(M_1_Comma, NOP0)
				count
			};

			struct ID {
				enum Enum {
					DbVer = 1,
				};
			};
		};
	};


	NodeDB();
	~NodeDB();

	void Close();
	void Open(const char* szPath, bool bCreate);

	void ParamIntSet(int ID, int val);
	bool ParamIntGet(int ID, int& val);

	int ParamIntGetDef(int ID, int def = 0);

	struct Blob {
		const void* p;
		uint32_t n;

		Blob() {}
		Blob(const void* p_, uint32_t n_) :p(p_) ,n(n_) {}
	};

	class Recordset
	{
		NodeDB& m_DB;
		sqlite3_stmt* m_pStmt;
	public:

		Recordset(NodeDB&);
		Recordset(NodeDB&, Query::Enum);
		~Recordset();

		void Reset();
		void Reset(Query::Enum);

		// Perform the transaction/fetch the next row
		bool FetchRow();

		// in/out
		void put(int col, int);
		void put(int col, int64_t);
		void put(int col, const Blob&);
		void put(int col, const Merkle::Hash&);
		void get(int col, int&);
		void get(int col, int64_t&);
		void get(int col, Blob&);
		const void* get_BlobStrict(int col, uint32_t n);

		template <typename T> void put_As(int col, const T& x) {
			put(col, Blob(&x, sizeof(x)));
		}

		template <typename T> const T& get_As(int col) {
			return *(const T*) get_BlobStrict(col, sizeof(T));
		}

		void putNull(int col);
		bool IsNull(int col);
	};

	int get_RowsChanged() const;
	int64_t get_LastInsertRowID() const;

	class Transaction {
		NodeDB* m_pDB;
	public:
		Transaction(NodeDB* = NULL);
		Transaction(NodeDB& db) :Transaction(&db) {}
		~Transaction(); // by default - rolls back

		void Start(NodeDB&);
		void Commit();
		void Rollback();
	};

private:

	sqlite3* m_pDb;
	sqlite3_stmt* m_pPrep[Query::count];
	static const char* const s_szSql[Query::count];

	void TestRet(int);
	void ThrowError(int);

	void Create();
	void ExecQuick(const char*);
	bool ExecStep(sqlite3_stmt*);
	bool ExecStep(Query::Enum); // returns true while there's a row

	sqlite3_stmt* get_Statement(Query::Enum);
};



} // namespace beam
