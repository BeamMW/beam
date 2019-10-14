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

#pragma once

#include "core/common.h"
#include "core/block_crypt.h"
#include "sqlite/sqlite3.h"

namespace beam {

class NodeDBUpgradeException : public std::runtime_error
{
public:
    NodeDBUpgradeException(const char* message)
        : std::runtime_error(message)
    {}
};

class NodeDB
{
public:

	struct StateFlags {
		static const uint32_t Functional	= 0x1;	// has block body
		static const uint32_t Reachable		= 0x2;	// has only functional nodes up to the genesis state
		static const uint32_t Active		= 0x4;	// part of the current blockchain
	};

	struct ParamID {
		enum Enum {
			DbVer,
			CursorRow,
			CursorHeight,
			FossilHeight, // Height starting from which and below original blocks are erased
			CfgChecksum,
			MyID,
			SyncTarget, // deprecated
			Deprecated_2,
			Treasury,
			DummyID, // hash of keys used to create UTXOs (owner key, dummy key)
			HeightTxoLo, // Height starting from which and below Txo info is totally erased.
			HeightTxoHi, // Height starting from which and below Txo infi is compacted, only the commitment is left
			SyncData,
			LastRecoveryHeight,
			UtxoStamp,
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
			AutoincrementID,
			ParamGet,
			ParamIns,
			ParamUpd,
			StateIns,
			StateDel,
			StateGet,
			StateGetHash,
			StateGetHeightAndPrev,
			StateFind,
			StateFind2,
			StateFindWithFlag,
			StateFindWorkGreater,
			StateUpdPrevRow,
			StateGetNextFCount,
			StateSetNextCount,
			StateSetNextCountF,
			StateGetHeightAndAux,
			StateGetNextFunctional,
			StateSetFlags,
			StateGetFlags0,
			StateGetFlags1,
			StateGetChainWork,
			StateGetNextCount,
			StateSetPeer,
			StateGetPeer,
			StateSetExtra,
			StateGetExtra,
			StateSetInputs,
			StateGetInputs,
			StateSetTxos,
			StateGetTxos,
			StateFindByTxos,
			TipAdd,
			TipDel,
			TipReachableAdd,
			TipReachableDel,
			EnumTips,
			EnumFunctionalTips,
			EnumAtHeight,
			EnumAncestors,
			StateGetPrev,
			Unactivate,
			Activate,
			MmrGet,
			MmrSet,
			HashForHist,
			StateGetBlock,
			StateSetBlock,
			StateDelBlock,
			EventIns,
			EventDel,
			EventEnum,
			EventFind,
			PeerAdd,
			PeerDel,
			PeerEnum,
			BbsEnumCSeq,
			BbsHistogram,
			BbsEnumAllSeq,
			BbsEnumAll,
			BbsFindRaw,
			BbsFind,
			BbsFindCursor,
			BbsDel,
			BbsIns,
			BbsMaxTime,
			BbsTotals,
			DummyIns,
			DummyFindLowest,
			DummyFind,
			DummyUpdHeight,
			DummyDel,
			KernelIns,
			KernelFind,
			KernelDel,
			TxoAdd,
			TxoDel,
			TxoDelFrom,
			TxoSetSpent,
			TxoEnum,
			TxoEnumBySpentMigrate,
			TxoSetValue,
			TxoGetValue,
			BlockFind,
			FindHeightBelow,
			EnumSystemStatesBkwd,

			Dbg0,
			Dbg1,
			Dbg2,
			Dbg3,
			Dbg4,

			count
		};
	};


	NodeDB();
	virtual ~NodeDB();

	void Close();
	void Open(const char* szPath);

	void Vacuum();
	void CheckIntegrity();

	virtual void OnModified() {}

	class Recordset
	{
		sqlite3_stmt* m_pStmt;
	public:

		NodeDB & m_DB;

		Recordset(NodeDB&);
		Recordset(NodeDB&, Query::Enum, const char*);
		~Recordset();

