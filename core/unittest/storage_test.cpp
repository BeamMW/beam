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

#include <iostream>
#include "../radixtree.h"
#include "../navigator.h"
#include "../../utility/serialize.h"

#ifndef WIN32
#	include <unistd.h>
#endif // WIN32

int g_TestsFailed = 0;

void TestFailed(const char* szExpr, uint32_t nLine)
{
	printf("Test failed! Line=%u, Expression: %s\n", nLine, szExpr);
	g_TestsFailed++;
}

#define verify_test(x) \
	do { \
		if (!(x)) \
			TestFailed(#x, __LINE__); \
	} while (false)

namespace beam
{
	class BlockChainClient
		:public ChainNavigator
	{
		struct Type {
			enum Enum {
				MyPatch = ChainNavigator::Type::count,
				count
			};
		};

	public:

		struct Header
			:public ChainNavigator::FixedHdr
		{
			uint32_t m_pDatas[30];
		};


		struct PatchPlus
			:public Patch
		{
			uint32_t m_iIdx;
			int32_t m_Delta;
		};

		void assert_valid() const { ChainNavigator::assert_valid(); }

		void Commit(uint32_t iIdx, int32_t nDelta)
		{
			PatchPlus* p = (PatchPlus*) m_Mapping.Allocate(Type::MyPatch, sizeof(PatchPlus));
			p->m_iIdx = iIdx;
			p->m_Delta = nDelta;

			ChainNavigator::Commit(*p);

			assert_valid();
		}

		void Tag(uint8_t n)
		{
			TagInfo ti;
			ZeroObject(ti);

			ti.m_Tag.m_pData[0] = n;
			ti.m_Height = 1;

			CreateTag(ti);

			assert_valid();
		}

	protected:
		// ChainNavigator
		virtual void AdjustDefs(MappedFile::Defs&d)
		{
			d.m_nBanks = Type::count;
			d.m_nFixedHdr = sizeof(Header);
		}

		virtual void Delete(Patch& p)
		{
			m_Mapping.Free(Type::MyPatch, &p);
		}

		virtual void Apply(const Patch& p, bool bFwd)
		{
			PatchPlus& pp = (PatchPlus&) p;
			Header& hdr = (Header&) get_Hdr_();

			verify_test(pp.m_iIdx < _countof(hdr.m_pDatas));

			if (bFwd)
				hdr.m_pDatas[pp.m_iIdx] += pp.m_Delta;
			else
				hdr.m_pDatas[pp.m_iIdx] -= pp.m_Delta;
		}

		virtual Patch* Clone(Offset x)
		{
			// during allocation ptr may change
			PatchPlus* pRet = (PatchPlus*) m_Mapping.Allocate(Type::MyPatch, sizeof(PatchPlus));
			PatchPlus& src = (PatchPlus&) get_Patch_(x);

			*pRet = src;

			return pRet;
		}

		virtual void assert_valid(bool b)
		{
			verify_test(b);
		}
	};


	void TestNavigator()
	{
#ifdef WIN32
		const char* sz = "mytest.bin";
#else // WIN32
		const char* sz = "/tmp/mytest.bin";
#endif // WIN32

		DeleteFile(sz);

		BlockChainClient bcc;

		bcc.Open(sz);
		bcc.assert_valid();

		bcc.Tag(15);

		bcc.Commit(0, 15);
		bcc.Commit(3, 10);

		bcc.MoveBwd();
		bcc.assert_valid();

		bcc.Tag(76);

		bcc.Commit(9, 35);
		bcc.Commit(10, 20);

		bcc.MoveBwd();
		bcc.assert_valid();

		for (ChainNavigator::Offset x = bcc.get_ChildTag(); x; x = bcc.get_NextTag(x))
		{
			bcc.MoveFwd(x);
			bcc.assert_valid();

			bcc.MoveBwd();
			bcc.assert_valid();
		}

		bcc.MoveFwd(bcc.get_ChildTag());
		bcc.assert_valid();

		bcc.Close();
		bcc.Open(sz);
		bcc.assert_valid();

		bcc.Tag(44);
		bcc.Commit(12, -3);

		bcc.MoveBwd();
		bcc.assert_valid();

		bcc.DeleteTag(bcc.get_Hdr().m_TagCursor); // will also move bkwd
		bcc.assert_valid();

		for (ChainNavigator::Offset x = bcc.get_ChildTag(); x; x = bcc.get_NextTag(x))
		{
			bcc.MoveFwd(x);
			bcc.assert_valid();

			bcc.MoveBwd();
			bcc.assert_valid();
		}
	}

	void SetRandomUtxoKey(UtxoTree::Key::Data& d)
	{
		for (size_t i = 0; i < d.m_Commitment.m_X.nBytes; i++)
			d.m_Commitment.m_X.m_pData[i] = (uint8_t) rand();

		d.m_Commitment.m_Y = (1 & rand());

		for (size_t i = 0; i < sizeof(d.m_Maturity); i++)
			((uint8_t*) &d.m_Maturity)[i] = (uint8_t) rand();
	}

	void SetLeafID(TxoID& var, uint32_t i, bool bTest)
	{
		if (bTest)
			verify_test(var == i);
		else
			var = i;
	}

	void SetLeafIDs(UtxoTree& t, UtxoTree::MyLeaf& x, uint32_t i, bool bTest)
	{
		bool bExt = !(i % 12);
		if (bTest)
			verify_test(x.IsExt() == bExt);

		if (bExt)
		{
			if (!bTest)
			{
				for (uint32_t j = 0; j < 2; j++)
					t.PushID(0, x);
			}

			for (auto p = x.m_pIDs.get_Strict()->m_pTop.get_Strict(); p; p = p->m_pNext.get())
				SetLeafID(p->m_ID, i++, bTest);
		}
		else
			SetLeafID(x.m_ID, i, bTest);
	}

	void TestUtxoTree()
	{
		std::vector<UtxoTree::Key> vKeys;
		vKeys.resize(70000);

		UtxoTree t;
		Merkle::Hash hv1, hv2, hvMid;

		for (uint32_t i = 0; i < vKeys.size(); i++)
		{
			UtxoTree::Key& key = vKeys[i];

			// random key
			UtxoTree::Key::Data d0, d1;
			SetRandomUtxoKey(d0);

			key = d0;
			d1 = key;

			verify_test(d0.m_Commitment == d1.m_Commitment);
			verify_test(d0.m_Maturity == d1.m_Maturity);

			UtxoTree::Cursor cu;
			bool bCreate = true;
			UtxoTree::MyLeaf* p = t.Find(cu, key, bCreate);

			verify_test(p && bCreate);

			SetLeafIDs(t, *p, i, false);

			if (!(i % 17))
			{
				t.get_Hash(hv1); // try to confuse clean/dirty

				for (int k = 0; k < 10; k++)
				{
					uint32_t j = rand() % (i + 1);

					bCreate = false;
					p = t.Find(cu, vKeys[j], bCreate);
					assert(p && !bCreate);

					Merkle::Proof proof;
					t.get_Proof(proof, cu);

					Merkle::Hash hvElement;
					p->get_Hash(hvElement);

					Merkle::Interpret(hvElement, proof);
					verify_test(hvElement == hv1);
				}
			}
		}

		t.get_Hash(hv1);

		for (uint32_t i = 0; i < vKeys.size(); i++)
		{
			if (i == vKeys.size()/2)
				t.get_Hash(hvMid);

			UtxoTree::Cursor cu;
			bool bCreate = true;
			UtxoTree::MyLeaf* p = t.Find(cu, vKeys[i], bCreate);

			verify_test(p && !bCreate);
			SetLeafIDs(t, *p, i, true);

			t.Delete(cu);

			if (!(i % 31))
				t.get_Hash(hv2); // try to confuse clean/dirty
		}

		t.get_Hash(hv2);
		verify_test(hv2 == Zero);

		// construct tree in different order
		for (uint32_t i = (uint32_t) vKeys.size(); i--; )
		{
			const UtxoTree::Key& key = vKeys[i];

			UtxoTree::Cursor cu;
			bool bCreate = true;
			UtxoTree::MyLeaf* p = t.Find(cu, key, bCreate);

			verify_test(p && bCreate);
			SetLeafIDs(t, *p, i, false);

			if (!(i % 11))
				t.get_Hash(hv2); // try to confuse clean/dirty

			if (i == vKeys.size()/2)
			{
				t.get_Hash(hv2);
				verify_test(hv2 == hvMid);
			}
		}

		t.get_Hash(hv2);
		verify_test(hv2 == hv1);

		verify_test(vKeys.size() == t.Count());

		// serialization
		Serializer ser;
		t.save(ser);

		SerializeBuffer sb = ser.buffer();

		Deserializer der;
		der.reset(sb.first, sb.second);

		t.load(der);

		t.get_Hash(hv2);
		verify_test(hv2 == hv1);

		// narrow traverse
		struct Traveler
			:public RadixTree::ITraveler
		{
			UtxoTree::Key m_Min, m_Max, m_Last;

			virtual bool OnLeaf(const RadixTree::Leaf& x) override
			{
				const UtxoTree::MyLeaf& v = Cast::Up<UtxoTree::MyLeaf>(x);
				verify_test(v.m_Key.V >= m_Min.V);
				verify_test(v.m_Key.V <= m_Max.V);
				verify_test(v.m_Key.V > m_Last.V);
				m_Last = v.m_Key;
				return true;
			}
		} t2;

		ZeroObject(t2.m_Min);
		ZeroObject(t2.m_Max);
		t2.m_Min.V.m_pData[0] = 0x33;
		t2.m_Max.V.m_pData[0] = 0x3a;
		t2.m_Max.V.m_pData[1] = 0xe2;
		ZeroObject(t2.m_Last);

		UtxoTree::Cursor cu;
		t2.m_pCu = &cu;
		t2.m_pBound[0] = t2.m_Min.V.m_pData;
		t2.m_pBound[1] = t2.m_Max.V.m_pData;
		t.Traverse(t2);

		// full traverse, and verification of Compact

		struct Traveler3
			:public RadixTree::ITraveler
		{
			UtxoTree::Compact m_Compact;

			virtual bool OnLeaf(const RadixTree::Leaf& x) override
			{
				const UtxoTree::MyLeaf& v = Cast::Up<UtxoTree::MyLeaf>(x);
				uint32_t nCount = v.get_Count();

				while (nCount--)
					verify_test(m_Compact.Add(v.m_Key));

				return true;
			}
		} t3;

		t.Traverse(t3);

		t3.m_Compact.Flush(hv2);
		verify_test(hv1 == hv2);
	}

	struct MyMmr
		:public Merkle::Mmr
	{
		typedef std::vector<Merkle::Hash> HashVector;
		typedef std::unique_ptr<HashVector> HashVectorPtr;

		std::vector<HashVectorPtr> m_vec;

		Merkle::Hash& get_At(const Merkle::Position& pos)
		{
			if (m_vec.size() <= pos.H)
				m_vec.resize(pos.H + 1);

			HashVectorPtr& ptr = m_vec[pos.H];
			if (!ptr)
				ptr.reset(new HashVector);

		
			HashVector& vec = *ptr;
			if (vec.size() <= size_t(pos.X))
				vec.resize(size_t(pos.X) + 1);

			return vec[size_t(pos.X)];
		}

		virtual void LoadElement(Merkle::Hash& hv, const Merkle::Position& pos) const override
		{
			hv = Cast::NotConst(this)->get_At(pos);
		}

		virtual void SaveElement(const Merkle::Hash& hv, const Merkle::Position& pos) override
		{
			get_At(pos) = hv;
		}
	};

	struct MyDmmr
		:public Merkle::DistributedMmr
	{
		struct Node
		{
			typedef std::unique_ptr<Node> Ptr;

			Merkle::Hash m_MyHash;
			std::unique_ptr<uint8_t[]> m_pArr;
		};

		std::vector<Node::Ptr> m_AllNodes;

		virtual const void* get_NodeData(Key key) const override
		{
			assert(key);
			return ((Node*) key)->m_pArr.get();
		}

		virtual void get_NodeHash(Merkle::Hash& hash, Key key) const override
		{
			hash = ((Node*) key)->m_MyHash;
		}

		void MyAppend(const Merkle::Hash& hv)
		{
			uint32_t n = get_NodeSize(m_Count);

			MyDmmr::Node::Ptr p(new MyDmmr::Node);
			p->m_MyHash = hv;

			if (n)
				p->m_pArr.reset(new uint8_t[n]);

			Append((Key) p.get(), p->m_pArr.get(), p->m_MyHash);
			m_AllNodes.push_back(std::move(p));
		}
	};

	void TestMmr()
	{
		std::vector<Merkle::Hash> vHashes;
		vHashes.resize(300);

		std::vector<uint32_t> vSet;

		MyMmr mmr;
		MyDmmr dmmr;
		Merkle::CompactMmr cmmr;
		Merkle::FixedMmmr fmmr(vHashes.size());

		struct MyFlyMmr
			:public Merkle::FlyMmr
		{
			const Merkle::Hash* m_pHashes;

			virtual void LoadElement(Merkle::Hash& hv, uint64_t n) const override
			{
				verify_test(n < m_Count);
				hv = m_pHashes[n];
			}
		};

		MyFlyMmr flymmr;
		flymmr.m_pHashes = &vHashes.front();

		for (uint32_t i = 0; i < vHashes.size(); i++)
		{
			Merkle::Hash& hv = vHashes[i];

			for (uint32_t j = 0; j < hv.nBytes; j++)
				hv.m_pData[j] = (uint8_t)rand();

			Merkle::Hash hvRoot, hvRoot2, hvRoot3, hvRoot4, hvRoot5;

			mmr.get_PredictedHash(hvRoot, hv);
			dmmr.get_PredictedHash(hvRoot2, hv);
			cmmr.get_PredictedHash(hvRoot3, hv);
			fmmr.get_PredictedHash(hvRoot4, hv);
			verify_test(hvRoot == hvRoot2);
			verify_test(hvRoot == hvRoot3);
			verify_test(hvRoot == hvRoot4);

			mmr.Append(hv);
			dmmr.MyAppend(hv);
			cmmr.Append(hv);
			fmmr.Append(hv);

			flymmr.m_Count++;

			mmr.get_Hash(hvRoot);
			verify_test(hvRoot == hvRoot3);
			dmmr.get_Hash(hvRoot);
			verify_test(hvRoot == hvRoot3);
			cmmr.get_Hash(hvRoot);
			verify_test(hvRoot == hvRoot3);
			fmmr.get_Hash(hvRoot);
			verify_test(hvRoot == hvRoot3);
			flymmr.get_Hash(hvRoot);
			verify_test(hvRoot == hvRoot3);

			vSet.clear();

			for (uint32_t j = 0; j <= i; j++)
			{
				Merkle::Proof proof;
				mmr.get_Proof(proof, j);

				Merkle::ProofBuilderStd bld;
				dmmr.get_Proof(bld, j);
				verify_test(proof == bld.m_Proof);

				bld.m_Proof.clear();
				fmmr.get_Proof(bld, j);
				verify_test(proof == bld.m_Proof);

				if (i < 40) // flymmr is too heavy (everything is literally recalculated every time).
				{
					bld.m_Proof.clear();
					flymmr.get_Proof(bld, j);
					verify_test(proof == bld.m_Proof);
				}

				Merkle::Hash hv2 = vHashes[j];
				Merkle::Interpret(hv2, proof);
				verify_test(hv2 == hvRoot);

				if (rand() & 1)
					vSet.push_back(j);
			}

			Merkle::MultiProof mp;

			{
				struct Builder
					:public Merkle::MultiProof::Builder
				{
					const MyMmr& m_Mmr;
					Builder(Merkle::MultiProof& x, const MyMmr& mmr)
						:Merkle::MultiProof::Builder(x)
						,m_Mmr(mmr)
					{
					}

					virtual void get_Proof(Merkle::IProofBuilder& p, uint64_t i) override
					{
						m_Mmr.get_Proof(p, i);
					}
				};

				Builder bld(mp, mmr);
				for (uint32_t j = 0; j < vSet.size(); j++)
					bld.Add(vSet[j]);
			}

			struct MyVerifier
				:public Merkle::MultiProof::Verifier
			{
				Merkle::Hash m_hvRoot;

				MyVerifier(const Merkle::MultiProof& x, uint64_t nCount) :Verifier(x, nCount) {}

				virtual bool IsRootValid(const Merkle::Hash& hv) override { return hv == m_hvRoot; }
			};

			while (true)
			{
				MyVerifier ver(mp, i + 1);
				ver.m_hvRoot = hvRoot;

				for (uint32_t j = 0; j < vSet.size(); j++)
				{
					ver.m_hvPos = vHashes[vSet[j]];
					ver.Process(vSet[j]);
					verify_test(ver.m_bVerify);
				}

				// crop
				vSet.resize(vSet.size() / 2);
				if (vSet.empty())
					break;

				MyVerifier crop(mp, i + 1);
				crop.m_bVerify = false;

				for (uint32_t j = 0; j < vSet.size(); j++)
					crop.Process(vSet[j]);

				mp.m_vData.resize(crop.get_Pos() - mp.m_vData.begin());
			}

		}
	}

} // namespace beam

int main()
{
	beam::TestNavigator();
	beam::TestUtxoTree();
	beam::TestMmr();

	return g_TestsFailed ? -1 : 0;
}
