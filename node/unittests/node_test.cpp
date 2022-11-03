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

//#include "../node.h"

#include "../node.h"
#include "../db.h"
#include "../processor.h"
#include "../../core/fly_client.h"
#include "../../core/serialization_adapters.h"
#include "../../core/treasury.h"
#include "../../core/block_rw.h"
#include "../../utility/test_helpers.h"
#include "../../utility/serialize.h"
#include "../../utility/blobmap.h"
#include "../../core/unittest/mini_blockchain.h"
#include "../../bvm/bvm2.h"
#include "../../bvm/ManagerStd.h"

#ifndef LOG_VERBOSE_ENABLED
    #define LOG_VERBOSE_ENABLED 0
#endif
#include "utility/logger.h"

namespace ECC {

	void SetRandom(uintBig& x)
	{
		GenRandom(x);
	}

	void SetRandom(Scalar::Native& x)
	{
		Scalar s;
		while (true)
		{
			SetRandom(s.m_Value);
			if (!x.Import(s))
				break;
		}
	}

	void SetRandom(Key::IKdf::Ptr& pRes)
	{
		uintBig seed;
		SetRandom(seed);
		HKdf::Create(pRes, seed);
	}

	void SetRandom(beam::Node& n)
	{
		uintBig seed;
		SetRandom(seed);

		n.m_Keys.InitSingleKey(seed);
	}
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

#define verify_test(x) \
	do { \
		if (!(x)) \
			TestFailed(#x, __LINE__); \
	} while (false)

#define fail_test(msg) TestFailed(msg, __LINE__)

namespace beam
{
	ByteBuffer g_Treasury;

	Amount get_Emission(const HeightRange& hr, Amount base = Rules::get().Emission.Value0)
	{
		AmountBig::Type vbig;
		Rules::get().get_Emission(vbig, hr, base);

		Amount res;
		vbig.ExportWord<1>(res);
		return res;
	}

	void PrintEmissionSchedule()
	{
		struct Year
		{
			Amount m_PerBlockMiner;
			Amount m_PerBlockTreasury;
			Amount m_PerBlock;
			Amount m_Total;
		};

		std::vector<Year> vYears;
		HeightRange hr(0, 0);
		const uint32_t nYearBlocks = 1440 * 365;

		Amount nCumulative = 0;

		for (uint32_t iY = 0; ; iY++)
		{
			hr.m_Min = hr.m_Max + 1;
			hr.m_Max += nYearBlocks;

			Amount val = get_Emission(hr);
			if (!val)
				break;

			vYears.emplace_back();
			Year& y = vYears.back();

			y.m_PerBlock = val / nYearBlocks;
			y.m_PerBlockMiner = y.m_PerBlock;

			if (iY < 5)
			{
				y.m_PerBlockTreasury = y.m_PerBlockMiner / 4;
				y.m_PerBlock += y.m_PerBlockTreasury;
				val += y.m_PerBlockTreasury * nYearBlocks;
			}
			else
				y.m_PerBlockTreasury = 0;

			y.m_Total = val;
			nCumulative += val;
		}

		std::cout << "Emission schedule" << std::endl;
		std::cout << "Year, Miner emission per block, Treasury emission per block, Total coins emitted per block, Total coins emitted per year, Cumulative Coins Emitted, Cumulative % of total" << std::endl;

		Amount nEmitted = 0;
		uint32_t nYear = 2019;
		for (size_t i = 0; i < vYears.size(); i++)
		{
			const Year& y = vYears[i];

			std::cout << (nYear++) << "," << y.m_PerBlockMiner << "," << y.m_PerBlockTreasury << "," << y.m_PerBlock << "," << y.m_Total << ",";
			std::cout << (nEmitted += y.m_Total) << ",";
			std::cout << double(nEmitted) * 100. / double(nCumulative) << std::endl;
		}

		nCumulative++;
	}

	void PrepareTreasury()
	{
		Key::IKdf::Ptr pKdf;
		ECC::SetRandom(pKdf);

		PeerID pid;
		ECC::Scalar::Native sk;
		Treasury::get_ID(*pKdf, pid, sk);

		Treasury tres;
		Treasury::Parameters pars;
		pars.m_Bursts = 1;
		Treasury::Entry* pE = tres.CreatePlan(pid, Rules::get().Emission.Value0 / 5, pars);

		pE->m_pResponse.reset(new Treasury::Response);
		uint64_t nIndex = 1;
		verify_test(pE->m_pResponse->Create(pE->m_Request, *pKdf, nIndex));

		Treasury::Data data;
		data.m_sCustomMsg = "test treasury";
		tres.Build(data);

		beam::Serializer ser;
		ser & data;

		ser.swap_buf(g_Treasury);

		ECC::Hash::Processor() << Blob(g_Treasury) >> Rules::get().TreasuryChecksum;
	}

	uint32_t CountTips(NodeDB& db, bool bFunctional, NodeDB::StateID* pLast = NULL)
	{
		uint32_t nTips = 0;

		NodeDB::WalkerState ws;

		if (bFunctional)
			db.EnumFunctionalTips(ws);
		else
			db.EnumTips(ws);

		while (ws.MoveNext())
			nTips++;

		if (pLast)
			*pLast = ws.m_Sid;

		return nTips;
	}

	struct StoragePts
	{
		ECC::Point::Storage m_pArr[18];

		void Init()
		{
			for (size_t i = 0; i < _countof(m_pArr); i++)
			{
				m_pArr[i].m_X = i;
			}
		}

		bool IsValid(size_t i0, size_t i1, uint32_t n0) const
		{
			for (; i0 < i1; i0++)
			{
				if (m_pArr[i0].m_X != ECC::uintBig(n0++))
					return false;
			}

			return true;
		}
	};

	void TestNodeDB(const char* sz)
	{
		NodeDB db;
		db.Open(sz);

		NodeDB::Transaction tr(db);

		const uint32_t hMax = 250;
		const uint32_t nOrd = 3;
		const uint32_t hFork0 = 70;

		Merkle::Hash hvZero(Zero);

		std::vector<Block::SystemState::Full> vStates;
		vStates.resize(hMax);
		memset0(&vStates.at(0), vStates.size());

		Difficulty::Raw d0;
		Difficulty(0).Unpack(d0);

		Merkle::CompactMmr cmmr, cmmrFork;

		for (uint32_t h = 0; h < hMax; h++)
		{
			Block::SystemState::Full& s = vStates[h];
			s.m_Height = h + Rules::HeightGenesis;

			s.m_ChainWork = d0;
			if (h)
				s.m_ChainWork += vStates[h - 1].m_ChainWork;

			if (h)
			{
				vStates[h - 1].get_Hash(s.m_Prev);
				cmmr.Append(s.m_Prev);
			}

			if (hFork0 == h)
				cmmrFork = cmmr;

			cmmr.get_Hash(s.m_Definition);
			Merkle::Interpret(s.m_Definition, hvZero, true);

			s.m_Kernels = Zero;
		}

		PeerID peer, peer2;
		memset(peer.m_pData, 0x66, peer.nBytes);

		uint64_t pRows[hMax];

		// insert states in random order
		for (uint32_t h1 = 0; h1 < nOrd; h1++)
		{
			for (uint32_t h = h1; h < hMax; h += nOrd)
			{
				pRows[h] = db.InsertState(vStates[h], peer);
				db.assert_valid();

				if (h)
				{
					db.SetStateFunctional(pRows[h]);
					db.assert_valid();
				}
			}
		}

		Blob bBodyP("body", 4), bBodyE("abc", 3);

		db.SetStateBlock(pRows[0], bBodyP, bBodyE, peer);
		db.set_Peer(pRows[0], nullptr);
		verify_test(!db.get_Peer(pRows[0], peer2));

		db.set_Peer(pRows[0], &peer);
		verify_test(db.get_Peer(pRows[0], peer2));
		verify_test(peer == peer2);

		db.set_Peer(pRows[0], NULL);
		verify_test(!db.get_Peer(pRows[0], peer2));

		ByteBuffer bbBodyP, bbBodyE;
		db.GetStateBlock(pRows[0], &bbBodyP, &bbBodyE, nullptr);

		//db.DelStateBlockPP(pRows[0]);
		//db.GetStateBlock(pRows[0], &bbBodyP, &bbBodyE);

		db.DelStateBlockAll(pRows[0]);
		db.GetStateBlock(pRows[0], &bbBodyP, &bbBodyE, nullptr);

		tr.Commit();
		tr.Start(db);

		verify_test(CountTips(db, false) == 1);
		verify_test(CountTips(db, true) == 0);

		// a subbranch
		Block::SystemState::Full s = vStates[hFork0];
		s.m_Definition.Inc(); // alter

		uint64_t r0 = db.InsertState(s, peer);

		verify_test(CountTips(db, false) == 2);

		db.assert_valid();
		db.SetStateFunctional(r0);
		db.assert_valid();

		verify_test(CountTips(db, true) == 0);

		s.get_Hash(s.m_Prev);
		cmmrFork.Append(s.m_Prev);
		cmmrFork.get_Hash(s.m_Definition);
		Merkle::Interpret(s.m_Definition, hvZero, true);

		s.m_Height++;
		s.m_ChainWork += d0;

		uint64_t rowLast1 = db.InsertState(s, peer);

		NodeDB::StateID sid;
		verify_test(CountTips(db, false, &sid) == 2);
		verify_test(sid.m_Height == hMax-1 + Rules::HeightGenesis);

		db.SetStateFunctional(rowLast1);
		db.assert_valid();

		db.SetStateFunctional(pRows[0]); // this should trigger big update
		db.assert_valid();
		verify_test(CountTips(db, true, &sid) == 2);
		verify_test(sid.m_Height == hFork0 + 1 + Rules::HeightGenesis);

		tr.Commit();
		tr.Start(db);

		NodeDB::StateID sid2;
		verify_test(CountTips(db, false, &sid2) == 2);
		verify_test(sid2.m_Height == hMax-1 + Rules::HeightGenesis);

		while (db.get_Prev(sid))
			;
		verify_test(sid.m_Height == Rules::HeightGenesis);

		db.SetStateNotFunctional(pRows[0]);
		db.assert_valid();
		verify_test(CountTips(db, true) == 0);

		db.SetStateFunctional(pRows[0]);
		db.assert_valid();
		verify_test(CountTips(db, true) == 2);

		// test cursor and StatesMmr
		NodeDB::StatesMmr smmr(db);
		Merkle::Hash hvRoot(Zero);

		for (sid.m_Height = Rules::HeightGenesis; sid.m_Height < hMax + Rules::HeightGenesis; sid.m_Height++)
		{
			sid.m_Row = pRows[sid.m_Height - Rules::HeightGenesis];
			db.MoveFwd(sid);
			
			Merkle::Hash hv;
			if (sid.m_Height < Rules::HeightGenesis + 50) // skip it for big heights, coz it's quadratic
			{
				for (Height h = Rules::HeightGenesis; h < sid.m_Height; h++)
				{
					Merkle::ProofBuilderStd bld;
					smmr.get_Proof(bld, smmr.H2I(h));

					vStates[h - Rules::HeightGenesis].get_Hash(hv);
					Merkle::Interpret(hv, bld.m_Proof);
					verify_test(hvRoot == hv);
				}
			}

			const Block::SystemState::Full& sTop = vStates[sid.m_Height - Rules::HeightGenesis];

			hv = hvRoot;
			Merkle::Interpret(hv, hvZero, true);
			verify_test(hv == sTop.m_Definition);

			sTop.get_Hash(hv);
			smmr.get_PredictedHash(hvRoot, hv);

			smmr.Append(hv);

			smmr.get_Hash(hv);
			verify_test(hv == hvRoot);


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
			verify_test(row);
			if (!db.DeleteState(row, row))
				break;
			db.assert_valid();
		}

		verify_test(row && (h == hFork0));

		for (h += 2; ; h--)
		{
			if (!rowLast1)
				break;
			verify_test(db.DeleteState(rowLast1, rowLast1));
			db.assert_valid();
		}

		verify_test(!h);

		tr.Commit();
		tr.Start(db);

		for (int i = 0; i < 20; i++)
		{
			NodeDB::WalkerPeer::Data d;
			ECC::SetRandom(d.m_ID);
			d.m_Address = i * 17;
			d.m_LastSeen = i + 10;
			d.m_Rating = i * 100 + 50;

			db.PeerIns(d);
		}

		NodeDB::WalkerPeer wlkp;
		for (db.EnumPeers(wlkp); wlkp.MoveNext(); )
			;

		db.PeersDel();

		for (db.EnumPeers(wlkp); wlkp.MoveNext(); )
			;


		NodeDB::WalkerBbs::Data dBbs;

		for (uint32_t i = 0; i < 200; i++)
		{
			dBbs.m_Key = i;
			dBbs.m_Channel = i % 7;
			dBbs.m_TimePosted = i + 100;
			dBbs.m_Message.p = "hello";
			dBbs.m_Message.n = 5;

			db.BbsIns(dBbs);
		}

		NodeDB::WalkerBbs wlkbbs;
		wlkbbs.m_Data = dBbs;
		verify_test(db.BbsFind(wlkbbs));

		wlkbbs.m_Data.m_Key.Inc();
		verify_test(!db.BbsFind(wlkbbs));


		for (wlkbbs.m_Data.m_Channel = 0; wlkbbs.m_Data.m_Channel < 7; wlkbbs.m_Data.m_Channel++)
		{
			wlkbbs.m_ID = 0;
			for (db.EnumBbsCSeq(wlkbbs); wlkbbs.MoveNext(); )
				;
		}

		for (wlkbbs.m_Data.m_Channel = 0; wlkbbs.m_Data.m_Channel < 7; wlkbbs.m_Data.m_Channel++)
		{
			wlkbbs.m_ID = 0;
			for (db.EnumBbsCSeq(wlkbbs); wlkbbs.MoveNext(); )
				;
		}

		Key::ID kid(Zero);
		kid.m_Idx = 345;

		verify_test(db.GetDummyHeight(kid) == MaxHeight);

		db.InsertDummy(176, kid);

		kid.m_Idx = 346;
		db.InsertDummy(568, kid);

		kid.m_Idx = 345;
		verify_test(db.GetDummyHeight(kid) == 176);

		Height h1 = db.GetLowestDummy(kid);
		verify_test(h1 == 176);
		verify_test(kid.m_Idx == 345U);

		db.SetDummyHeight(kid, 1055);

		h1 = db.GetLowestDummy(kid);
		verify_test(h1 == 568);
		verify_test(kid.m_Idx == 346U);
		
		db.DeleteDummy(kid);

		h1 = db.GetLowestDummy(kid);
		verify_test(h1 == 1055);
		verify_test(kid.m_Idx == 345U);

		db.DeleteDummy(kid);

		verify_test(MaxHeight == db.GetLowestDummy(kid));

		// Kernels
		db.InsertKernel(bBodyP, 5);
		db.InsertKernel(bBodyP, 5); // duplicate
		db.InsertKernel(bBodyP, 7);
		db.InsertKernel(bBodyP, 2);

		verify_test(db.FindKernel(bBodyP) == 7);
		verify_test(db.FindKernel(bBodyE) == 0);

		db.DeleteKernel(bBodyP, 7);
		verify_test(db.FindKernel(bBodyP) == 5);
		db.DeleteKernel(bBodyP, 5);
		verify_test(db.FindKernel(bBodyP) == 5);
		db.DeleteKernel(bBodyP, 2);
		verify_test(db.FindKernel(bBodyP) == 5);
		db.DeleteKernel(bBodyP, 5);
		verify_test(db.FindKernel(bBodyP) == 0);

		// Shielded
		TxoID nShielded = 16 * 1024 * 3 + 5;
		db.ShieldedResize(nShielded, 0);

		StoragePts pts;
		pts.Init();

		db.ShieldedWrite(16 * 1024 * 2 - 2, pts.m_pArr, _countof(pts.m_pArr));

		ZeroObject(pts.m_pArr);

		db.ShieldedRead(16 * 1024 * 3 + 5 - _countof(pts.m_pArr), pts.m_pArr, _countof(pts.m_pArr));
		verify_test(memis0(pts.m_pArr, sizeof(pts.m_pArr)));

		db.ShieldedRead(16 * 1024 * 2 -2, pts.m_pArr, _countof(pts.m_pArr));
		verify_test(pts.IsValid(0, _countof(pts.m_pArr), 0));

		db.ShieldedResize(1, nShielded);
		db.ShieldedResize(0, 1);

		ECC::uintBig k1 = 223U;
		Blob val(nullptr, 0);

		verify_test(db.UniqueInsertSafe(k1, &val));
		db.UniqueDeleteStrict(k1);
		verify_test(db.UniqueInsertSafe(k1, nullptr));
		verify_test(!db.UniqueInsertSafe(k1, nullptr));


		// Assets
		Asset::Full ai1, ai2;
		ZeroObject(ai1);

		for (uint32_t i = 1; i <= 5; i++)
		{
			ai1.m_ID = 0;
			db.AssetAdd(ai1);
			verify_test(ai1.m_ID == i);
		}

		verify_test(db.AssetDelete(5) == 4); // should shrink
		verify_test(db.AssetDelete(3) == 4); // should retain the same size

		ai2.m_ID = 3;
		verify_test(!db.AssetGetSafe(ai2));
		ai2.m_ID = 2;
		verify_test(db.AssetGetSafe(ai2));
		verify_test(ai2.m_Owner == ai1.m_Owner);

		ai1.m_Owner.Inc();
		ai1.m_Owner.Negate();
		ai1.m_ID = 0;
		db.AssetAdd(ai1);
		verify_test(ai1.m_ID == 3);

		AmountBig::Type assetVal1, assetVal2 = 1U;
		ai2.m_ID = 3;
		verify_test(db.AssetGetSafe(ai2));
		verify_test(ai2.m_Value == Zero);

		assetVal2 = 334U;
		db.AssetSetValue(3, assetVal2, 18);

		verify_test(db.AssetGetSafe(ai2));
		verify_test(ai2.m_Value == assetVal2);
		verify_test(ai2.m_LockHeight == 18);

		ai1.m_ID = db.AssetFindByOwner(ai1.m_Owner);
		verify_test(ai1.m_ID == 3);
		ai1.m_Value = Zero;
		verify_test(db.AssetGetSafe(ai1));
		verify_test(ai1.m_Value == assetVal2);

		verify_test(db.AssetDelete(2) == 4);
		verify_test(db.AssetDelete(3) == 4);
		verify_test(db.AssetDelete(4) == 1);
		verify_test(db.AssetDelete(1) == 0);

		// StreamMmr, test cache
		struct MyMmr
			:public NodeDB::StreamMmr
		{
			using StreamMmr::StreamMmr;
			uint32_t m_Total = 0;
			uint32_t m_Miss = 0;

			virtual void LoadElement(Merkle::Hash& hv, const Merkle::Position& pos) const override
			{
				Cast::NotConst(this)->m_Total++;
				if (!CacheFind(hv, pos))
				{
					Cast::NotConst(this)->m_Miss++;
					StreamMmr::LoadElement(hv, pos);
				}
			}
		};

		MyMmr myMmr(db, NodeDB::StreamType::ShieldedMmr, true);

		for (uint32_t i = 0; i < 40; i++)
		{
			Merkle::Hash hv = i;
			myMmr.Append(hv);
			myMmr.get_Hash(hv);
		}

		// in a 'friendly' scenario, where we only add and calculate root - cache must be 100% effective
		verify_test(!myMmr.m_Miss);

		tr.Commit();

		// Contract data
		NodeDB::Recordset rs;
		Blob blob1;
		ECC::Hash::Value hvKey = 234U, hvVal = 1232U, hvKey2;
		verify_test(!db.ContractDataFind(hvKey, blob1, rs));

		blob1 = hvKey;
		verify_test(!db.ContractDataFindNext(blob1, rs));

		db.ContractDataInsert(hvKey, hvVal);
		verify_test(!db.ContractDataFindNext(blob1, rs));

		hvVal.Inc();
		db.ContractDataUpdate(hvKey, hvVal);

		verify_test(db.ContractDataFind(hvKey, blob1, rs));
		verify_test(Blob(hvVal) == blob1);

		blob1 = hvKey2;
		hvKey2 = hvKey;
		hvKey2.Inc();
		verify_test(!db.ContractDataFindNext(blob1, rs));

		hvKey2 = hvKey;
		hvKey2.Negate();
		hvKey2 += ECC::Hash::Value(2U);
		hvKey2.Negate();
		verify_test(db.ContractDataFindNext(blob1, rs));
		verify_test(Blob(hvKey) == blob1);

		db.ContractDataDel(hvKey);
		verify_test(!db.ContractDataFind(hvKey, blob1, rs));

		// contract logs
		NodeDB::ContractLog::Entry cdl;
		bvm2::ContractID cid = 15U;
		cdl.m_Key = cid;

		hvKey = 1043U;
		hvKey2 = 1045U;

		cdl.m_Val = hvKey;

		cdl.m_Pos.m_Height = 15;
		cdl.m_Pos.m_Pos = 3;
		db.ContractLogInsert(cdl);

		cdl.m_Pos.m_Height = 16;
		db.ContractLogInsert(cdl);

		cid = 19U;
		cdl.m_Pos.m_Height = 17;
		cdl.m_Pos.m_Pos = 4;
		db.ContractLogInsert(cdl);

		NodeDB::ContractLog::Walker wlkCdl;
		db.ContractLogEnum(wlkCdl, HeightPos(0), HeightPos(MaxHeight));
		verify_test(wlkCdl.MoveNext());
		verify_test(wlkCdl.MoveNext());
		verify_test(wlkCdl.MoveNext());
		verify_test(!wlkCdl.MoveNext());

		db.ContractLogEnum(wlkCdl, cid, cid, HeightPos(0), HeightPos(MaxHeight));
		verify_test(wlkCdl.MoveNext());
		verify_test(!wlkCdl.MoveNext());

		db.ContractLogDel(HeightPos(0), HeightPos(16, 0xffff));

		db.ContractLogEnum(wlkCdl, HeightPos(0), HeightPos(MaxHeight));
		verify_test(wlkCdl.MoveNext());
		verify_test(!wlkCdl.MoveNext());

		// Cache
		{
			ECC::Hash::Value key1 = 1U;
			ECC::Hash::Value key2 = 2U;
			ECC::Hash::Value key3 = 3U;

			db.CacheInsert(key1, key1);
			db.CacheInsert(key2, key2);
			db.CacheInsert(key3, key3);

			NodeDB::CacheState cs;
			db.get_CacheState(cs);
			verify_test(cs.m_HitCounter == 3);
			verify_test(cs.m_SizeCurrent == key1.nBytes * 3);

			ByteBuffer buf;
			verify_test(db.CacheFind(key2, buf));
			verify_test(Blob(buf) == Blob(key2));

			db.get_CacheState(cs);
			verify_test(cs.m_HitCounter == 4);

			db.CacheSetMaxSize(key1.nBytes * 2); // should throw out key1
			db.get_CacheState(cs);
			verify_test(cs.m_SizeCurrent == cs.m_SizeMax);
			verify_test(!db.CacheFind(key1, buf));

			verify_test(db.CacheFind(key3, buf));
			verify_test(Blob(buf) == Blob(key3));
			verify_test(db.CacheFind(key2, buf));
			verify_test(Blob(buf) == Blob(key2));

			db.CacheInsert(key1, key1); // should throw out key3
			verify_test(!db.CacheFind(key3, buf));
			verify_test(db.CacheFind(key2, buf));
			verify_test(Blob(buf) == Blob(key2));

			db.CacheSetMaxSize(0);
			verify_test(!db.CacheFind(key2, buf));

			db.get_CacheState(cs);
			verify_test(cs.m_SizeCurrent == 0);
		}
	}

#ifdef WIN32
		const char* g_sz = "mytest.db";
		const char* g_sz2 = "mytest2.db";
		const char* g_sz3 = "recovery_info";
#else // WIN32
		const char* g_sz = "/tmp/mytest.db";
		const char* g_sz2 = "/tmp/mytest2.db";
		const char* g_sz3 = "/tmp/recovery_info";
#endif // WIN32

	void TestNodeDB()
	{
		TestNodeDB(g_sz); // will create

		{
			NodeDB db;
			db.Open(g_sz); // test to open already-existing DB
		}
	}

	struct MiniWallet
	{
		Key::IKdf::Ptr m_pKdf;
		uint32_t m_nRunningIndex = 0;

		struct MyUtxo
		{
			CoinID m_Cid;
		};

		void ToOutput(const MyUtxo& utxo, Transaction& tx, Height h, Height hIncubation) const
		{
			ECC::Scalar::Native k;

			bool bPublic = !utxo.m_Cid.m_AssetID; // confidential transactions will be too slow for test in debug mode.
			// But public don't support assets

			Output::Ptr pOut(new Output);
			pOut->m_Incubation = hIncubation;
			pOut->Create(h + 1, k, *m_pKdf, utxo.m_Cid, *m_pKdf, bPublic ? Output::OpCode::Public : Output::OpCode::Standard);

			tx.m_vOutputs.push_back(std::move(pOut));
			UpdateOffset(tx, k, true);
		}

		void ToCommtiment(const MyUtxo& utxo, ECC::Point& comm, ECC::Scalar::Native& k) const
		{
			CoinID::Worker(utxo.m_Cid).Create(k, comm, *m_pKdf);
		}

		void ToInput(const MyUtxo& utxo, Transaction& tx) const
		{
			ECC::Scalar::Native k;
			Input::Ptr pInp(new Input);

			ToCommtiment(utxo, pInp->m_Commitment, k);

			tx.m_vInputs.push_back(std::move(pInp));
			UpdateOffset(tx, k, false);
		}

		typedef std::multimap<Height, MyUtxo> UtxoQueue;
		UtxoQueue m_MyUtxos;

		bool m_AutoAddTxOutputs = true; // assume tx outputs are always ok, and add them to the m_MyUtxos

		const MyUtxo* AddMyUtxo(const CoinID& cid, Height hMaturity)
		{
			if (!cid.m_Value)
				return NULL;

			assert(!cid.m_AssetID); // currently we don't handle assets

			MyUtxo utxo;
			utxo.m_Cid = cid;

			return &m_MyUtxos.insert(std::make_pair(hMaturity, utxo))->second;
		}

		const MyUtxo* AddMyUtxo(const CoinID& cid)
		{
			Height h = cid.m_Idx; // this is our convention
			h += (Key::Type::Coinbase == cid.m_Type) ? Rules::get().Maturity.Coinbase : Rules::get().Maturity.Std;

			return AddMyUtxo(cid, h);
		}

		struct MyKernel
		{
			Amount m_Fee;
			ECC::Scalar::Native m_k;
			bool m_bUseHashlock;
			Height m_Height = 0;
			Merkle::Hash m_hvRelLock = Zero;

			void Export(TxKernelStd& krn) const
			{
				krn.m_Fee = m_Fee;
				krn.m_Height.m_Min = m_Height + 1;

				if (m_bUseHashlock)
				{
					krn.m_pHashLock.reset(new TxKernelStd::HashLock); // why not?
					ECC::Hash::Processor() << m_Fee << m_k >> krn.m_pHashLock->m_Value;
				}

				if (!(m_hvRelLock == Zero))
				{
					krn.m_pRelativeLock.reset(new TxKernelStd::RelativeLock);
					krn.m_pRelativeLock->m_ID = m_hvRelLock;
					krn.m_pRelativeLock->m_LockHeight = 1;
				}

				krn.Sign(m_k);
			}

			void Export(TxKernelStd::Ptr& pKrn) const
			{
				pKrn.reset(new TxKernelStd);
				Export(*pKrn);
			}
		};

		typedef std::vector<MyKernel> KernelList;
		KernelList m_MyKernels;

		Merkle::Hash m_hvKrnRel = Zero;

		static void UpdateOffset(Transaction& tx, const ECC::Scalar::Native& offs, bool bOutput)
		{
			ECC::Scalar::Native k = tx.m_Offset;
			if (bOutput)
				k += -offs;
			else
				k += offs;
			tx.m_Offset = k;
		}

		bool MakeTx(Transaction::Ptr& pTx, Height h, Height hIncubation)
		{
			Amount val = MakeTxInput(pTx, h);
			if (!val)
				return false;

			MakeTxOutput(*pTx, h, hIncubation, val);
			return true;
		}


		Amount MakeTxInput(Transaction::Ptr& pTx, Height h)
		{
			UtxoQueue::iterator it = m_MyUtxos.begin();
			if (m_MyUtxos.end() == it)
				return 0;

			if (it->first > h)
				return 0; // not spendable yet

			pTx = std::make_shared<Transaction>();
			pTx->m_Offset = Zero;

			const MyUtxo& utxo = it->second;
			assert(utxo.m_Cid.m_Value);

			ToInput(utxo, *pTx);

			Amount ret = utxo.m_Cid.m_Value;
			m_MyUtxos.erase(it);

			return ret;
		}

		void MakeTxKernel(Transaction& tx, Amount fee, Height h)
		{
			m_MyKernels.emplace_back();
			MyKernel& mk = m_MyKernels.back();
			mk.m_Fee = fee;
			mk.m_bUseHashlock = 0 != (1 & h);
			mk.m_Height = h;

			if (!(m_hvKrnRel == Zero) && (h >= Rules::get().pForks[1].m_Height))
			{
				mk.m_hvRelLock = m_hvKrnRel;
				m_hvKrnRel = Zero;
			}

			m_pKdf->DeriveKey(mk.m_k, Key::ID(++m_nRunningIndex, Key::Type::Kernel));

			TxKernelStd::Ptr pKrn;
			mk.Export(pKrn);

			tx.m_vKernels.push_back(std::move(pKrn));
			UpdateOffset(tx, mk.m_k, true);
		}

		void MakeTxOutput(Transaction& tx, Height h, Height hIncubation, Amount val, Amount fee = 10900000)
		{
			if (fee >= val)
				MakeTxKernel(tx, val, h);
			else
			{
				MakeTxKernel(tx, fee, h);

				MyUtxo utxoOut;
				utxoOut.m_Cid.m_Value = val - fee;
				utxoOut.m_Cid.m_Idx = ++m_nRunningIndex;
				utxoOut.m_Cid.set_Subkey(0);
				utxoOut.m_Cid.m_Type = Key::Type::Regular;

				ToOutput(utxoOut, tx, h, hIncubation);

				if (m_AutoAddTxOutputs)
					m_MyUtxos.insert(std::make_pair(h + 1 + hIncubation, utxoOut));
			}

			tx.Normalize();

			Transaction::Context::Params pars;
			Transaction::Context ctx(pars);
			ctx.m_Height.m_Min = h + 1;
			bool isTxValid = tx.IsValid(ctx);
			verify_test(isTxValid);
		}
	};

	class MyNodeProcessor1
		:public NodeProcessor
	{
	public:

		TxPool::Fluff m_TxPool;
		MiniWallet m_Wallet;

		MyNodeProcessor1()
		{
			ECC::SetRandom(m_Wallet.m_pKdf);
		}
	};

	struct BlockPlus
	{
		typedef std::unique_ptr<BlockPlus> Ptr;

		Block::SystemState::Full m_Hdr;
		ByteBuffer m_BodyP;
		ByteBuffer m_BodyE;
	};

	void TestNodeProcessor1(std::vector<BlockPlus::Ptr>& blockChain)
	{
		MyNodeProcessor1 np;
		np.m_Horizon.m_Branching = 35;
		np.Initialize(g_sz);
		np.OnTreasury(g_Treasury);

		const Height hIncubation = 3; // artificial incubation period for outputs.

		for (Height h = Rules::HeightGenesis; h < 96 + Rules::HeightGenesis; h++)
		{
			while (true)
			{
				// Spend it in a transaction
				Transaction::Ptr pTx;
				if (!np.m_Wallet.MakeTx(pTx, np.m_Cursor.m_ID.m_Height, hIncubation))
					break;

				HeightRange hr(np.m_Cursor.m_ID.m_Height + 1, MaxHeight);

				uint32_t nBvmCharge = 0;
				verify_test(proto::TxStatus::Ok == np.ValidateTxContextEx(*pTx, hr, false, nBvmCharge, nullptr, nullptr, nullptr));

				Transaction::Context::Params pars;
				Transaction::Context ctx(pars);
				ctx.m_Height = np.m_Cursor.m_Sid.m_Height + 1;
				verify_test(pTx->IsValid(ctx));

				Transaction::KeyType key;
				pTx->get_Key(key);

				TxPool::Stats stats;
				stats.From(*pTx, ctx, 0, 0);

				np.m_TxPool.AddValidTx(std::move(pTx), stats, key, TxPool::Fluff::State::Fluffed);
			}

			NodeProcessor::BlockContext bc(np.m_TxPool, 0, *np.m_Wallet.m_pKdf, *np.m_Wallet.m_pKdf);
			verify_test(np.GenerateNewBlock(bc));

			np.OnState(bc.m_Hdr, PeerID());

			Block::SystemState::ID id;
			bc.m_Hdr.get_ID(id);

			np.OnBlock(id, bc.m_BodyP, bc.m_BodyE, PeerID());
			np.TryGoUp();

			np.m_Wallet.AddMyUtxo(CoinID(bc.m_Fees, h, Key::Type::Comission));
			np.m_Wallet.AddMyUtxo(CoinID(Rules::get_Emission(h), h, Key::Type::Coinbase));

			BlockPlus::Ptr pBlock(new BlockPlus);
			pBlock->m_Hdr = std::move(bc.m_Hdr);
			pBlock->m_BodyP = std::move(bc.m_BodyP);
			pBlock->m_BodyE = std::move(bc.m_BodyE);
			blockChain.push_back(std::move(pBlock));
		}

		for (Height h = 1; h <= np.m_Cursor.m_ID.m_Height; h++)
		{
			NodeDB::StateID sid;
			sid.m_Height = h;
			sid.m_Row = np.FindActiveAtStrict(h);

			Block::Body block;
			std::vector<Output::Ptr> vOutsIn;
			std::vector<NodeProcessor::ContractInvokeExtraInfo> vC;
			np.ExtractBlockWithExtra(block, vOutsIn, sid, vC);

			verify_test(vOutsIn.size() == block.m_vInputs.size());

			// inputs must come with maturities!
			for (size_t i = 0; i < block.m_vInputs.size(); i++)
			{
				const Input& inp = *block.m_vInputs[i];
				verify_test(inp.m_Commitment == vOutsIn[i]->m_Commitment);
				verify_test(inp.m_Internal.m_ID && inp.m_Internal.m_Maturity);
			}
		}

	}


	void TestNodeProcessor2(std::vector<BlockPlus::Ptr>& blockChain)
	{
		NodeProcessor::Horizon horz;
		horz.m_Branching = 12;
		horz.m_Sync.Hi = 12;
		horz.m_Sync.Lo = 15;
		horz.m_Local = horz.m_Sync;

		size_t nMid = blockChain.size() / 2;

		{
			NodeProcessor np;
			np.m_Horizon = horz;
			np.Initialize(g_sz);
			np.OnTreasury(g_Treasury);

			PeerID peer;
			ZeroObject(peer);

			for (size_t i = 0; i < blockChain.size(); i += 2)
				np.OnState(blockChain[i]->m_Hdr, peer);
		}

		{
			NodeProcessor np;
			np.m_Horizon = horz;
			np.Initialize(g_sz);

			PeerID peer;
			ZeroObject(peer);

			for (size_t i = 0; i < nMid; i += 2)
			{
				Block::SystemState::ID id;
				blockChain[i]->m_Hdr.get_ID(id);
				np.OnBlock(id, blockChain[i]->m_BodyP, blockChain[i]->m_BodyE, peer);
				np.TryGoUp();
			}
		}

		{
			NodeProcessor np;
			np.m_Horizon = horz;
			np.Initialize(g_sz);

			PeerID peer;
			ZeroObject(peer);

			for (size_t i = 1; i < blockChain.size(); i += 2)
				np.OnState(blockChain[i]->m_Hdr, peer);
		}

		{
			NodeProcessor np;
			np.m_Horizon = horz;
			np.Initialize(g_sz);

			PeerID peer;
			ZeroObject(peer);

			for (size_t i = 0; i < nMid; i++)
			{
				Block::SystemState::ID id;
				blockChain[i]->m_Hdr.get_ID(id);
				np.OnBlock(id, blockChain[i]->m_BodyP, blockChain[i]->m_BodyE, peer);
				np.TryGoUp();
			}
		}

		{
			NodeProcessor np;
			np.m_Horizon = horz;
			np.Initialize(g_sz);

			PeerID peer;
			ZeroObject(peer);

			for (size_t i = nMid; i < blockChain.size(); i++)
			{
				Block::SystemState::ID id;
				blockChain[i]->m_Hdr.get_ID(id);
				np.OnBlock(id, blockChain[i]->m_BodyP, blockChain[i]->m_BodyE, peer);
				np.TryGoUp();
			}
		}

		{
			NodeProcessor np;
			np.m_Horizon = horz;

			NodeProcessor::StartParams sp;
			sp.m_CheckIntegrity = true;
			sp.m_Vacuum = true;
			np.Initialize(g_sz, sp);
		}

	}

	void TestNodeProcessor3(std::vector<BlockPlus::Ptr>& blockChain)
	{
		NodeProcessor np, npSrc;
		np.m_Horizon.m_Branching = 5;
		np.m_Horizon.m_Sync.Hi = 12;
		np.m_Horizon.m_Sync.Lo = 30;
		np.m_Horizon.m_Local = np.m_Horizon.m_Sync;
		np.Initialize(g_sz);
		np.OnTreasury(g_Treasury);

		npSrc.Initialize(g_sz2);
		npSrc.OnTreasury(g_Treasury);

		PeerID pid(Zero);

		for (size_t i = 0; i < blockChain.size(); i++)
		{
			const BlockPlus& bp = *blockChain[i];
			verify_test(np.OnState(bp.m_Hdr, pid) == NodeProcessor::DataStatus::Accepted);

			verify_test(npSrc.OnState(bp.m_Hdr, pid) == NodeProcessor::DataStatus::Accepted);

			Block::SystemState::ID id;
			bp.m_Hdr.get_ID(id);
			verify_test(npSrc.OnBlock(id, bp.m_BodyP, bp.m_BodyE, pid) == NodeProcessor::DataStatus::Accepted);
		}

		npSrc.TryGoUp();
		verify_test(npSrc.m_Cursor.m_ID.m_Height == blockChain.size());

		np.EnumCongestions();

		verify_test(np.IsFastSync()); // should go into fast-sync mode
		verify_test(np.m_SyncData.m_TxoLo); // should be used on the 1st attempt

		// 1st attempt - tamper with txlo. Remove arbitrary input
		bool bTampered = false;
		for (Height h = Rules::HeightGenesis; h <= np.m_SyncData.m_TxoLo; h++)
		{
			NodeDB::StateID sid;
			sid.m_Row = npSrc.FindActiveAtStrict(h);
			sid.m_Height = h;

			ByteBuffer bbE, bbP;
			verify_test(npSrc.GetBlock(sid, &bbE, &bbP, 0, np.m_SyncData.m_TxoLo, np.m_SyncData.m_Target.m_Height, true));

			if (!bTampered)
			{
				Deserializer der;
				der.reset(bbP);

				Block::BodyBase bbb;
				TxVectors::Perishable txvp;
				der & bbb;
				der & txvp;

				verify_test(txvp.m_vInputs.empty()); // may contain only treasury, but we don't spend it in the test

				if (!txvp.m_vOutputs.empty())
				{
					txvp.m_vOutputs.pop_back();

					Serializer ser;
					ser & bbb;
					ser & txvp;
					ser.swap_buf(bbP);

					bTampered = true;
				}
			}

			Block::SystemState::ID id;
			blockChain[h-1]->m_Hdr.get_ID(id);
			verify_test(np.OnBlock(id, bbP, bbE, pid) == NodeProcessor::DataStatus::Accepted);
		}

		np.TryGoUp();
		verify_test(np.m_Cursor.m_ID.m_Height == Rules::HeightGenesis - 1); // should fall back to start
		verify_test(!np.m_SyncData.m_TxoLo); // next attempt should be with TxLo disabled


		// 1.1 attempt - tamper with txlo. Modify block offset
		np.m_SyncData.m_TxoLo = np.m_SyncData.m_Target.m_Height / 2;
		bTampered = false;
		for (Height h = Rules::HeightGenesis; h <= np.m_SyncData.m_TxoLo; h++)
		{
			NodeDB::StateID sid;
			sid.m_Row = npSrc.FindActiveAtStrict(h);
			sid.m_Height = h;

			ByteBuffer bbE, bbP;
			verify_test(npSrc.GetBlock(sid, &bbE, &bbP, 0, np.m_SyncData.m_TxoLo, np.m_SyncData.m_Target.m_Height, true));

			if (!bTampered)
			{
				Deserializer der;
				der.reset(bbP);

				Block::BodyBase bbb;
				TxVectors::Perishable txvp;
				der & bbb;
				der & txvp;

				bbb.m_Offset.m_Value.Inc();

				Serializer ser;
				ser & bbb;
				ser & txvp;
				ser.swap_buf(bbP);

				bTampered = true;
			}

			Block::SystemState::ID id;
			blockChain[h - 1]->m_Hdr.get_ID(id);
			verify_test(np.OnBlock(id, bbP, bbE, pid) == NodeProcessor::DataStatus::Accepted);
		}

		np.TryGoUp();
		verify_test(np.m_Cursor.m_ID.m_Height == Rules::HeightGenesis - 1); // should fall back to start
		verify_test(!np.m_SyncData.m_TxoLo); // next attempt should be with TxLo disabled

		// 2nd attempt. Tamper with the non-naked output
		np.m_SyncData.m_TxoLo = np.m_SyncData.m_Target.m_Height / 2;
		bTampered = false;
		for (Height h = Rules::HeightGenesis; h <= np.m_SyncData.m_Target.m_Height; h++)
		{
			NodeDB::StateID sid;
			sid.m_Row = npSrc.FindActiveAtStrict(h);
			sid.m_Height = h;

			ByteBuffer bbE, bbP;
			verify_test(npSrc.GetBlock(sid, &bbE, &bbP, 0, np.m_SyncData.m_TxoLo, np.m_SyncData.m_Target.m_Height, true));

			if (!bTampered)
			{
				Deserializer der;
				der.reset(bbP);

				Block::BodyBase bbb;
				TxVectors::Perishable txvp;
				der & bbb;
				der & txvp;

				for (size_t j = 0; j < txvp.m_vOutputs.size(); j++)
				{
					Output& outp = *txvp.m_vOutputs[j];
					if (outp.m_pConfidential)
					{
						outp.m_pConfidential->m_P_Tag.m_pCondensed[0].m_Value.Inc();
						bTampered = true;
						break;
					}
				}

				if (bTampered)
				{
					Serializer ser;
					ser & bbb;
					ser & txvp;
					ser.swap_buf(bbP);
				}
			}

			Block::SystemState::ID id;
			blockChain[h - 1]->m_Hdr.get_ID(id);
			verify_test(np.OnBlock(id, bbP, bbE, pid) == NodeProcessor::DataStatus::Accepted);

			np.TryGoUp();

			if (bTampered)
			{
				verify_test(np.m_Cursor.m_ID.m_Height == h - 1);
				break;
			}

			verify_test(np.m_Cursor.m_ID.m_Height == h);
		}

		verify_test(bTampered);
		verify_test(np.m_SyncData.m_TxoLo);

		// 3rd attempt. enforce "naked" output. The node won't notice a problem until all the blocks are fed
		bTampered = false;

		for (Height h = np.m_Cursor.m_ID.m_Height + 1; ; h++)
		{
			NodeDB::StateID sid;
			sid.m_Row = npSrc.FindActiveAtStrict(h);
			sid.m_Height = h;

			ByteBuffer bbE, bbP;
			verify_test(npSrc.GetBlock(sid, &bbE, &bbP, 0, np.m_SyncData.m_TxoLo, np.m_SyncData.m_Target.m_Height, true));

			if (!bTampered)
			{
				Deserializer der;
				der.reset(bbP);

				Block::BodyBase bbb;
				TxVectors::Perishable txvp;
				der & bbb;
				der & txvp;

				for (size_t j = 0; j < txvp.m_vOutputs.size(); j++)
				{
					Output& outp = *txvp.m_vOutputs[j];
					if (outp.m_pConfidential || outp.m_pPublic)
					{
						outp.m_pConfidential.reset();
						outp.m_pPublic.reset();
						bTampered = true;
						break;
					}
				}

				if (bTampered)
				{
					Serializer ser;
					ser & bbb;
					ser & txvp;
					ser.swap_buf(bbP);
				}
			}

			Block::SystemState::ID id;
			blockChain[h - 1]->m_Hdr.get_ID(id);
			verify_test(np.OnBlock(id, bbP, bbE, pid) == NodeProcessor::DataStatus::Accepted);

			bool bLast = (h == np.m_SyncData.m_Target.m_Height);

			np.TryGoUp();

			if (bLast)
			{
				verify_test(np.m_Cursor.m_ID.m_Height == Rules::HeightGenesis - 1);
				break;
			}

			verify_test(np.m_Cursor.m_ID.m_Height == h);
		}

		verify_test(!np.m_SyncData.m_TxoLo);

		// 3.1 Same as above, but now TxoLo is zero, Node should not erase all the blocks on error
		Height hTampered = 0;

		for (Height h = np.m_Cursor.m_ID.m_Height + 1; ; h++)
		{
			NodeDB::StateID sid;
			sid.m_Row = npSrc.FindActiveAtStrict(h);
			sid.m_Height = h;

			ByteBuffer bbE, bbP;
			verify_test(npSrc.GetBlock(sid, &bbE, &bbP, 0, np.m_SyncData.m_TxoLo, np.m_SyncData.m_Target.m_Height, true));

			if (!hTampered)
			{
				Deserializer der;
				der.reset(bbP);

				Block::BodyBase bbb;
				TxVectors::Perishable txvp;
				der & bbb;
				der & txvp;

				for (size_t j = 0; j < txvp.m_vOutputs.size(); j++)
				{
					Output& outp = *txvp.m_vOutputs[j];
					if (outp.m_pConfidential || outp.m_pPublic)
					{
						outp.m_pConfidential.reset();
						outp.m_pPublic.reset();
						hTampered = h;
						break;
					}
				}

				if (hTampered)
				{
					Serializer ser;
					ser & bbb;
					ser & txvp;
					ser.swap_buf(bbP);
				}
			}

			Block::SystemState::ID id;
			blockChain[h - 1]->m_Hdr.get_ID(id);
			verify_test(np.OnBlock(id, bbP, bbE, pid) == NodeProcessor::DataStatus::Accepted);

			bool bLast = (h == np.m_SyncData.m_Target.m_Height);

			np.TryGoUp();

			if (bLast)
			{
				verify_test(np.m_Cursor.m_ID.m_Height == hTampered - 1);
				break;
			}

			verify_test(np.m_Cursor.m_ID.m_Height == h);
		}

		// 4th attempt. provide valid data
		for (Height h = np.m_Cursor.m_ID.m_Height + 1; h <= blockChain.size(); h++)
		{
			NodeDB::StateID sid;
			sid.m_Row = npSrc.FindActiveAtStrict(h);
			sid.m_Height = h;

			ByteBuffer bbE, bbP;
			verify_test(npSrc.GetBlock(sid, &bbE, &bbP, 0, np.m_SyncData.m_TxoLo, np.m_SyncData.m_Target.m_Height, true));

			Block::SystemState::ID id;
			blockChain[h - 1]->m_Hdr.get_ID(id);
			np.OnBlock(id, bbP, bbE, pid);
		}

		np.TryGoUp();
		verify_test(!np.IsFastSync());
		verify_test(np.m_Cursor.m_ID.m_Height == blockChain.size());
	}

	const uint16_t g_Port = 25003; // don't use the default port to prevent collisions with running nodes, beacons and etc.

	void TestNodeConversation()
	{
		// Testing configuration: Node0 <-> Node1 <-> Client.

		io::Reactor::Ptr pReactor(io::Reactor::create());
		io::Reactor::Scope scope(*pReactor);

		Node node, node2;
		node.m_Cfg.m_sPathLocal = g_sz;
		node.m_Cfg.m_Listen.port(g_Port);
		node.m_Cfg.m_Listen.ip(INADDR_ANY);
		node.m_Cfg.m_Treasury = g_Treasury;

		node.m_Cfg.m_Timeout.m_GetBlock_ms = 1000 * 60;
		node.m_Cfg.m_Timeout.m_GetState_ms = 1000 * 60;

		node2.m_Cfg.m_sPathLocal = g_sz2;
		node2.m_Cfg.m_Listen.port(g_Port + 1);
		node2.m_Cfg.m_Listen.ip(INADDR_ANY);
		node2.m_Cfg.m_Timeout = node.m_Cfg.m_Timeout;
		node2.m_Cfg.m_Treasury = g_Treasury;

		node2.m_Cfg.m_BeaconPort = g_Port;

		ECC::SetRandom(node);
		ECC::SetRandom(node2);

		node.Initialize();
		node2.Initialize();

		struct MyClient
			:public proto::NodeConnection
		{
			Node* m_ppNode[2];
			unsigned int m_iNode;
			unsigned int m_WaitingCycles;

			Height m_HeightMax;
			const Height m_HeightTrg = 70;

			MyClient()
			{
				m_pTimer = io::Timer::create(io::Reactor::get_Current());
			}

			virtual void OnConnectedSecure() override {
				OnTimer();
			}

			virtual void OnDisconnect(const DisconnectReason&) override {
				fail_test("OnDisconnect");
			}

			io::Timer::Ptr m_pTimer;

			void OnTimer() {


				if (m_HeightMax < m_HeightTrg)
				{
					Node& n = *m_ppNode[m_iNode];

					TxPool::Fluff txPool; // empty, no transactions
					NodeProcessor::BlockContext bc(txPool, 0, *n.m_Keys.m_pMiner, *n.m_Keys.m_pMiner);

					verify_test(n.get_Processor().GenerateNewBlock(bc));

					n.get_Processor().OnState(bc.m_Hdr, PeerID());

					Block::SystemState::ID id;
					bc.m_Hdr.get_ID(id);

					n.get_Processor().OnBlock(id, bc.m_BodyP, bc.m_BodyE, PeerID());
					n.get_Processor().TryGoUp();

					std::setmax(m_HeightMax, bc.m_Hdr.m_Height);

					printf("Mined block Height = %u, node = %u \n", (unsigned int) bc.m_Hdr.m_Height, (unsigned int)m_iNode);

					++m_iNode %= _countof(m_ppNode);
				}
				else
					if (m_WaitingCycles++ > 60)
					{
						fail_test("Blockchain height didn't reach target");
						io::Reactor::get_Current().stop();
					}

				SetTimer(100);
			}

			virtual void OnMsg(proto::NewTip&& msg) override
			{
				printf("Tip Height=%u\n", (unsigned int) msg.m_Description.m_Height);
				verify_test(msg.m_Description.m_Height <= m_HeightMax);
				if (msg.m_Description.m_Height == m_HeightTrg)
					io::Reactor::get_Current().stop();
			}

			void SetTimer(uint32_t timeout_ms) {
				m_pTimer->start(timeout_ms, false, [this]() { return (this->OnTimer)(); });
			}
		};

		MyClient cl;
		cl.m_iNode = 0;
		cl.m_HeightMax = 0;
		cl.m_WaitingCycles = 0;
		cl.m_ppNode[0] = &node;
		cl.m_ppNode[1] = &node2;

		io::Address addr;
		addr.resolve("127.0.0.1");
		addr.port(g_Port + 1);

		cl.Connect(addr);


		pReactor->run();

		node.GenerateRecoveryInfo(g_sz3);

		struct MyParser :public RecoveryInfo::IParser
		{
			Key::IPKdf::Ptr m_pOwner1;
			Key::IPKdf::Ptr m_pOwner2;
			uint32_t m_nUnrecognized = 0;

			virtual bool OnUtxo(Height h, const Output& outp) override
			{
				CoinID cid;
				bool b1 = outp.Recover(h, *m_pOwner1, cid);
				bool b2 = outp.Recover(h, *m_pOwner2, cid);
				if (!(b1 || b2))
				{
					verify_test(!h); // treasury
					m_nUnrecognized++;
					verify_test(m_nUnrecognized <= 1);
				}

				return true;
			}
		} parser;
		parser.m_pOwner1 = node.m_Keys.m_pOwner;
		parser.m_pOwner2 = node2.m_Keys.m_pOwner;

		verify_test(parser.Proceed(g_sz3));

		DeleteFile(g_sz3);
	}

	namespace bvm2
	{
		void Compile(ByteBuffer& res, const char* sz, Processor::Kind kind)
		{
			std::FStream fs;
			fs.Open(sz, true, true);

			res.resize(static_cast<size_t>(fs.get_Remaining()));
			if (!res.empty())
				fs.read(&res.front(), res.size());

			bvm2::Processor::Compile(res, res, kind);
		}

	}



	void TestNodeClientProto()
	{
		// Testing configuration: Node <-> Client. Node is a miner

		io::Reactor::Ptr pReactor(io::Reactor::create());
		io::Reactor::Scope scope(*pReactor);

		Node node;
		node.m_Cfg.m_sPathLocal = g_sz;
		node.m_Cfg.m_Listen.port(g_Port);
		node.m_Cfg.m_Listen.ip(INADDR_ANY);
		node.m_Cfg.m_TestMode.m_FakePowSolveTime_ms = 100;
		node.m_Cfg.m_MiningThreads = 1;

		ECC::SetRandom(node);

		node.m_Cfg.m_Horizon.m_Branching = 6;
		node.m_Cfg.m_Horizon.m_Sync.Hi = 10;
		node.m_Cfg.m_Horizon.m_Sync.Lo = 14;
		//node.m_Cfg.m_Horizon.m_Local = node.m_Cfg.m_Horizon.m_Sync;
		node.m_Cfg.m_VerificationThreads = -1;

		node.m_Cfg.m_Dandelion.m_AggregationTime_ms = 0;
		node.m_Cfg.m_Dandelion.m_OutputsMin = 3;
		node.m_Cfg.m_Dandelion.m_DummyLifetimeLo = 5;
		node.m_Cfg.m_Dandelion.m_DummyLifetimeHi = 10;

		struct MyClient
			:public proto::NodeConnection
		{
			const Height m_HeightTrg = 75;

			MiniWallet m_Wallet;
			NodeProcessor* m_pProc = nullptr;

			std::vector<Block::SystemState::Full> m_vStates;

			std::set<ECC::Point> m_UtxosBeingSpent;
			std::list<ECC::Point> m_queProofsExpected;
			std::list<uint32_t> m_queProofsStateExpected;
			std::list<uint32_t> m_queProofsKrnExpected;
			uint32_t m_nChainWorkProofsPending = 0;
			uint32_t m_nBbsMsgsPending = 0;
			uint32_t m_nRecoveryPending = 0;
			std::list<std::pair<Height, Merkle::Hash> > m_queProofLogsExpected;

			struct
			{
				Height m_hCreated = 0;
				bool m_Emitted = false;
				Asset::Metadata m_Metadata;
				PeerID m_Owner;
				Asset::ID m_ID = 0; // set after successful creation + proof
				bool m_Recognized = false;
				bool m_ListReceived = false;
				bool m_EvtCreated = false;
				bool m_EvtEmitted = false;

			} m_Assets;

			struct
			{
				Height m_pStage[9]; // ctor, list, deposit, proof, print, withdraw, print, dtor, logs
				uint32_t m_Done = 0;
				bvm2::ContractID m_Cid;
				bool m_VarProof = false;

			} m_Contract;

			Height m_hEvts = 0;
			bool m_bEvtsPending = false;

			MyClient(const Key::IKdf::Ptr& pKdf)
			{
				m_Wallet.m_pKdf = pKdf;
				m_Wallet.m_AutoAddTxOutputs = false;
				m_pTimer = io::Timer::create(io::Reactor::get_Current());
			}

			virtual void OnConnectedSecure() override
			{
				SetTimer(90 * 1000);
				SendLogin();

				Send(proto::GetExternalAddr());
			}

			virtual void OnMsg(proto::Authentication&& msg) override
			{
				proto::NodeConnection::OnMsg(std::move(msg));

				switch (msg.m_IDType)
				{
				case proto::IDType::Node:
					ProveKdfObscured(*m_Wallet.m_pKdf, proto::IDType::Owner);
					break;

				case proto::IDType::Viewer:
					verify_test(IsPKdfObscured(*m_Wallet.m_pKdf, msg.m_ID));
					break;

				default: // suppress warning
					break;
				}
			}

			virtual void OnMsg(proto::ExternalAddr&& msg) override
			{
			}

			virtual void OnDisconnect(const DisconnectReason&) override {
				fail_test("OnDisconnect");
				io::Reactor::get_Current().stop();
			}

			io::Timer::Ptr m_pTimer;

			bool IsHeightReached() const
			{
				return !m_vStates.empty() && (m_vStates.back().m_Height >= m_HeightTrg);
			}

			bool IsAllProofsReceived() const
			{
				return
					m_queProofsExpected.empty() &&
					m_queProofsKrnExpected.empty() &&
					m_queProofsStateExpected.empty() &&
					m_queProofLogsExpected.empty() &&
					!m_nChainWorkProofsPending;
			}

			bool IsAllBbsReceived() const
			{
				return !m_nBbsMsgsPending;
			}

			bool IsAllRecoveryReceived() const
			{
				return !m_nRecoveryPending;
			}

			void OnTimer() {

				io::Reactor::get_Current().stop();
			}

			void OnBeingSpent(const proto::NewTransaction& msg)
			{
				for (size_t i = 0; i < msg.m_Transaction->m_vInputs.size(); i++)
					m_UtxosBeingSpent.insert(msg.m_Transaction->m_vInputs[i]->m_Commitment);
			}

			struct Shielded
			{
				Height m_Sent = 0;
				bool m_Withdrew = false;
				TxoID m_Confirmed = MaxHeight;
				TxoID m_Wnd0 = 0;
				uint32_t m_N;

				Lelantus::Cfg m_Cfg;

				ShieldedTxo::Data::Params m_Params;

				ECC::Scalar::Native m_skSpendKey;
				ECC::Point m_SerialPub;

				ECC::Hash::Value m_SpendKernelID;
				bool m_SpendConfirmed = false;
				bool m_EvtAdd = false;
				bool m_EvtSpend = false;

			} m_Shielded;


			bool SendShielded()
			{
				Height h = m_vStates.back().m_Height;
				if (h + 1 < Rules::get().pForks[2].m_Height + 3)
					return false;

				proto::NewTransaction msgTx;

				ShieldedTxo::Data::Params& sdp = m_Shielded.m_Params; // alias

				sdp.m_Output.m_Value = m_Wallet.MakeTxInput(msgTx.m_Transaction, h);
				if (!sdp.m_Output.m_Value)
					return false;

				auto& fs = Transaction::FeeSettings::get(h + 1);
				Amount fee = fs.get_DefaultStd() + fs.m_ShieldedOutputTotal;

				sdp.m_Output.m_Value -= fee;

				m_Shielded.m_Cfg = Rules::get().Shielded.m_ProofMax;

				assert(msgTx.m_Transaction);

				{
					TxKernelShieldedOutput::Ptr pKrn(new TxKernelShieldedOutput);
					pKrn->m_Height.m_Min = h + 1;
					pKrn->m_Fee = fee;

					ShieldedTxo::Voucher voucher;
					{
						// create a voucher
						ShieldedTxo::Viewer viewer;
						viewer.FromOwner(*m_Wallet.m_pKdf, 0);

						ShieldedTxo::Data::TicketParams sp;
						sp.Generate(voucher.m_Ticket, viewer, 13U);

						voucher.m_SharedSecret = sp.m_SharedSecret;

						m_Shielded.m_Params.m_Ticket = sp; // save just for later verification.

						// skip the voucher signature
					}

					pKrn->UpdateMsg();
					ECC::Oracle oracle;
					oracle << pKrn->m_Msg;

					// substitute the voucher
					pKrn->m_Txo.m_Ticket = voucher.m_Ticket;
					sdp.m_Ticket.m_SharedSecret = voucher.m_SharedSecret;

					ZeroObject(sdp.m_Output.m_User);
					sdp.m_Output.m_User.m_Sender = 165U;
					sdp.m_Output.m_User.m_pMessage[0] = 243U;
					sdp.m_Output.m_User.m_pMessage[1] = 2435U;
					sdp.GenerateOutp(pKrn->m_Txo, h + 1, oracle, true); // generate asset proof, though it's not CA

					pKrn->MsgToID();

					m_Shielded.m_SerialPub = pKrn->m_Txo.m_Ticket.m_SerialPub;

					Key::IKdf::Ptr pSerPrivate;
					ShieldedTxo::Viewer::GenerateSerPrivate(pSerPrivate, *m_Wallet.m_pKdf, 0);
					pSerPrivate->DeriveKey(m_Shielded.m_skSpendKey, sdp.m_Ticket.m_SerialPreimage);

					ECC::Point::Native pt;
					verify_test(pKrn->IsValid(h + 1, pt));

					msgTx.m_Transaction->m_vKernels.push_back(std::move(pKrn));
					m_Wallet.UpdateOffset(*msgTx.m_Transaction, sdp.m_Output.m_k, true);
				}

				msgTx.m_Transaction->Normalize();

				Transaction::Context::Params pars;
				Transaction::Context ctx(pars);
				ctx.m_Height.m_Min = h + 1;
				bool isTxValid = msgTx.m_Transaction->IsValid(ctx);
				verify_test(isTxValid);

				msgTx.m_Fluff = true;
				OnBeingSpent(msgTx);
				Send(msgTx);

				printf("Created shielded output\n");

				return true;
			}

			virtual void OnMsg(proto::ShieldedList&& msg) override
			{
				verify_test(msg.m_Items.size() <= m_Shielded.m_N);

				TxoID nWnd1 = m_Shielded.m_Wnd0 + msg.m_Items.size();
				if (nWnd1 <= m_Shielded.m_Confirmed)
					return;

				proto::NewTransaction msgTx;
				msgTx.m_Transaction = std::make_shared<Transaction>();
				msgTx.m_Transaction->m_Offset = Zero;

				Height h = m_vStates.back().m_Height;

				TxKernelShieldedInput::Ptr pKrn(new TxKernelShieldedInput);
				pKrn->m_Height.m_Min = h + 1;
				pKrn->m_WindowEnd = nWnd1;
				pKrn->m_SpendProof.m_Cfg = m_Shielded.m_Cfg;

				Lelantus::CmListVec lst;

				assert(nWnd1 <= m_Shielded.m_Wnd0 + m_Shielded.m_N);
				if (nWnd1 == m_Shielded.m_Wnd0 + m_Shielded.m_N)
					lst.m_vec.swap(msg.m_Items);
				else
				{
					// zero-pad from left
					lst.m_vec.resize(m_Shielded.m_N);
					for (size_t i = 0; i < m_Shielded.m_N - msg.m_Items.size(); i++)
					{
						ECC::Point::Storage& v = lst.m_vec[i];
						v.m_X = Zero;
						v.m_Y = Zero;
					}
					std::copy(msg.m_Items.begin(), msg.m_Items.end(), lst.m_vec.end() - msg.m_Items.size());
				}

				Lelantus::Prover p(lst, pKrn->m_SpendProof);
				p.m_Witness.m_L = static_cast<uint32_t>(m_Shielded.m_N - m_Shielded.m_Confirmed) - 1;
				p.m_Witness.m_R = m_Shielded.m_Params.m_Ticket.m_pK[0] + m_Shielded.m_Params.m_Output.m_k; // total blinding factor of the shielded element
				p.m_Witness.m_SpendSk = m_Shielded.m_skSpendKey;
				p.m_Witness.m_V = m_Shielded.m_Params.m_Output.m_Value;

				pKrn->UpdateMsg();

				ECC::SetRandom(p.m_Witness.m_R_Output);

				pKrn->m_NotSerialized.m_hvShieldedState = msg.m_State1;
				pKrn->Sign(p, 0, true); // hide asset, although it's beam

				verify_test(m_Shielded.m_Params.m_Ticket.m_SpendPk == pKrn->m_SpendProof.m_SpendPk);

				auto& fs = Transaction::FeeSettings::get(h + 1);
				Amount fee = fs.get_DefaultStd() + fs.m_ShieldedInputTotal;

				msgTx.m_Transaction->m_vKernels.push_back(std::move(pKrn));
				m_Wallet.UpdateOffset(*msgTx.m_Transaction, p.m_Witness.m_R_Output, false);

				m_Wallet.MakeTxOutput(*msgTx.m_Transaction, h, 0, m_Shielded.m_Params.m_Output.m_Value, fee);

				Transaction::Context::Params pars;
				Transaction::Context ctx(pars);
				ctx.m_Height.m_Min = h + 1;
				verify_test(msgTx.m_Transaction->IsValid(ctx));

				for (size_t i = 0; i < msgTx.m_Transaction->m_vKernels.size(); i++)
				{
					const TxKernel& krn = *msgTx.m_Transaction->m_vKernels[i];
					if (krn.get_Subtype() == TxKernel::Subtype::Std)
						m_Shielded.m_SpendKernelID = krn.m_Internal.m_ID;
				}

				msgTx.m_Fluff = true;
				OnBeingSpent(msgTx);
				Send(msgTx);
			}

			virtual void OnMsg(proto::ProofShieldedOutp&& msg) override
			{
				if (msg.m_Proof.empty())
					return;

				printf("Shielded output confirmed\n");

				ShieldedTxo::DescriptionOutp d;
				d.m_ID = msg.m_ID;
				d.m_Height = msg.m_Height;
				d.m_SerialPub = m_Shielded.m_SerialPub;
				d.m_Commitment = msg.m_Commitment;

				verify_test(m_vStates.back().IsValidProofShieldedOutp(d, msg.m_Proof));
				m_Shielded.m_Confirmed = msg.m_ID;

				m_Shielded.m_N = m_Shielded.m_Cfg.get_N();

				m_Shielded.m_Wnd0 = 0; // TODO - randomize m_Wnd0

				proto::GetShieldedList msgOut;
				msgOut.m_Id0 = m_Shielded.m_Wnd0;
				msgOut.m_Count = m_Shielded.m_N;
				Send(msgOut);
			}

			virtual void OnMsg(proto::ProofShieldedInp&& msg) override
			{
				if (msg.m_Proof.empty())
					return;

				printf("Shielded input confirmed\n");

				ShieldedTxo::DescriptionInp d;
				d.m_Height = msg.m_Height;
				d.m_SpendPk = m_Shielded.m_Params.m_Ticket.m_SpendPk;

				verify_test(m_vStates.back().IsValidProofShieldedInp(d, msg.m_Proof));
			}

			virtual void OnMsg(proto::ProofAsset&& msg) override
			{
				verify_test(m_Assets.m_hCreated && !m_Assets.m_ID);

				if (msg.m_Proof.empty())
					return;

				verify_test(msg.m_Info.m_Metadata.m_Value == m_Assets.m_Metadata.m_Value);
				verify_test(msg.m_Info.m_Metadata.m_Hash == m_Assets.m_Metadata.m_Hash);

				verify_test(m_vStates.back().IsValidProofAsset(msg.m_Info, msg.m_Proof));

				m_Assets.m_ID = msg.m_Info.m_ID;
				verify_test(m_Assets.m_ID);
			}

			virtual void OnMsg(proto::AssetsListAt&& msg) override
			{
				verify_test(m_Assets.m_hCreated);

				if (msg.m_Assets.empty())
					return;

				verify_test(1 == msg.m_Assets.size());
				auto& ai = msg.m_Assets.front();

				verify_test(m_Assets.m_ID == ai.m_ID);
				verify_test(ai.m_Metadata.m_Value == m_Assets.m_Metadata.m_Value);
				verify_test(ai.m_Metadata.m_Hash == m_Assets.m_Metadata.m_Hash);

				m_Assets.m_ListReceived = true;
			}

			struct AchievementTester
			{
				bool m_AllDone = true;
				const bool m_Fin;

				AchievementTester(bool bFin) :m_Fin(bFin) {}

				void Test(bool bAchieved, const char* sz)
				{
					if (!bAchieved)
					{
						m_AllDone = false;
						if (m_Fin)
							fail_test(sz);
					}
				}
			};


			bool TestAllDone(bool bFin)
			{
				AchievementTester t(bFin);
				t.Test(IsHeightReached(), "Blockchain height didn't reach target");
				t.Test(IsAllProofsReceived(), "some proofs missing");
				t.Test(IsAllBbsReceived(), "some BBS messages missing");
				t.Test(IsAllRecoveryReceived(), "some recovery messages missing");
				t.Test(m_Assets.m_ID != 0, "CA not created");
				t.Test(m_Assets.m_Recognized, "CA output not recognized");
				t.Test(m_Assets.m_EvtCreated, "CA creation not recognized by node");
				t.Test(m_Assets.m_EvtEmitted, "CA emission not recognized by node");
				t.Test(m_Assets.m_ListReceived, "CA list not received");
				t.Test(m_Shielded.m_SpendConfirmed, "Shielded spend not confirmed");
				t.Test(m_Shielded.m_EvtAdd, "Shielded Add event didn't arrive");
				t.Test(m_Shielded.m_EvtSpend, "Shielded Spend event didn't arrive");
				t.Test(m_Contract.m_VarProof, "Contract variable proof not received");

				return t.m_AllDone;
			}

			virtual void OnMsg(proto::NewTip&& msg) override
			{
				if (!msg.m_Description.m_Height)
					return; // skip the treasury-received notification

				printf("Tip Height=%u\n", (unsigned int) msg.m_Description.m_Height);
				verify_test(m_vStates.size() + 1 == msg.m_Description.m_Height);

				m_vStates.push_back(msg.m_Description);

				if (IsHeightReached())
				{
					if (TestAllDone(false))
						io::Reactor::get_Current().stop();
					return;
				}

				if (!m_Shielded.m_Sent && SendShielded())
					m_Shielded.m_Sent = msg.m_Description.m_Height;

				if (!m_Shielded.m_Withdrew && m_Shielded.m_Sent && (msg.m_Description.m_Height - m_Shielded.m_Sent >= 5))
				{
					proto::GetProofShieldedOutp msgOut;
					msgOut.m_SerialPub = m_Shielded.m_SerialPub;
					Send(msgOut);

					printf("Waiting for shielded output proof...\n");

					m_Shielded.m_Withdrew = true;
				}

				if (m_Assets.m_hCreated && (msg.m_Description.m_Height == m_Assets.m_hCreated + 3))
				{
					proto::GetProofAsset msgOut;
					msgOut.m_Owner = m_Assets.m_Owner;
					Send(msgOut);

					proto::GetAssetsListAt msgOut2;
					msgOut2.m_Height = MaxHeight;
					Send(msgOut2);
				}

				proto::BbsMsg msgBbs;
				msgBbs.m_Channel = 11;
				msgBbs.m_TimePosted = getTimestamp();
				msgBbs.m_Message.resize(1);
				msgBbs.m_Message[0] = (uint8_t) msg.m_Description.m_Height;
				Send(msgBbs);

				m_nBbsMsgsPending++;

				for (size_t i = 0; i + 1 < m_vStates.size(); i++)
				{
					proto::GetProofState msgOut2;
					msgOut2.m_Height = i + Rules::HeightGenesis;
					Send(msgOut2);

					m_queProofsStateExpected.push_back((uint32_t) i);
				}

				if (m_vStates.size() > 1)
				{
					proto::GetCommonState msgOut2;
					msgOut2.m_IDs.resize(m_vStates.size() - 1);

					for (size_t i = 0; i < m_vStates.size() - 1; i++)
						m_vStates[m_vStates.size() - i - 2].get_ID(msgOut2.m_IDs[i]);

					Send(msgOut2);
				}

				for (auto it = m_Wallet.m_MyUtxos.begin(); m_Wallet.m_MyUtxos.end() != it; it++)
				{
					const MiniWallet::MyUtxo& utxo = it->second;

					proto::GetProofUtxo msgOut2;

					ECC::Scalar::Native sk;
					m_Wallet.ToCommtiment(utxo, msgOut2.m_Utxo, sk);

					if (m_UtxosBeingSpent.find(msgOut2.m_Utxo) == m_UtxosBeingSpent.end())
					{
						Send(msgOut2);
						m_queProofsExpected.push_back(msgOut2.m_Utxo);
					}
				}

				for (uint32_t i = 0; i < m_Wallet.m_MyKernels.size(); i++)
				{
					const MiniWallet::MyKernel mk = m_Wallet.m_MyKernels[i];

					TxKernelStd krn;
					mk.Export(krn);

					proto::GetProofKernel2 msgOut2;
					msgOut2.m_ID = krn.m_Internal.m_ID;
					msgOut2.m_Fetch = true;
					Send(msgOut2);

					m_queProofsKrnExpected.push_back(i);

					proto::GetProofKernel msgOut3;
					msgOut3.m_ID = krn.m_Internal.m_ID;
					Send(msgOut3);

					m_queProofsKrnExpected.push_back(i);
				}

				{
					proto::GetProofChainWork msgOut2;
					Send(msgOut2);
					m_nChainWorkProofsPending++;
				}

				proto::NewTransaction msgTx;
				msgTx.m_Fluff = true; // currently - DISABLE dandelion. In this test blocks are assembled fast, and there's a (small) lag between the 2 nodes
				// sometimes a stem node, which is slightly behind, receives a tx whose m_Height.m_Min is already larger.
				// TODO: re-enable Dandelion once we take care of this (put slightly lower kernel height, wait a little longer for inputs to spend, don't send txs near the forks).

				for (int i = 0; i < 2; i++) // don't send too many txs, it's too heavy for the test
				{
					Amount val = m_Wallet.MakeTxInput(msgTx.m_Transaction, msg.m_Description.m_Height);
					if (!val)
						break;

					assert(msgTx.m_Transaction);

					MaybeCreateAsset(msgTx, val);
					MaybeEmitAsset(msgTx, val);
					MaybeInvokeContract(msgTx, val);

					m_Wallet.MakeTxOutput(*msgTx.m_Transaction, msg.m_Description.m_Height, 2, val);

					Transaction::Context::Params pars;
					Transaction::Context ctx(pars);
					ctx.m_Height.m_Min = msg.m_Description.m_Height + 1;
					verify_test(msgTx.m_Transaction->IsValid(ctx));

					OnBeingSpent(msgTx);
					Send(msgTx);
				}

				MaybeAskEvents();

				if (!(msg.m_Description.m_Height % 4))
				{
					// switch offline/online mining modes
					m_MiningFinalization = ((msg.m_Description.m_Height % 8) != 0);
					SendLogin();
				}

			}

			bool MaybeCreateAsset(proto::NewTransaction& msg, Amount& val)
			{
				if (m_Assets.m_hCreated)
					return false;

				const Block::SystemState::Full& s = m_vStates.back();
				if (s.m_Height + 1 < Rules::get().pForks[2].m_Height)
					return false;

				const Amount nFee = 330;
				Amount nLock = Rules::get().get_DepositForCA(s.m_Height + 1);
				if (val < nLock + nFee)
					return false;

				val -= nLock + nFee;

				ECC::Scalar::Native sk;
				ECC::SetRandom(sk);

				static const char szMyData[] = "My cool metadata!";
				m_Assets.m_Metadata.m_Value.resize(sizeof(szMyData) - 1);
				memcpy(&m_Assets.m_Metadata.m_Value.front(), szMyData, sizeof(szMyData) - 1);
				m_Assets.m_Metadata.UpdateHash();

				TxKernelAssetCreate::Ptr pKrn(new TxKernelAssetCreate);
				pKrn->m_Fee = nFee;
				pKrn->m_Height.m_Min = s.m_Height + 1;
				pKrn->m_MetaData = m_Assets.m_Metadata;
				pKrn->Sign(sk, *m_Wallet.m_pKdf);

				m_Assets.m_Owner = pKrn->m_Owner;

				msg.m_Transaction->m_vKernels.push_back(std::move(pKrn));
				m_Wallet.UpdateOffset(*msg.m_Transaction, sk, true);

				m_Assets.m_hCreated = s.m_Height;
				printf("Creating asset...\n");

				return true;
			}

			bool MaybeEmitAsset(proto::NewTransaction& msg, Amount& val)
			{
				if (m_Assets.m_Emitted)
					return false;

				const Amount nFee = 300;
				if (!m_Assets.m_ID || (val < nFee))
					return false;

				const Block::SystemState::Full& s = m_vStates.back();
				if (s.m_Height + 1 < Rules::get().pForks[2].m_Height)
					return false;

				val -= nFee;

				CoinID cid(Zero);
				cid.m_Value = 100500;
				cid.m_AssetID = m_Assets.m_ID;

				ECC::Scalar::Native sk, skOut;
				ECC::SetRandom(sk);

				TxKernelAssetEmit::Ptr pKrn(new TxKernelAssetEmit);
				pKrn->m_AssetID = m_Assets.m_ID;
				pKrn->m_Fee = nFee;
				pKrn->m_Value = cid.m_Value;
				pKrn->m_Height.m_Min = s.m_Height + 1;
				pKrn->Sign(sk, *m_Wallet.m_pKdf, m_Assets.m_Metadata);

				Output::Ptr pOutp(new Output);
				pOutp->Create(s.m_Height + 1, skOut, *m_Wallet.m_pKdf, cid, *m_Wallet.m_pKdf);

				msg.m_Transaction->m_vOutputs.push_back(std::move(pOutp));
				m_Wallet.UpdateOffset(*msg.m_Transaction, skOut, true);

				msg.m_Transaction->m_vKernels.push_back(std::move(pKrn));
				m_Wallet.UpdateOffset(*msg.m_Transaction, sk, true);

				m_Assets.m_Emitted = true;
				printf("Emitting asset...\n");

				return true;
			}

			struct MyManager
				:public bvm2::ManagerStd
			{
				MyClient& m_This;

				bool m_Done = false;
				bool m_Err = false;

				MyManager(MyClient& me)
					:m_This(me)
				{
				}

				void OnDone(const std::exception* pExc) override
				{
					m_Done = true;
					m_Err = !!pExc;

					m_This.m_Contract.m_Done++;

					if (m_This.m_pMan)
					{
						if (!m_Err)
							printf("manager shader: %s\n", m_Out.str().c_str());

						//m_This.m_pMan.reset();
					}
				}

				struct DelayedStart
					:public io::IdleEvt
				{
					void OnSchedule() override
					{
						cancel();
						get_ParentObj().StartRun(1);
					}

					IMPLEMENT_GET_PARENT_OBJ(MyManager, m_DelayedStart)

				} m_DelayedStart;

				std::map<uint32_t, ECC::Hash::Value> m_Slots;

				bool SlotLoad(ECC::Hash::Value& hv, uint32_t iSlot) override
				{
					auto it = m_Slots.find(iSlot);
					if (m_Slots.end() == it)
						return false;

					hv = it->second;
					return true;
				}

				void SlotSave(const ECC::Hash::Value& hv, uint32_t iSlot) override
				{
					m_Slots[iSlot] = hv;
				}

				void SlotErase(uint32_t iSlot) override
				{
					auto it = m_Slots.find(iSlot);
					if (m_Slots.end() != it)
						m_Slots.erase(it);
				}

				void SelectContext(bool /* bDependent */, uint32_t /* nChargeNeeded */) override
				{
					m_Context.m_Height = m_This.m_vStates.empty() ? 0 : m_This.m_vStates.back().m_Height;
				}

			};

			std::unique_ptr<MyManager> m_pMan;

			struct MyNetwork
				:public proto::FlyClient::INetwork
			{
				MyClient& m_This;
				MyNetwork(MyClient& me) :m_This(me) {}

				virtual void Connect() override {}
				virtual void Disconnect() override {}
				virtual void BbsSubscribe(BbsChannel, Timestamp, proto::FlyClient::IBbsReceiver*) override {}

				proto::FlyClient::Request::Ptr m_pReq;

				virtual void PostRequestInternal(proto::FlyClient::Request& r) override
				{
					switch (r.get_Type())
					{
					case proto::FlyClient::Request::Type::ContractVars:
						m_This.Send(Cast::Up<proto::FlyClient::RequestContractVars>(r).m_Msg);
						break;

					case proto::FlyClient::Request::Type::ContractLogs:
						m_This.Send(Cast::Up<proto::FlyClient::RequestContractLogs>(r).m_Msg);
						break;

					case proto::FlyClient::Request::Type::ContractVar:
						m_This.Send(Cast::Up<proto::FlyClient::RequestContractVar>(r).m_Msg);
						break;

					default:
						return;
					}

					m_pReq = &r;
				}

				void OnComplete2()
				{
					auto pReq = std::move(m_pReq);
					pReq->m_pTrg->OnComplete(*pReq);
				}

				void OnMsg(proto::ContractVars&& msg)
				{
					if (m_pReq && m_pReq->m_pTrg)
					{
						auto& x = Cast::Up<proto::FlyClient::RequestContractVars>(*m_pReq);
						x.m_Res = std::move(msg);
						OnComplete2();
					}
				}

				void OnMsg(proto::ContractLogs&& msg)
				{
					if (m_pReq && m_pReq->m_pTrg)
					{
						auto& x = Cast::Up<proto::FlyClient::RequestContractLogs>(*m_pReq);
						x.m_Res = std::move(msg);
						OnComplete2();
					}
				}

				void OnMsg(proto::ContractVar&& msg)
				{
					if (m_pReq && m_pReq->m_pTrg)
					{
						auto& x = Cast::Up<proto::FlyClient::RequestContractVar>(*m_pReq);
						x.m_Res = std::move(msg);
						OnComplete2();
					}
				}
			};

			std::shared_ptr<MyNetwork> m_pMyNetwork;

			bool MaybeInvokeContract(proto::NewTransaction& msg, Amount& val)
			{
				const Block::SystemState::Full& s = m_vStates.back();
				if (s.m_Height + 1 < Rules::get().pForks[3].m_Height)
					return false;

				if (m_pMan)
				{
					auto& proc = *m_pMan;
					if (!proc.m_Done)
						return false; // pending

					if (!proc.m_InvokeData.m_vec.empty())
					{
						bvm2::FundsMap fm;

						HeightRange hr;
						hr.m_Min = s.m_Height + 1;

						for (uint32_t i = 0; i < proc.m_InvokeData.m_vec.size(); i++)
						{
							const auto& cdata = proc.m_InvokeData.m_vec[i];
							fm += cdata.m_Spend;
							fm.AddSpend(0, cdata.get_FeeMin(hr.m_Min));

							if (!cdata.m_iMethod)
								bvm2::get_Cid(m_Contract.m_Cid, cdata.m_Data, cdata.m_Args);
						}

						AmountSigned valSpend = fm[0]; // including fees. Would be negative if we're receiving funds
						if (valSpend > static_cast<AmountSigned>(val))
							return false; // not enough funds

						for (uint32_t i = 0; i < proc.m_InvokeData.m_vec.size(); i++)
						{
							const auto& x = proc.m_InvokeData.m_vec[i];
							x.Generate(*msg.m_Transaction, *m_Wallet.m_pKdf, hr, x.get_FeeMin(hr.m_Min));
						}

						val -= valSpend;

						printf("Invoking contract, action=%s...\n", proc.m_Args["action"].c_str());
					}

					m_pMan.reset();
				}

				if (m_Contract.m_Done == _countof(m_Contract.m_pStage))
					return false;

				if (m_Contract.m_Done && (s.m_Height - m_Contract.m_pStage[m_Contract.m_Done - 1] < 4))
					return false;

				m_pMan = std::make_unique<MyManager>(*this);
				MyManager& proc = *m_pMan;

				proc.m_pPKdf = m_Wallet.m_pKdf;
				proc.m_pKdf = m_Wallet.m_pKdf; // enable privileged mode!

				bvm2::Compile(proc.m_BodyManager, "vault/app.wasm", bvm2::Processor::Kind::Manager);
				bvm2::Compile(proc.m_BodyContract, "vault/contract.wasm", bvm2::Processor::Kind::Contract);

				if (!m_pMyNetwork)
					m_pMyNetwork = std::make_shared<MyNetwork>(*this);

				proc.m_pNetwork = m_pMyNetwork;

				if (m_Contract.m_Done)
					proc.set_ArgBlob("cid", m_Contract.m_Cid);

				switch (m_Contract.m_Done)
				{
				case 0: // ctor
					proc.m_Args["role"] = "manager";
					proc.m_Args["action"] = "create";
					break;

				case 1: // list
					proc.m_Args["role"] = "manager";
					proc.m_Args["action"] = "view";
					break;

				case 2: // deposit
					proc.m_Args["role"] = "my_account";
					proc.m_Args["action"] = "deposit";
					proc.m_Args["amount"] = "700000";
					break;

				case 4:
					proc.m_Args["role"] = "my_account";
					proc.m_Args["action"] = "get_proof";
					break;

				case 5: // withdraw
					proc.m_Args["role"] = "my_account";
					proc.m_Args["action"] = "withdraw";
					proc.m_Args["amount"] = "700000";
					break;

				case 7: // dtor
					proc.m_Args["role"] = "manager";
					proc.m_Args["action"] = "destroy";
					break;

				case 8: // logs
					proc.m_Args["role"] = "manager";
					proc.m_Args["action"] = "view_logs";

					{
						// proofs for logs
						NodeDB::ContractLog::Walker wlk;
						for (m_pProc->get_DB().ContractLogEnum(wlk, HeightPos(0), HeightPos(MaxHeight)); wlk.MoveNext(); )
						{
							proto::GetContractLogProof msgOut;
							msgOut.m_Pos = wlk.m_Entry.m_Pos;
							Send(msgOut);

							auto& x = m_queProofLogsExpected.emplace_back();
							x.first = wlk.m_Entry.m_Pos.m_Height;
							Block::get_HashContractLog(x.second, wlk.m_Entry.m_Key, wlk.m_Entry.m_Val, wlk.m_Entry.m_Pos.m_Pos);
						}
					}

					break;

				default: // print
					proc.m_Args["role"] = "my_account";
					proc.m_Args["action"] = "view";

				}

				m_Contract.m_pStage[m_Contract.m_Done] = s.m_Height;

				printf("manager shader, action=%s...\n", proc.m_Args["action"].c_str());
				proc.m_DelayedStart.start();


				return true;
			}

			virtual void OnMsg(proto::ContractVars&& msg) override
			{
				if (m_pMyNetwork)
					m_pMyNetwork->OnMsg(std::move(msg));
			}

			virtual void OnMsg(proto::ContractLogs&& msg) override
			{
				if (m_pMyNetwork)
					m_pMyNetwork->OnMsg(std::move(msg));
			}

			virtual void OnMsg(proto::ContractVar&& msg) override
			{
				if (m_pMyNetwork)
				{
					if (!msg.m_Proof.empty() && m_pMyNetwork->m_pReq && (proto::FlyClient::Request::ContractVar == m_pMyNetwork->m_pReq->get_Type()))
					{
						auto& r = Cast::Up<proto::FlyClient::RequestContractVar>(*m_pMyNetwork->m_pReq);
						verify_test(m_vStates.back().IsValidProofContract(r.m_Msg.m_Key, msg.m_Value, msg.m_Proof));

						m_Contract.m_VarProof = true;
					}

					m_pMyNetwork->OnMsg(std::move(msg));
				}
			}

			virtual void OnMsg(proto::ContractLogProof&& msg) override
			{
				verify_test(!m_queProofLogsExpected.empty());
				auto& x = m_queProofLogsExpected.front();

				const auto& s = m_vStates[x.first - Rules::HeightGenesis];
				verify_test(s.IsValidProofLog(x.second, msg.m_Proof));

				m_queProofLogsExpected.pop_front();
			}


			void MaybeAskEvents()
			{
				if (m_bEvtsPending || m_vStates.empty())
					return;

				assert(m_hEvts <= m_vStates.back().m_Height);
				if (m_hEvts == m_vStates.back().m_Height)
					return;
				
				proto::GetEvents msg;
				msg.m_HeightMin = m_hEvts + 1;
				Send(msg);

				m_bEvtsPending = true;
				m_nRecoveryPending++;
			}

			bool m_MiningFinalization = false;

			virtual void SetupLogin(proto::Login& msg) override
			{
				if (m_MiningFinalization)
					msg.m_Flags |= proto::LoginFlags::MiningFinalization;
			}

			virtual void OnMsg(proto::ProofState&& msg) override
			{
				if (!m_queProofsStateExpected.empty())
				{
					const Block::SystemState::Full& s = m_vStates[m_queProofsStateExpected.front()];
					Block::SystemState::ID id;
					s.get_ID(id);

					verify_test(m_vStates.back().IsValidProofState(id, msg.m_Proof));

					m_queProofsStateExpected.pop_front();
				}
				else
					fail_test("unexpected proof");
			}

			virtual void OnMsg(proto::ProofCommonState&& msg) override
			{
				verify_test(!m_vStates.empty());
				verify_test(m_vStates.back().IsValidProofState(msg.m_ID, msg.m_Proof));
			}

			virtual void OnMsg(proto::ProofUtxo&& msg) override
			{
				if (!m_queProofsExpected.empty())
				{
					const ECC::Point& comm = m_queProofsExpected.front();

					verify_test(!msg.m_Proofs.empty());

					for (uint32_t j = 0; j < msg.m_Proofs.size(); j++)
						verify_test(m_vStates.back().IsValidProofUtxo(comm, msg.m_Proofs[j]));

					m_queProofsExpected.pop_front();
				}
				else
					fail_test("unexpected proof");
			}

			virtual void OnMsg(proto::ProofKernel2&& msg) override
			{
				if (!m_queProofsKrnExpected.empty())
				{
					m_queProofsKrnExpected.pop_front();

					if (!msg.m_Proof.empty())
					{
						verify_test(msg.m_Kernel);

						ECC::Point::Native exc;
						verify_test(msg.m_Kernel->IsValid(msg.m_Height, exc));

						verify_test(msg.m_Height <= m_vStates.size());
						const Block::SystemState::Full& s = m_vStates[msg.m_Height - 1];
						verify_test(s.m_Height == msg.m_Height);

						verify_test(s.IsValidProofKernel(msg.m_Kernel->m_Internal.m_ID, msg.m_Proof));
					}
				}
				else
					fail_test("unexpected proof");
			}

			virtual void OnMsg(proto::ProofKernel&& msg) override
			{
				if (!m_queProofsKrnExpected.empty())
				{
					const MiniWallet::MyKernel& mk = m_Wallet.m_MyKernels[m_queProofsKrnExpected.front()];
					m_queProofsKrnExpected.pop_front();

					if (!msg.m_Proof.empty())
					{
						TxKernelStd krn;
						mk.Export(krn);
						verify_test(m_vStates.back().IsValidProofKernel(krn, msg.m_Proof));

						if (!m_Shielded.m_SpendConfirmed && (krn.m_Internal.m_ID == m_Shielded.m_SpendKernelID))
						{
							m_Shielded.m_SpendConfirmed = true;

							proto::GetProofShieldedInp msgOut;
							msgOut.m_SpendPk = m_Shielded.m_Params.m_Ticket.m_SpendPk;
							Send(msgOut);

							printf("Waiting for shielded input proof...\n");

						}
					}
				}
				else
					fail_test("unexpected proof");
			}

			virtual void OnMsg(proto::ProofChainWork&& msg) override
			{
				verify_test(m_nChainWorkProofsPending);
				verify_test(!m_vStates.empty() && (msg.m_Proof.m_Heading.m_Prefix.m_Height + msg.m_Proof.m_Heading.m_vElements.size() - 1 == m_vStates.back().m_Height));
				verify_test(msg.m_Proof.IsValid());
				m_nChainWorkProofsPending--;
			}

			virtual void OnMsg(proto::Events&& msg) override
			{
				verify_test(m_nRecoveryPending);
				m_nRecoveryPending--;

				verify_test(m_bEvtsPending);
				m_bEvtsPending = false;

				Height hTip = m_vStates.empty() ? 0 : m_vStates.back().m_Height;

				struct MyParser :public proto::Event::IGroupParser
				{
					MyClient& m_This;
					MyParser(MyClient& x) :m_This(x) {}

					virtual void OnEventBase(proto::Event::Base& evt) override
					{
						// log non-UTXO events
						std::ostringstream os;
						os << "Evt H=" << m_Height << ", ";
						evt.Dump(os);
						printf("%s\n", os.str().c_str());
					}

					virtual void OnEventType(proto::Event::Utxo& evt) override
					{
						ECC::Scalar::Native sk;
						ECC::Point comm;
						CoinID::Worker(evt.m_Cid).Create(sk, comm, *m_This.m_Wallet.m_pKdf);
						verify_test(comm == evt.m_Commitment);

						if (evt.m_Cid.m_AssetID)
						{
							verify_test(evt.m_Cid.m_AssetID == m_This.m_Assets.m_ID);
							if (!m_This.m_Assets.m_Recognized)
							{
								m_This.m_Assets.m_Recognized = true;
								printf("Asset UTXO recognized\n");
							}
						}
						else
						{
							if (proto::Event::Flags::Add & evt.m_Flags)
								m_This.m_Wallet.AddMyUtxo(evt.m_Cid, evt.m_Maturity);
						}
					}

					virtual void OnEventType(proto::Event::Shielded& evt) override
					{
						OnEventBase(evt);

						// Restore all the relevent data
						verify_test(evt.m_TxoID == 0);

						// Output parameters are fully recovered
						verify_test(!memcmp(&m_This.m_Shielded.m_Params.m_Output.m_User, &evt.m_CoinID.m_User, sizeof(evt.m_CoinID.m_User)));
						verify_test(m_This.m_Shielded.m_Params.m_Output.m_Value == evt.m_CoinID.m_Value);
						verify_test(m_This.m_Shielded.m_Params.m_Output.m_AssetID == evt.m_CoinID.m_AssetID);
						

						// Shielded parameters: recovered only the part that is sufficient to spend it
						ShieldedTxo::Viewer viewer;
						viewer.FromOwner(*m_This.m_Wallet.m_pKdf, evt.m_CoinID.m_Key.m_nIdx);

						ShieldedTxo::Data::TicketParams sp;
						sp.m_pK[0] = evt.m_CoinID.m_Key.m_kSerG;
						sp.m_IsCreatedByViewer = evt.m_CoinID.m_Key.m_IsCreatedByViewer;
						sp.Restore(viewer); // restores only what is necessary for spend

						verify_test(m_This.m_Shielded.m_Params.m_Ticket.m_IsCreatedByViewer == sp.m_IsCreatedByViewer);
						verify_test(m_This.m_Shielded.m_Params.m_Ticket.m_pK[0] == sp.m_pK[0]);
						verify_test(m_This.m_Shielded.m_Params.m_Ticket.m_SerialPreimage == sp.m_SerialPreimage);

						// Recover the full data
						ShieldedTxo::Data::OutputParams op;
						op.m_Value = evt.m_CoinID.m_Value;
						op.m_AssetID = evt.m_CoinID.m_AssetID;
						op.m_User = evt.m_CoinID.m_User;
						op.Restore_kG(sp.m_SharedSecret);

						verify_test(m_This.m_Shielded.m_Params.m_Output.m_k == op.m_k);

						if (proto::Event::Flags::Add & evt.m_Flags)
							m_This.m_Shielded.m_EvtAdd = true;
						else
							m_This.m_Shielded.m_EvtSpend = true;
					}

					virtual void OnEventType(proto::Event::AssetCtl& evt) override
					{
						OnEventBase(evt);

						if (m_This.m_Assets.m_ID) {
							// creation event may come before the client got proof for its asset
							verify_test(evt.m_Info.m_ID == m_This.m_Assets.m_ID);
						}
						verify_test(evt.m_Info.m_Metadata.m_Value == m_This.m_Assets.m_Metadata.m_Value);
						verify_test(evt.m_Info.m_Owner == m_This.m_Assets.m_Owner);

						if (proto::Event::Flags::Add & evt.m_Flags)
						{
							verify_test(!m_This.m_Assets.m_EvtCreated);
							m_This.m_Assets.m_EvtCreated = true;
						}

						if (evt.m_EmissionChange)
							m_This.m_Assets.m_EvtEmitted = true;
					}

				} p(*this);

				uint32_t nCount = p.Proceed(msg.m_Events);

				m_hEvts = (nCount < proto::Event::s_Max) ? hTip : p.m_Height;

				MaybeAskEvents();

			}

			virtual void OnMsg(proto::GetBlockFinalization&& msg) override
			{
				Block::Builder bb(0, *m_Wallet.m_pKdf, *m_Wallet.m_pKdf, msg.m_Height);
				bb.AddCoinbaseAndKrn();
				bb.AddFees(msg.m_Fees);

				proto::BlockFinalization msgOut;
				msgOut.m_Value.reset(new Transaction);
				bb.m_Txv.MoveInto(*msgOut.m_Value);
				msgOut.m_Value->m_Offset = -bb.m_Offset;
				msgOut.m_Value->Normalize();

				Send(msgOut);
			}

			void SetTimer(uint32_t timeout_ms) {
				m_pTimer->start(timeout_ms, false, [this]() { return (this->OnTimer)(); });
			}
		};

		MyClient cl(node.m_Keys.m_pMiner);
		cl.m_pProc = &node.get_Processor();

		io::Address addr;
		addr.resolve("127.0.0.1");
		addr.port(g_Port);

		node.m_Cfg.m_Treasury = g_Treasury;

		ByteBuffer bufParser;
		bvm2::Compile(bufParser, "Explorer/Parser.wasm", bvm2::Processor::Kind::Manager);

		node.m_Cfg.m_ProcessorParams.m_RichInfoFlags =
			NodeProcessor::StartParams::RichInfo::On |
			NodeProcessor::StartParams::RichInfo::UpdShader;
		node.m_Cfg.m_ProcessorParams.m_RichParser = bufParser;

		node.Initialize();

		cl.Connect(addr);


		struct MyClient2
			:public proto::NodeConnection
		{
			MyClient* m_pOtherClient;

			virtual void OnConnectedSecure() override
			{
				SendLogin();
			}

			virtual void SetupLogin(proto::Login& msg) override
			{
				msg.m_Flags |= proto::LoginFlags::SendPeers;
			}

			virtual void OnDisconnect(const DisconnectReason&) override {
				fail_test("OnDisconnect");
			}

			virtual void OnMsg(proto::NewTip&& msg) override {
				if (msg.m_Description.m_Height == 10)
				{
					proto::BbsSubscribe msgOut;
					msgOut.m_Channel = 11;
					msgOut.m_On = true;

					Send(msgOut);
				}
			}

			uint32_t m_MsgCount = 0;

			virtual void OnMsg(proto::BbsMsg&& msg) override {
				OnBbsMsg(msg.m_Message);
			}

			void OnBbsMsg(const ByteBuffer& msg)
			{
				verify_test(msg.size() == 1);
				uint8_t nMsg = msg[0];

				verify_test(nMsg == (uint8_t) m_MsgCount + 1);
				m_MsgCount++;

				verify_test(m_pOtherClient->m_nBbsMsgsPending);
				m_pOtherClient->m_nBbsMsgsPending--;
			}
		};

		MyClient2 cl2;
		cl2.m_pOtherClient = &cl;
		cl2.Connect(addr);


		Node node2;
		node2.m_Cfg.m_sPathLocal = g_sz2;
		node2.m_Cfg.m_Connect.resize(1);
		node2.m_Cfg.m_Connect[0].resolve("127.0.0.1");
		node2.m_Cfg.m_Connect[0].port(g_Port);
		node2.m_Cfg.m_Timeout = node.m_Cfg.m_Timeout;

		node2.m_Cfg.m_Dandelion = node.m_Cfg.m_Dandelion;

		node2.m_Cfg.m_Horizon = node.m_Cfg.m_Horizon;
		node2.m_Cfg.m_Horizon.m_Local = node2.m_Cfg.m_Horizon.m_Sync;

		ECC::SetRandom(node2);
		node2.Initialize();
		verify_test(node2.get_AcessiblePeerCount() == 1);

		pReactor->run();

		cl.TestAllDone(true);

		struct TxoRecover
			:public NodeProcessor::ITxoRecover
		{
			uint32_t m_Recovered = 0;

			TxoRecover(Key::IPKdf& key) :NodeProcessor::ITxoRecover(key) {}

			virtual bool OnTxo(const NodeDB::WalkerTxo&, Height hCreate, Output&, const CoinID&, const Output::User&) override
			{
				m_Recovered++;
				return true;
			}
		};

		TxoRecover wlk(*node.m_Keys.m_pOwner);
		node2.get_Processor().EnumTxos(wlk);

		node.get_Processor().RescanOwnedTxos();

		verify_test(wlk.m_Recovered);

		// Test recovery info. Check if shielded in/outs and assets can re recognized
		node.GenerateRecoveryInfo(beam::g_sz3);

		struct MyParser
			:public beam::RecoveryInfo::IRecognizer
		{
			uint32_t m_Utxos = 0;
			uint32_t m_UtxosCA = 0;
			uint32_t m_Assets = 0;
			uint32_t m_ShieldedOuts = 0;
			uint32_t m_ShieldedIns = 0;

			typedef std::set<ECC::Point> PkSet;
			PkSet m_SpendKeys;

			virtual bool OnUtxoRecognized(Height, const Output&, CoinID& cid, const Output::User&) override
			{
				m_Utxos++;
				if (cid.m_AssetID)
					m_UtxosCA++;
				return true;
			}

			virtual bool OnShieldedOutRecognized(const ShieldedTxo::DescriptionOutp& dout, const ShieldedTxo::DataParams& pars, Key::Index) override
			{
				verify_test(m_SpendKeys.end() == m_SpendKeys.find(pars.m_Ticket.m_SpendPk));
				m_SpendKeys.insert(pars.m_Ticket.m_SpendPk);
				m_ShieldedOuts++;
				return true;
			}

			virtual bool OnShieldedIn(const ShieldedTxo::DescriptionInp& din) override
			{
				if (m_SpendKeys.end() != m_SpendKeys.find(din.m_SpendPk))
					m_ShieldedIns++;
				return true;
			}

			virtual bool OnAssetRecognized(Asset::Full&) override
			{
				m_Assets++;
				return true;
			}

		};

		MyParser p;
		p.Init(cl.m_Wallet.m_pKdf);
		p.Proceed(beam::g_sz3); // check we can rebuild the Live consistently with shielded and assets

		verify_test((p.m_SpendKeys.size() == 1) && p.m_Utxos && p.m_UtxosCA && p.m_Assets && p.m_ShieldedOuts && p.m_ShieldedIns);

		auto logger = beam::Logger::create(LOG_LEVEL_DEBUG, LOG_LEVEL_DEBUG);
		node.PrintTxos();

		NodeProcessor& proc = node.get_Processor();
		Height h0 = proc.m_Cursor.m_Full.m_Height;
		proc.ManualRollbackTo(h0 - 5);
		verify_test(proc.m_Cursor.m_ID.m_Height >= h0 - 5); // it can be adjusted up
		verify_test(proc.m_Cursor.m_Full.m_Height < h0); // some rollback with forbidden state update must take place
		verify_test(proc.m_ManualSelection.m_Forbidden);
	}


	void TestChainworkProof()
	{
		printf("Preparing blockchain ...\n");

		MiniBlockChain cc;
		cc.Generate(200000);

		const Block::SystemState::Full& sRoot = cc.m_vStates.back().m_Hdr;

		Block::ChainWorkProof cwp;
		cwp.m_hvRootLive = cc.m_hvLive;
		cwp.Create(cc.m_Source, sRoot);

		uint32_t nStates = (uint32_t) cc.m_vStates.size();
		for (size_t i0 = 0; ; i0++)
		{
			Block::SystemState::Full sTip;
			verify_test(cwp.IsValid(&sTip));
			verify_test(sRoot == sTip);

			printf("Blocks = %u. Proof: States = %u/%u, Hashes = %u, Size = %u\n",
				nStates,
				uint32_t(cwp.m_Heading.m_vElements.size()),
				uint32_t(cwp.m_vArbitraryStates.size()),
				uint32_t(cwp.m_Proof.m_vData.size()),
				uint32_t(sizeof(Block::SystemState::Sequence::Prefix) + cwp.m_Heading.m_vElements.size() * sizeof(Block::SystemState::Sequence::Element) + cwp.m_vArbitraryStates.size() * sizeof(Block::SystemState::Full) + cwp.m_Proof.m_vData.size() * sizeof(Merkle::Hash))
				);

			nStates >>= 1;
			if (nStates < 64)
				break;

			Block::ChainWorkProof cwp2;
			cwp2.m_LowerBound = cc.m_vStates[cc.m_vStates.size() - nStates].m_Hdr.m_ChainWork;
			verify_test(cwp2.Crop(cwp));

			cwp.m_LowerBound = cwp2.m_LowerBound;
			verify_test(cwp.Crop());

			verify_test(cwp2.IsValid(&sTip));
			verify_test(sRoot == sTip);
			verify_test(cwp.IsValid(&sTip));
			verify_test(sRoot == sTip);
		}
	}

	void RaiseHeightTo(Node& node, Height h)
	{
		while (node.get_Processor().m_Cursor.m_ID.m_Height < h)
		{
			NodeProcessor::BlockContext bc(node.m_TxPool, 0, *node.m_Keys.m_pMiner, *node.m_Keys.m_pMiner);
			bc.m_pParent = node.m_TxDependent.m_pBest;
			verify_test(node.get_Processor().GenerateNewBlock(bc));
			node.get_Processor().OnState(bc.m_Hdr, PeerID());

			Block::SystemState::ID id;
			bc.m_Hdr.get_ID(id);
			node.get_Processor().OnBlock(id, bc.m_BodyP, bc.m_BodyE, PeerID());
			node.get_Processor().TryGoUp();
		}
	}

	void TestFlyClient()
	{
		io::Reactor::Ptr pReactor(io::Reactor::create());
		io::Reactor::Scope scope(*pReactor);

		Node node;
		node.m_Cfg.m_sPathLocal = g_sz;
		node.m_Cfg.m_Listen.port(g_Port);
		node.m_Cfg.m_Listen.ip(INADDR_ANY);
		node.m_Cfg.m_MiningThreads = 0;
		node.m_Cfg.m_Treasury = g_Treasury;

		ECC::SetRandom(node);

		node.Initialize();

		struct MyFlyClient
			:public proto::FlyClient
			,public proto::FlyClient::Request::IHandler
			,public proto::FlyClient::IBbsReceiver
		{
			io::Timer::Ptr m_pTimer;

			bool m_bRunning;
			bool m_bTip;
			Height m_hRolledTo;
			uint32_t m_nProofsExpected;
			BbsChannel m_LastBbsChannel = 0;
			bool m_bBbsReceived;
			Block::SystemState::HistoryMap m_Hist;

			MyFlyClient()
			{
				m_pTimer = io::Timer::create(io::Reactor::get_Current());
			}

			virtual Block::SystemState::IHistory& get_History() override
			{
				return m_Hist;
			}

			virtual void OnNewTip() override
			{
				m_bTip = true;
				MaybeStop();
			}

			void MaybeStop()
			{
				if (m_bRunning && m_bTip && !m_nProofsExpected && m_bBbsReceived)
				{
					io::Reactor::get_Current().stop();
					m_bRunning = false;
				}
			}

			virtual void OnRolledBack() override
			{
				m_hRolledTo = m_Hist.m_Map.empty() ? 0 : m_Hist.m_Map.rbegin()->first;
			}

			void OnTimer() {
				io::Reactor::get_Current().stop();
			}

			void SetTimer(uint32_t timeout_ms) {
				m_pTimer->start(timeout_ms, false, [this]() { return (this->OnTimer)(); });
			}

			void KillTimer() {
				m_pTimer->cancel();
			}

			virtual void OnComplete(Request& r) override
			{
				verify_test(this == r.m_pTrg);
				verify_test(m_nProofsExpected);
				m_nProofsExpected--;
				MaybeStop();
			}

			virtual void OnMsg(proto::BbsMsg&&) override
			{
				m_bBbsReceived = true;
				MaybeStop();
			}

			void SyncSync() // synchronize synchronously. Joky joke.
			{
				m_bTip = false;
				m_hRolledTo = MaxHeight;
				m_nProofsExpected = 0;
				m_bBbsReceived = false;
				++m_LastBbsChannel;

				NetworkStd net(*this);

				io::Address addr;
				addr.resolve("127.0.0.1");
				addr.port(g_Port);
				net.m_Cfg.m_vNodes.resize(4, addr); // create several connections, let the compete

				net.Connect();

				// request several proofs
				for (uint32_t i = 0; i < 10; i++)
				{
					RequestUtxo::Ptr pUtxo(new RequestUtxo);
					net.PostRequest(*pUtxo, *this);

					if (1 & i)
						pUtxo->m_pTrg = NULL;
					else
						m_nProofsExpected++;

					RequestKernel::Ptr pKrnl(new RequestKernel);
					net.PostRequest(*pKrnl, *this);

					if (1 & i)
						pKrnl->m_pTrg = NULL;
					else
						m_nProofsExpected++;

					RequestBbsMsg::Ptr pBbs(new RequestBbsMsg);
					pBbs->m_Msg.m_Channel = m_LastBbsChannel;
					pBbs->m_Msg.m_TimePosted = getTimestamp();
					net.PostRequest(*pBbs, *this);
					m_nProofsExpected++;
				}

				net.BbsSubscribe(m_LastBbsChannel, 0, this);

				RequestEnumHdrs::Ptr pHdrs(new RequestEnumHdrs);
				pHdrs->m_Msg.m_Height.m_Min = 0;
				pHdrs->m_Msg.m_Height.m_Max = MaxHeight; // result should be truncated

				net.PostRequest(*pHdrs, *this);
				m_nProofsExpected++;

				{
					RequestAssetsListAt::Ptr pReq(new RequestAssetsListAt);
					pReq->m_Msg.m_Height = MaxHeight;
					net.PostRequest(*pReq, *this);
					m_nProofsExpected++;
				}

				SetTimer(90 * 1000);
				m_bRunning = true;
				io::Reactor::get_Current().run();
				KillTimer();

				verify_test(!pHdrs->m_vStates.empty());
			}
		};

		const Height hThrd1 = 250;
		RaiseHeightTo(node, hThrd1);


		MyFlyClient fc;
		// simple case
		fc.SyncSync();

		verify_test(fc.m_bTip);
		verify_test(fc.m_hRolledTo == MaxHeight);
		verify_test(!fc.m_Hist.m_Map.empty() && fc.m_Hist.m_Map.rbegin()->second.m_Height == hThrd1);

		{
			// pop last
			auto it = fc.m_Hist.m_Map.rbegin();
			fc.m_Hist.m_Map.erase((++it).base());
		}

		fc.SyncSync(); // should be trivial
		verify_test(fc.m_bTip);
		verify_test(fc.m_hRolledTo == MaxHeight);
		verify_test(!fc.m_Hist.m_Map.empty() && fc.m_Hist.m_Map.rbegin()->second.m_Height == hThrd1);

		const Height hThrd2 = 270;
		RaiseHeightTo(node, hThrd2);

		// should only fill the gap to the tip, not from the beginning. Should involve
		fc.SyncSync();
		verify_test(fc.m_bTip);
		verify_test(fc.m_hRolledTo == MaxHeight);
		verify_test(!fc.m_Hist.m_Map.empty() && fc.m_Hist.m_Map.rbegin()->second.m_Height == hThrd2);

		// simulate branching, make it rollback
		Height hBranch = 203;
		fc.m_Hist.DeleteFrom(hBranch + 1);

		Block::SystemState::Full s1;
		ZeroObject(s1);
		s1.m_Height = hBranch + 2;
		fc.m_Hist.m_Map[s1.m_Height] = s1;

		fc.SyncSync();

		verify_test(fc.m_bTip);
		verify_test(fc.m_hRolledTo <= hBranch); // must rollback beyond the manually appended state
		verify_test(!fc.m_Hist.m_Map.empty() && fc.m_Hist.m_Map.rbegin()->second.m_Height == hThrd2);
	}

	void TestHalving()
	{
		HeightRange hr;
		for (hr.m_Min = Rules::HeightGenesis; ; hr.m_Min++)
		{
			Amount v = Rules::get_Emission(hr.m_Min);
			if (!v)
				break;

			AmountBig::Type sum0(v);

			uint32_t nZeroTest = 200;
			for (hr.m_Max = hr.m_Min; nZeroTest; )
			{
				AmountBig::Type sum1;
				Rules::get_Emission(sum1, hr);
				verify_test(sum0 == sum1);

				v = Rules::get_Emission(++hr.m_Max);
				if (v)
					sum0 += uintBigFrom(v);
				else
					nZeroTest--;
			}
		}
	}

	struct Waiter
	{
		io::Timer::Ptr m_pTimer;
		bool m_Ok;

		void OnTimer()
		{
			m_Ok = false;
			TestFailed("timeout", __LINE__);
			StopSafe(false);
		}

		bool StopSafe(bool bRes)
		{
			if (!m_pTimer)
				return false;

			m_pTimer->cancel();
			m_pTimer.reset();
			io::Reactor::get_Current().stop();

			m_Ok = bRes;
			return true;
		}

		bool Wait()
		{
			m_Ok = true;
			m_pTimer = io::Timer::create(io::Reactor::get_Current());
			m_pTimer->start(2000000, false, [this]() { return (this->OnTimer)(); });

			io::Reactor::get_Current().run();
			StopSafe(false); // would propagate foreign stop

			return m_Ok;
		}
	};

	bool PerformRequestSync(proto::FlyClient::INetwork& net, proto::FlyClient::Request& r)
	{
		struct MyHandler
			:public proto::FlyClient::Request::IHandler
		{
			Waiter m_W;

			void OnComplete(proto::FlyClient::Request&) override
			{
				m_W.StopSafe(true);
			}
		};

		MyHandler h;
		net.PostRequest(r, h);

		bool bRes = h.m_W.Wait();

		r.m_pTrg = nullptr;
		return bRes;
	}

	void TestDependentTxs()
	{
		io::Reactor::Ptr pReactor(io::Reactor::create());
		io::Reactor::Scope scope(*pReactor);

		struct MyFlyClient
			:public proto::FlyClient
		{
			Block::SystemState::HistoryMap m_Hist;
			Key::IKdf::Ptr m_pKdf;
			Waiter* m_pW = nullptr;


			Block::SystemState::IHistory& get_History() override
			{
				return m_Hist;
			}

			void OnNewTip() override
			{
				if (m_pW)
				{
					m_pW->StopSafe(true);
					m_pW = nullptr;
				}
			}

			void get_Kdf(Key::IKdf::Ptr& pOut) override {
				pOut = m_pKdf;
			}
			void get_OwnerKdf(Key::IPKdf::Ptr& pOut) override {
				pOut = m_pKdf;
			}


		};


		MyFlyClient fc;
		ECC::SetRandom(fc.m_pKdf);

		Node node;
		node.m_Cfg.m_sPathLocal = g_sz;
		node.m_Cfg.m_Listen.port(g_Port);
		node.m_Cfg.m_Listen.ip(INADDR_ANY);
		node.m_Cfg.m_MiningThreads = 0;
		node.m_Cfg.m_Treasury = g_Treasury;
		node.m_Keys.SetSingleKey(fc.m_pKdf);
		node.Initialize();
		node.m_PostStartSynced = true;

		RaiseHeightTo(node, 20);

		Node node2;
		node2.m_Cfg.m_sPathLocal = g_sz2;
		node2.m_Cfg.m_MiningThreads = 0;
		ECC::SetRandom(node2);

		io::Address& addr = node2.m_Cfg.m_Connect.emplace_back();
		addr.resolve("127.0.0.1");
		addr.port(g_Port);
		node2.Initialize();


		auto pNet = std::make_shared<proto::FlyClient::NetworkStd>(fc);
		pNet->m_Cfg.m_vNodes.push_back(addr);

		pNet->Connect();

		struct MyManager
			:public bvm2::ManagerStd
		{
			Waiter* m_pW = nullptr;
			bool m_Done;
			bool m_Err;

			std::list<CoinID> m_lstCoins;
			std::vector<Merkle::Hash> m_vKrnIds;

			void OnDone(const std::exception* pExc) override
			{
				m_Done = true;
				m_Err = !!pExc;

				if (m_pW)
					m_pW->StopSafe(!m_Err);
			}

			void RunSync0(uint32_t iMethod)
			{
				m_Done = false;
				m_Err = false;

				StartRun(iMethod);
			}

			void RunSync1()
			{
				if (m_Done)
					return;

				{
					Waiter wt;
					m_pW = &wt;
					wt.Wait();
					m_pW = nullptr;
				}

				if (!m_Done)
					// propagate it
					io::Reactor::get_Current().stop();
			}

			void RunSync(uint32_t iMethod)
			{
				RunSync0(iMethod);
				RunSync1();
			}

			Transaction::Ptr BuildTx()
			{
				Height hTx = m_Context.m_Height + 1;

				auto pTx = std::make_shared<Transaction>();
				pTx->m_Offset = Zero;

				bvm2::FundsMap fm;

				for (uint32_t i = 0; i < m_InvokeData.m_vec.size(); i++)
				{
					const auto& cdata = m_InvokeData.m_vec[i];

					Amount fee;
					if (cdata.IsAdvanced())
						fee = cdata.m_Adv.m_Fee; // can't change!
					else
						fee = cdata.get_FeeMin(hTx);

					cdata.Generate(*pTx, *m_pKdf, hTx, fee);

					auto& krn = *pTx->m_vKernels.back();
					m_vKrnIds.push_back(krn.m_Internal.m_ID);

					fm += cdata.m_Spend;
					fm[0] += fee;
				}

				ECC::Scalar::Native kOff(pTx->m_Offset);

				auto valSpend = fm[0];
				while (valSpend > 0)
				{
					if (m_lstCoins.empty())
					{
						TestFailed("no funds", __LINE__);
						return nullptr;
					}

					CoinID cid = m_lstCoins.front();
					m_lstCoins.pop_front();

					Input::Ptr pInp = std::make_unique<Input>();

					ECC::Scalar::Native sk;
					CoinID::Worker(cid).Create(sk, pInp->m_Commitment, *m_pKdf);
					pTx->m_vInputs.push_back(std::move(pInp));
					kOff += sk;

					valSpend -= cid.m_Value;
				}

				if (valSpend < 0)
				{
					Output::Ptr pOutp = std::make_unique<Output>();

					CoinID cid(Zero);
					cid.m_Type = Key::Type::Change;
					cid.m_Value = -valSpend;
					ECC::GenRandom(&cid.m_Idx, sizeof(cid.m_Idx));

					m_lstCoins.push_front(cid); // reuse in the next tx

					ECC::Scalar::Native sk;
					pOutp->Create(hTx, sk, *m_pKdf, cid, *m_pKdf);

					pTx->m_vOutputs.push_back(std::move(pOutp));
					kOff += -sk;
				}

				pTx->m_Offset = kOff;
				pTx->Normalize();
				return pTx;
			}

			void BuildAndSend(proto::FlyClient::INetwork& net)
			{
				RunSync(1);
				verify_test(!m_InvokeData.m_vec.empty());

				proto::FlyClient::RequestTransaction::Ptr pReq(new proto::FlyClient::RequestTransaction);
				pReq->m_Msg.m_Transaction = BuildTx();

				if (m_Context.m_pParent)
					pReq->m_Msg.m_Context = std::make_unique<Merkle::Hash>(*m_Context.m_pParent);

				pReq->m_Msg.m_Fluff = true;

				verify_test(beam::PerformRequestSync(*m_pNetwork, *pReq));
				verify_test(proto::TxStatus::Ok == pReq->m_Res.m_Value);
			}

		} man;


		{
			proto::FlyClient::RequestEvents::Ptr pReq(new proto::FlyClient::RequestEvents);
			verify_test(PerformRequestSync(*pNet, *pReq));

			Block::SystemState::Full s;
			fc.m_Hist.get_Tip(s);

			struct MyParser :public proto::Event::IGroupParser
			{
				Height m_hMaxMaturity;
				std::list<CoinID>* m_pCoins;

				void OnEventType(proto::Event::Utxo& evt) override
				{
					if ((evt.m_Maturity <= m_hMaxMaturity) && !evt.m_Cid.m_AssetID)
						m_pCoins->push_back(evt.m_Cid);
				}
			} p;

			p.m_hMaxMaturity = s.m_Height;
			p.m_pCoins = &man.m_lstCoins;
			p.Proceed(pReq->m_Res.m_Events);

			verify_test(!man.m_lstCoins.empty());
		}

		man.m_EnforceDependent = true;
		man.m_pHist = &fc.m_Hist;
		man.m_pNetwork = pNet;
		man.m_pKdf = fc.m_pKdf;
		man.m_pPKdf = fc.m_pKdf;

		bvm2::Compile(man.m_BodyManager, "vault/app.wasm", bvm2::Processor::Kind::Manager);
		bvm2::Compile(man.m_BodyContract, "vault/contract.wasm", bvm2::Processor::Kind::Contract);

		// 1. Deploy

		man.m_Args["role"] = "manager";
		man.m_Args["action"] = "create";
		man.BuildAndSend(*pNet);

		bvm2::ContractID cid;
		bvm2::get_Cid(cid, man.m_BodyContract, Blob(nullptr, 0));
		man.set_ArgBlob("cid", cid);

		// 2. Deposit
		Amount valDeposit = 25 * Rules::Coin;

		man.m_Args["role"] = "my_account";
		man.m_Args["action"] = "deposit";
		man.m_Args["amount"] = std::to_string(valDeposit);
		man.BuildAndSend(*pNet);

		// 3. Withdraw
		valDeposit = 22 * Rules::Coin;

		man.m_Args["action"] = "withdraw";
		man.m_Args["amount"] = std::to_string(valDeposit);
		man.BuildAndSend(*pNet);

		RaiseHeightTo(node, 21);

		// make sure everything's included in the block
		for (uint32_t i = 0; i < man.m_vKrnIds.size(); i++)
		{
			const auto& hv = man.m_vKrnIds[i];

			Height h = node.get_Processor().get_DB().FindKernel(hv);
			verify_test(h == 21);
		}
	}



}

void TestAll()
{
	ECC::PseudoRandomGenerator prg;
	ECC::PseudoRandomGenerator::Scope scopePrg(&prg);

	bool bClientProtoOnly = false;

	//auto logger = beam::Logger::create(LOG_LEVEL_DEBUG, LOG_LEVEL_DEBUG);
	if (!bClientProtoOnly)
		beam::PrintEmissionSchedule();

	beam::Rules::get().AllowPublicUtxos = true;
	beam::Rules::get().FakePoW = true;
	beam::Rules::get().MaxRollback = 10;
	beam::Rules::get().DA.WindowWork = 35;
	beam::Rules::get().Maturity.Coinbase = 35; // lowered to see more txs
	beam::Rules::get().Emission.Drop0 = 5;
	beam::Rules::get().Emission.Drop1 = 8;
	beam::Rules::get().CA.Enabled = true;
	beam::Rules::get().Maturity.Coinbase = 10;
	beam::Rules::get().pForks[1].m_Height = 16;
	beam::Rules::get().UpdateChecksum();

	beam::PrepareTreasury();

	if (!bClientProtoOnly)
	{
		beam::TestHalving();
		beam::TestChainworkProof();
	}

	// Make sure this test doesn't run in parallel. We have the following potential collisions for Nodes:
	//	.db files
	//	ports, wrong beacon and etc.
	verify_test(beam::helpers::ProcessWideLock("/tmp/BEAM_node_test_lock"));

	beam::DeleteFile(beam::g_sz);
	beam::DeleteFile(beam::g_sz2);

	if (!bClientProtoOnly)
	{
		printf("NodeDB test...\n");
		fflush(stdout);

		beam::TestNodeDB();
		beam::DeleteFile(beam::g_sz);

		{
			printf("NodeProcessor test1...\n");
			fflush(stdout);


			std::vector<beam::BlockPlus::Ptr> blockChain;
			beam::TestNodeProcessor1(blockChain);
			beam::DeleteFile(beam::g_sz);
			beam::DeleteFile(beam::g_sz2);

			printf("NodeProcessor test2...\n");
			fflush(stdout);

			beam::TestNodeProcessor2(blockChain);
			beam::DeleteFile(beam::g_sz);

			printf("NodeProcessor test3...\n");
			fflush(stdout);

			beam::TestNodeProcessor3(blockChain);
			beam::DeleteFile(beam::g_sz);
			beam::DeleteFile(beam::g_sz2);
		}

		printf("NodeX2 concurrent test...\n");
		fflush(stdout);

		beam::TestNodeConversation();
		beam::DeleteFile(beam::g_sz);
		beam::DeleteFile(beam::g_sz2);
	}

	beam::Rules::get().MaxRollback = 100;
	beam::Rules::get().pForks[2].m_Height = 17;
	beam::Rules::get().pForks[3].m_Height = 17;
	beam::Rules::get().pForks[4].m_Height = 17;
	beam::Rules::get().CA.DepositForList2 = beam::Rules::Coin * 16;
	beam::Rules::get().CA.LockPeriod = 2;
	beam::Rules::get().Shielded.m_ProofMax = { 4, 6 }; // 4K
	beam::Rules::get().Shielded.m_ProofMin = { 4, 5 }; // 1K
	beam::Rules::get().UpdateChecksum();

	printf("Node <---> Client test (with proofs)...\n");
	fflush(stdout);

	beam::TestNodeClientProto();

	{
		auto logger = beam::Logger::create(LOG_LEVEL_DEBUG, LOG_LEVEL_DEBUG);

		{
			// test migration
			beam::NodeDB db;
			db.Open(beam::g_sz);
			db.ParamIntSet(beam::NodeDB::ParamID::Flags1, beam::NodeDB::Flags1::PendingRebuildNonStd);
		}

		// test mapping image rebuilding with shielded in/outs and contracts
		beam::io::Reactor::Ptr pReactor(beam::io::Reactor::create());
		beam::io::Reactor::Scope scope(*pReactor);

		std::string sPath;
		beam::NodeProcessor::get_MappingPath(sPath, beam::g_sz);
		beam::DeleteFile(sPath.c_str());

		beam::Node node;
		node.m_Cfg.m_sPathLocal = beam::g_sz;
		node.Initialize();

		auto& p = node.get_Processor();

		beam::Block::SystemState::ID sid;
		sid.m_Height = 15;
		sid.m_Hash = beam::Zero;

		p.ManualSelect(sid); // won't go down, can't rollback that deep

		sid.m_Height = p.m_Cursor.m_Full.m_Height;
		p.get_DB().get_StateHash(p.FindActiveAtStrict(sid.m_Height), sid.m_Hash);
		p.ManualSelect(sid); // already at correct branch, won't go down
		verify_test(sid.m_Height == p.m_Cursor.m_Full.m_Height);

		sid.m_Hash.Inv();
		p.ManualSelect(sid); // should go down
		verify_test(sid.m_Height > p.m_Cursor.m_Full.m_Height);
	}

	beam::DeleteFile(beam::g_sz);
	beam::DeleteFile(beam::g_sz2);
	beam::DeleteFile(beam::g_sz3);

	printf("Node <---> FlyClient test...\n");
	fflush(stdout);

	beam::TestFlyClient();
	beam::DeleteFile(beam::g_sz);

	printf("Node <---> Dependent txs test...\n");
	fflush(stdout);

	beam::TestDependentTxs();
	beam::DeleteFile(beam::g_sz);
	beam::DeleteFile(beam::g_sz2);
}

int main()
{
	try
	{
		TestAll();
	}
	catch (const std::exception & ex)
	{
		printf("Expression: %s\n", ex.what());
		g_TestsFailed++;
	}

	return g_TestsFailed ? -1 : 0;
}