		void Reset();
		void Reset(Query::Enum, const char*);

		// Perform the query step. SELECT only: returns true while there're rows to read
		bool Step();
		void StepStrict(); // must return at least 1 row, applicable for SELECT

		// in/out
		void put(int col, uint32_t);
		void put(int col, uint64_t);
		void put(int col, const Blob&);
		void put(int col, const char*);
		void put(int col, const Key::ID&, Key::ID::Packed&);
		void get(int col, uint32_t&);
		void get(int col, uint64_t&);
		void get(int col, Blob&);
		void get(int col, ByteBuffer&); // don't over-use
		void get(int col, Key::ID&);

		const void* get_BlobStrict(int col, uint32_t n);

		template <typename T> void put_As(int col, const T& x) { put(col, Blob(&x, sizeof(x))); }
		template <typename T> void get_As(int col, T& out) { out = get_As<T>(col); }
		template <typename T> const T& get_As(int col) { return *(const T*) get_BlobStrict(col, sizeof(T)); }

		void putNull(int col);
		bool IsNull(int col);

		void put(int col, const Merkle::Hash& x) { put_As(col, x); }
		void get(int col, Merkle::Hash& x) { get_As(col, x); }
		void put(int col, const Block::PoW& x) { put_As(col, x); }
		void get(int col, Block::PoW& x) { get_As(col, x); }
	};

	int get_RowsChanged() const;
	uint64_t get_LastInsertRowID() const;

	class Transaction {
		NodeDB* m_pDB;
	public:
		Transaction(NodeDB* = NULL);
		Transaction(NodeDB& db) :Transaction(&db) {}
		~Transaction(); // by default - rolls back

		bool IsInProgress() const { return NULL != m_pDB; }

		void Start(NodeDB&);
		void Commit();
		void Rollback();
	};

	// Hi-level functions

	void ParamSet(uint32_t ID, const uint64_t*, const Blob*);
	bool ParamGet(uint32_t ID, uint64_t*, Blob*, ByteBuffer* = NULL);

	uint64_t ParamIntGetDef(int ID, uint64_t def = 0);

	uint64_t InsertState(const Block::SystemState::Full&); // Fails if state already exists

	uint64_t FindActiveStateStrict(Height);
	uint64_t StateFindSafe(const Block::SystemState::ID&);
	void get_State(uint64_t rowid, Block::SystemState::Full&);
	void get_StateHash(uint64_t rowid, Merkle::Hash&);

	bool DeleteState(uint64_t rowid, uint64_t& rowPrev); // State must exist. Returns false if there are ancestors.

	uint32_t GetStateNextCount(uint64_t rowid);
	uint32_t GetStateFlags(uint64_t rowid);
	void SetFlags(uint64_t rowid, uint32_t);

	void SetStateFunctional(uint64_t rowid);
	void SetStateNotFunctional(uint64_t rowid);

	void set_Peer(uint64_t rowid, const PeerID*);
	bool get_Peer(uint64_t rowid, PeerID&);

	void set_StateExtra(uint64_t rowid, const ECC::Scalar*);
	bool get_StateExtra(uint64_t rowid, ECC::Scalar&);

	void set_StateTxos(uint64_t rowid, const TxoID*);
	TxoID get_StateTxos(uint64_t rowid);

	void SetStateBlock(uint64_t rowid, const Blob& bodyP, const Blob& bodyE);
	void GetStateBlock(uint64_t rowid, ByteBuffer* pP, ByteBuffer* pE);
	void DelStateBlockPP(uint64_t rowid); // delete perishable, peer. Keep ethernal
	void DelStateBlockAll(uint64_t rowid);

	struct StateID {
		uint64_t m_Row;
		Height m_Height;
		void SetNull();
	};

	void get_StateID(const StateID&, Block::SystemState::ID&);

	TxoID FindStateByTxoID(StateID&, TxoID); // returns the Txos at state end

