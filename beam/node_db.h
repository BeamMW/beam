#pragma once

#include "../core/common.h"
#include "../core/storage.h"
#include "../sqlite/sqlite3.h"

namespace beam {

class NodeDB
{
public:

	struct StateFlags {
		static const uint32_t Functional	= 0x1;	// has valid PoW and block body
		static const uint32_t Reachable		= 0x2;	// has only functional nodes up to the genesis state
		static const uint32_t Active		= 0x4;	// part of the current blockchain.
		static const uint32_t OverHorizon	= 0x8;	// block body is deleted, rollback is no more possible.
		static const uint32_t BlockPassed	= 0x10;	// block verification may be simplified (already verified)
	};

	struct ParamID {
		enum Enum {
			DbVer,
			CursorRow,
			CursorHeight,
		};
	};

	struct Query
	{
		enum Enum
		{
			Begin,
			Commit,
			Rollback,
			Scheme,
			ParamGet,
			ParamIns,
			ParamUpd,
			StateIns,
			StateDel,
			StateGet,
			StateGetHeightAndPrev,
			StateFind,
			StateFind2,
			StateUpdPrevRow,
			StateGetNextFCount,
			StateSetNextCount,
			StateSetNextCountF,
			StateGetHeightAndAux,
			StateGetNextFunctional,
			StateSetFlags,
			StateGetFlags0,
			StateGetFlags1,
			TipAdd,
			TipDel,
			TipReachableAdd,
			TipReachableDel,
			EnumTips,
			EnumFunctionalTips,
			StateGetPrev,
			Unactivate,
			Activate,
			MmrGet,
			MmrSet,
			SpendableAdd,
			SpendableDel,
			SpendableModify,
			SpendableEnum,
			StateGetBlock,
			StateSetBlock,
			StateDelBlock,
			StateSetRollback,

			Dbg0,
			Dbg1,
			Dbg2,
			Dbg3,
			Dbg4,

			count
		};
	};


	NodeDB();
	~NodeDB();

	void Close();
	void Open(const char* szPath);

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
		Recordset(NodeDB&, Query::Enum, const char*);
		~Recordset();

		void Reset();
		void Reset(Query::Enum, const char*);

		// Perform the query step. SELECT only: returns true while there're rows to read
		bool Step();

		// in/out
		void put(int col, uint32_t);
		void put(int col, uint64_t);
		void put(int col, const Blob&);
		void put(int col, const char*);
		void get(int col, uint32_t&);
		void get(int col, uint64_t&);
		void get(int col, Blob&);
		void get(int col, ByteBuffer&); // don't over-use

		const void* get_BlobStrict(int col, uint32_t n);

		template <typename T> void put_As(int col, const T& x) { put(col, Blob(&x, sizeof(x))); }
		template <typename T> void get_As(int col, T& out) { out = get_As<T>(col); }
		template <typename T> const T& get_As(int col) { return *(const T*) get_BlobStrict(col, sizeof(T)); }

		void putNull(int col);
		bool IsNull(int col);

		void put(int col, const Merkle::Hash& x) { put_As(col, x); }
		void get(int col, Merkle::Hash& x) { get_As(col, x); }
	};

	int get_RowsChanged() const;
	uint64_t get_LastInsertRowID() const;

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

	// Hi-level functions

	void ParamSet(uint32_t ID, const uint64_t*, const Blob*);
	bool ParamGet(uint32_t ID, uint64_t*, ByteBuffer*);

	uint64_t ParamIntGetDef(int ID, uint64_t def = 0);

	uint64_t InsertState(const Block::SystemState::Full&); // Fails if state already exists

	uint64_t StateFindSafe(const Block::SystemState::ID&);
	void get_State(uint64_t rowid, Block::SystemState::Full&);

	bool DeleteState(uint64_t rowid, uint64_t& rowPrev); // State must exist. Returns false if there are ancestors.

	uint32_t GetStateFlags(uint64_t rowid);
	void SetFlags(uint64_t rowid, uint32_t);

	void SetStateFunctional(uint64_t rowid);
	void SetStateNotFunctional(uint64_t rowid);

	typedef Merkle::Hash PeerID;

	void SetStateBlock(uint64_t rowid, const Blob& body, const PeerID& peer);
	void GetStateBlock(uint64_t rowid, ByteBuffer& body, ByteBuffer& rollback, PeerID& peer);
	void SetStateRollback(uint64_t rowid, const Blob& rollback);
	void DelStateBlock(uint64_t rowid);

	struct StateID {
		uint64_t m_Row;
		Height m_Height;
		//Merkle::Hash m_Hash; //?
	};


	struct WalkerState {
		Recordset m_Rs;
		StateID m_Sid;

		WalkerState(NodeDB& db) :m_Rs(db) {}
		bool MoveNext();
	};

	void EnumTips(WalkerState&); // lowest to highest
	void EnumFunctionalTips(WalkerState&); // highest to lowest
	bool get_Prev(StateID&);
	bool get_Prev(uint64_t&);

	bool get_Cursor(StateID& sid);

    void get_Proof(Merkle::Proof&, const StateID& sid, Height hPrev);
    void get_PredictedStatesHash(Merkle::Hash&, const StateID& sid); // For the next block.

	// the following functions move the curos, and mark the states with 'Active' flag
	void MoveBack(StateID&);
	void MoveFwd(const StateID&);

	// Utxos & kernels
	struct WalkerSpendable {
		Recordset m_Rs;
		Blob m_Key;
		uint32_t m_nUnspentCount;

		WalkerSpendable(NodeDB& db) :m_Rs(db) {}
		bool MoveNext();
	};

	void EnumUnpsent(WalkerSpendable&);

	void AddSpendable(const Blob& key, const Blob& body, uint32_t nRefs, uint32_t nUnspentCount);
	void ModifySpendable(const Blob& key, int32_t nRefsDelta, int32_t nUnspentDelta, bool bMaybeDelete); // will delete iff refs=0

	void assert_valid(); // diagnostic, for tests only

private:

	sqlite3* m_pDb;
	sqlite3_stmt* m_pPrep[Query::count];

	void TestRet(int);
	void ThrowError(int);

	void Create();
	void ExecQuick(const char*);
	bool ExecStep(sqlite3_stmt*);
	bool ExecStep(Query::Enum, const char*); // returns true while there's a row

	sqlite3_stmt* get_Statement(Query::Enum, const char*);


	void TipAdd(uint64_t rowid, Height);
	void TipDel(uint64_t rowid, Height);
	void TipReachableAdd(uint64_t rowid, Height);
	void TipReachableDel(uint64_t rowid, Height);
	void SetNextCount(uint64_t rowid, uint32_t);
	void SetNextCountFunctional(uint64_t rowid, uint32_t);
	void OnStateReachable(uint64_t rowid, uint64_t rowPrev, Height, bool);
	void BuildMmr(uint64_t rowid, uint64_t rowPrev, Height);
	void put_Cursor(const StateID& sid); // jump

	void TestChanged1Row();

	struct Dmmr;
};



} // namespace beam
