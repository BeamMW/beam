#include <iostream>
#include "../navigator.h"

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
			memset(&ti, 0, sizeof(ti));

			ti.m_Tag.m_pData[0] = n;
			ti.m_Difficulty = 1;
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


void DeleteFile(const char* szPath)
{
#ifdef WIN32
	DeleteFileA(szPath);
#else // WIN32
	unlink(szPath);
#endif // WIN32
}

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


	void TestUtxoTree()
	{
		std::vector<UtxoTree::Key> vKeys;
		vKeys.resize(70000);

		UtxoTree t;
		Merkle::Hash hv1, hv2, hvMid;

		for (size_t i = 0; i < vKeys.size(); i++)
		{
			UtxoTree::Key& key = vKeys[i];

			// random key
			for (int j = 0; j < key.s_Bytes; j++)
				key.m_pArr[j] = (uint8_t) rand();

			UtxoTree::Cursor cu;
			bool bCreate = true;
			UtxoTree::MyLeaf* p = t.Find(cu, key, bCreate);

			verify_test(p && bCreate);
			p->m_Count = i;

			if (!(i % 17))
				t.get_Hash(hv1); // try to confuse clean/dirty
		}

		t.get_Hash(hv1);

		for (size_t i = 0; i < vKeys.size(); i++)
		{
			if (i == vKeys.size()/2)
				t.get_Hash(hvMid);

			UtxoTree::Cursor cu;
			bool bCreate = true;
			UtxoTree::MyLeaf* p = t.Find(cu, vKeys[i], bCreate);

			verify_test(p && !bCreate);
			verify_test(p->m_Count == i);
			t.Delete(cu);

			if (!(i % 31))
				t.get_Hash(hv2); // try to confuse clean/dirty
		}

		t.get_Hash(hv2);
		verify_test(hv2 == ECC::Zero);

		// construct tree in different order
		for (size_t i = vKeys.size(); i--; )
		{
			const UtxoTree::Key& key = vKeys[i];

			UtxoTree::Cursor cu;
			bool bCreate = true;
			UtxoTree::MyLeaf* p = t.Find(cu, key, bCreate);

			verify_test(p && bCreate);
			p->m_Count = i;

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
	}


} // namespace beam

int main()
{
	beam::TestNavigator();
	beam::TestUtxoTree();
}