	struct WalkerState {
		Recordset m_Rs;
		StateID m_Sid;

		WalkerState(NodeDB& db) :m_Rs(db) {}
		bool MoveNext();
	};

#pragma pack (push, 1)
	struct StateInput
	{
		ECC::uintBig m_CommX;
		TxoID m_Txo_AndY;

		static const TxoID s_Y = TxoID(1) << (sizeof(TxoID) * 8 - 1);

		void Set(TxoID, const ECC::Point&);
		void Set(TxoID, const ECC::uintBig& x, uint8_t y);

		TxoID get_ID() const;
		void Get(ECC::Point&) const;

		static bool IsLess(const StateInput&, const StateInput&);
	};
#pragma pack (pop)

	void set_StateInputs(uint64_t rowid, StateInput*, size_t);
	bool get_StateInputs(uint64_t rowid, std::vector<StateInput>&);

	void EnumTips(WalkerState&); // height lowest to highest
	void EnumFunctionalTips(WalkerState&); // chainwork highest to lowest

	Height get_HeightBelow(Height);
	void EnumStatesAt(WalkerState&, Height);
	void EnumAncestors(WalkerState&, const StateID&);
	bool get_Prev(StateID&);
	bool get_Prev(uint64_t&);

	bool get_Cursor(StateID& sid);

    void get_Proof(Merkle::IProofBuilder&, const StateID& sid, Height hPrev);
    void get_PredictedStatesHash(Merkle::Hash&, const StateID& sid); // For the next block.

	void get_ChainWork(uint64_t, Difficulty::Raw&);

	// the following functions move the curos, and mark the states with 'Active' flag
	void MoveBack(StateID&);
	void MoveFwd(const StateID&);

	void assert_valid(); // diagnostic, for tests only

	void InsertEvent(Height, const Blob&, const Blob& key);
	void DeleteEventsFrom(Height);

	struct WalkerEvent {
		Recordset m_Rs;
		Height m_Height;
		Blob m_Body;
		Blob m_Key;

		WalkerEvent(NodeDB& db) :m_Rs(db) {}
		bool MoveNext();
	};

	void EnumEvents(WalkerEvent&, Height hMin);
	void FindEvents(WalkerEvent&, const Blob& key);

	struct WalkerPeer
	{
		Recordset m_Rs;

		struct Data {
			PeerID m_ID;
			uint32_t m_Rating;
			uint64_t m_Address;
			Timestamp m_LastSeen;
		} m_Data;

		WalkerPeer(NodeDB& db) :m_Rs(db) {}
		bool MoveNext();
	};

	void EnumPeers(WalkerPeer&); // highest to lowest
	void PeerIns(const WalkerPeer::Data&);
	void PeersDel();

	struct WalkerBbs
	{
		typedef ECC::Hash::Value Key;

		Recordset m_Rs;
		uint64_t m_ID;

		struct Data {
			Key m_Key;
			BbsChannel m_Channel;
			Timestamp m_TimePosted;
			Blob m_Message;
			uint32_t m_Nonce;
		} m_Data;

		WalkerBbs(NodeDB& db) :m_Rs(db) {}
		bool MoveNext();
	};

	void EnumBbsCSeq(WalkerBbs&); // set channel and ID before invocation
	uint64_t BbsIns(const WalkerBbs::Data&); // must be unique (if not sure - first try to find it). Returns the ID
	bool BbsFind(WalkerBbs&); // set Key
	uint64_t BbsFind(const WalkerBbs::Key&);
	void BbsDel(uint64_t id);
	uint64_t BbsFindCursor(Timestamp);
	Timestamp get_BbsMaxTime();
	uint64_t get_BbsLastID();

	struct BbsTotals {
		uint32_t m_Count;
		uint64_t m_Size;
	};

	void get_BbsTotals(BbsTotals&);

	struct WalkerBbsLite
	{
		typedef WalkerBbs::Key Key;

		Recordset m_Rs;
		uint64_t m_ID;
		Key m_Key;
		uint32_t m_Size;

		WalkerBbsLite(NodeDB& db) :m_Rs(db) {}
		bool MoveNext();
	};

	void EnumAllBbsSeq(WalkerBbsLite&); // ordered by m_ID. Must be initialized to specify the lower bound

	struct WalkerBbsTimeLen
	{
		Recordset m_Rs;
		uint64_t m_ID;
		Timestamp m_Time;
		uint32_t m_Size;

		WalkerBbsTimeLen(NodeDB& db) :m_Rs(db) {}
		bool MoveNext();
	};

	void EnumAllBbs(WalkerBbsTimeLen&); // ordered by m_ID.

	struct IBbsHistogram {
		virtual bool OnChannel(BbsChannel, uint64_t nCount) = 0;
	};
	bool EnumBbs(IBbsHistogram&);


	void InsertDummy(Height h, const Key::ID&);
	Height GetLowestDummy(Key::ID&);
	void DeleteDummy(const Key::ID& kid);
	void SetDummyHeight(const Key::ID&, Height);
	Height GetDummyHeight(const Key::ID&);

	void InsertKernel(const Blob&, Height h);
	void DeleteKernel(const Blob&, Height h);
	Height FindKernel(const Blob&); // in case of duplicates - returning the one with the largest Height
    Height FindBlock(const Blob&);

	uint64_t FindStateWorkGreater(const Difficulty::Raw&);

	void TxoAdd(TxoID, const Blob&);
	void TxoDel(TxoID);
	void TxoDelFrom(TxoID);
	void TxoSetSpent(TxoID, Height);

	struct WalkerTxo
	{
		Recordset m_Rs;
		TxoID m_ID;
		Blob m_Value;
		Height m_SpendHeight;

		WalkerTxo(NodeDB& db) :m_Rs(db) {}
		bool MoveNext();
	};

	void EnumTxos(WalkerTxo&, TxoID id0);
	void TxoSetValue(TxoID, const Blob&);
	void TxoGetValue(WalkerTxo&, TxoID);

	struct WalkerSystemState
	{
		Recordset m_Rs;
		uint64_t m_RowTrg;
		Block::SystemState::Full m_State;

		WalkerSystemState(NodeDB& db) :m_Rs(db) {}
		bool MoveNext();
	};

	void EnumSystemStatesBkwd(WalkerSystemState&, const StateID&);

private:

	sqlite3* m_pDb;

	struct Statement
	{
		sqlite3_stmt* m_pStmt;
		Statement() :m_pStmt(nullptr) {}
		~Statement() { Close(); }

		void Close();
	};

	Statement m_pPrep[Query::count];

	void Prepare(Statement&, const char*);

	void TestRet(int);
	void ThrowSqliteError(int);
	static void ThrowError(const char*);
	static void ThrowInconsistent();

	void Create();
	void CreateTableDummy();
	void CreateTableTxos();
	void ExecQuick(const char*);
	std::string ExecTextOut(const char*);
	bool ExecStep(sqlite3_stmt*);
	bool ExecStep(Query::Enum, const char*); // returns true while there's a row

	sqlite3_stmt* get_Statement(Query::Enum, const char*);

	uint64_t get_AutoincrementID(const char* szTable);
	void TipAdd(uint64_t rowid, Height);
	void TipDel(uint64_t rowid, Height);
	void TipReachableAdd(uint64_t rowid);
	void TipReachableDel(uint64_t rowid);
	void SetNextCount(uint64_t rowid, uint32_t);
	void SetNextCountFunctional(uint64_t rowid, uint32_t);
	void OnStateReachable(uint64_t rowid, uint64_t rowPrev, Height, bool);
	void BuildMmr(uint64_t rowid, uint64_t rowPrev, Height);
	void put_Cursor(const StateID& sid); // jump

	void TestChanged1Row();

	void MigrateFrom18();

	struct Dmmr;
};



} // namespace beam
