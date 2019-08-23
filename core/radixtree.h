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

#include "block_crypt.h"
#include "mapped_file.h"

namespace beam
{

class RadixTree
{
protected:

	template <typename T>
	class Ptr
	{
		int64_t m_Offset;
	public:

		operator bool() const
		{
			return m_Offset != 0;
		}

		void set_Strict(const T* p)
		{
			assert(p);
			m_Offset = reinterpret_cast<intptr_t>(p) - reinterpret_cast<intptr_t>(this);
		}

		void set(const T* p)
		{
			if (p)
				set_Strict(p);
			else
				m_Offset = 0;
		}

		T* get_Strict() const
		{
			assert(m_Offset);
			return reinterpret_cast<T*>(reinterpret_cast<intptr_t>(this) + m_Offset);
		}

		T* get() const
		{
			return m_Offset ? get_Strict() : nullptr;
		}
	};

	struct Node
	{
		uint16_t m_Bits;
		static const uint16_t s_Clean = 1 << 0xf;
		static const uint16_t s_Leaf  = 1 << 0xe;
		static const uint16_t s_User  = 1 << 0xd;

		uint16_t get_Bits() const;
	};

	struct Joint :public Node {
		Ptr<Node> m_ppC[2];
		Ptr<uint8_t> m_pKeyPtr; // should be equal to one of the ancestors
	};

public:

	struct Leaf :public Node {
	};


	virtual void OnDirty() {}

protected:
	Node* get_Root() const;
	const uint8_t* get_NodeKey(const Node&) const;

	virtual intptr_t get_Base() const { return 0; }

	virtual Joint* CreateJoint() = 0;
	virtual Leaf* CreateLeaf() = 0;
	virtual uint8_t* GetLeafKey(const Leaf&) const = 0;
	virtual void DeleteJoint(Joint*) = 0;
	virtual void DeleteLeaf(Leaf*) = 0;

public:

	RadixTree();
	~RadixTree();

	void Clear();

	class CursorBase
	{
	protected:
		uint16_t m_nBits;
		uint16_t m_nPtrs;
		uint16_t m_nPosInLastNode;

		Node** const m_pp;

		static uint8_t get_BitRawStat(const uint8_t* p0, uint16_t nBit);

		uint8_t get_BitRaw(const uint8_t* p0) const;
		uint8_t get_Bit(const uint8_t* p0) const;

		friend class RadixTree;

	public:
		CursorBase(Node** pp) :m_pp(pp) {}

		Leaf& get_Leaf() const;
		void InvalidateElement();

		Node** get_pp() const { return m_pp; }
		uint16_t get_Depth() const { return m_nPtrs; }
	};

	template <uint16_t nKeyBits>
	class Cursor_T :public CursorBase
	{
		Node* m_ppBuf[nKeyBits + 1];
	public:
		Cursor_T() :CursorBase(m_ppBuf) {}
	};

	bool Goto(CursorBase& cu, const uint8_t* pKey, uint16_t nBits) const;

	Leaf* Find(CursorBase& cu, const uint8_t* pKey, uint16_t nBits, bool& bCreate);

	void Delete(CursorBase& cu);

	struct ITraveler
	{
		CursorBase* m_pCu; // set it to a valid cursor instance to get the cursor of the element during traverse.
		// Insert/Delete are not allowed. However it may be used for invalidation or etc.

		// optional min/max bounds
		const uint8_t* m_pBound[2];

		ITraveler()
			:m_pCu(NULL)
		{
			ZeroObject(m_pBound);
		}

		virtual bool OnLeaf(const Leaf&) = 0; // return false to stop iteration
	};

	bool Traverse(ITraveler&) const;

	size_t Count() const; // implemented via the whole tree traversing, shouldn't use frequently.

protected:
	int64_t m_RootOffset;

private:
	void set_Root(Node*);

	void DeleteNode(Node*);
	void ReplaceTip(CursorBase& cu, Node* pNew);
	bool Traverse(const Node&, ITraveler&) const;

	static int Cmp(const uint8_t* pKey, const uint8_t* pThreshold, uint16_t n0, uint16_t dn);
	static int Cmp1(uint8_t, const uint8_t* pThreshold, uint16_t n0);
};

class RadixHashTree
	:public RadixTree
{
public:

	struct MyJoint :public Joint {
		Merkle::Hash m_Hash;
	};

	void get_Hash(Merkle::Hash&);
	void get_Proof(Merkle::Proof&, const CursorBase&);

protected:
	// RadixTree
	virtual Joint* CreateJoint() override { return new MyJoint; }
	virtual void DeleteJoint(Joint* p) override { delete Cast::Up<MyJoint>(p); }

	const Merkle::Hash& get_Hash(Node&, Merkle::Hash&);

	virtual const Merkle::Hash& get_LeafHash(Node&, Merkle::Hash&) = 0;
};

class RadixHashOnlyTree
	:public RadixHashTree
{
public:

	// Just store hashes.

	struct MyLeaf :public Leaf
	{
		Merkle::Hash m_Hash;
	};

	typedef RadixTree::Cursor_T<ECC::nBits> Cursor;

	MyLeaf* Find(CursorBase& cu, const Merkle::Hash& key, bool& bCreate)
	{
		static_assert(Merkle::Hash::nBits == ECC::nBits, "");
		return Cast::Up<MyLeaf>(RadixTree::Find(cu, key.m_pData, ECC::nBits, bCreate));
	}

	~RadixHashOnlyTree() { Clear(); }

protected:
	virtual Leaf* CreateLeaf() override { return new MyLeaf; }
	virtual uint8_t* GetLeafKey(const Leaf& x) const override { return Cast::Up<MyLeaf>(Cast::NotConst(x)).m_Hash.m_pData; }
	virtual void DeleteLeaf(Leaf* p) override { delete Cast::Up<MyLeaf>(p); }
	virtual const Merkle::Hash& get_LeafHash(Node& n, Merkle::Hash&) override { return Cast::Up<MyLeaf>(n).m_Hash; }
};


class UtxoTree
	:public RadixHashTree
{
public:

	// This tree is different from RadixHashOnlyTree in 2 ways:
	//	1. Each key comes with a count (i.e. duplicates are allowed)
	//	2. We support "group search", i.e. all elements with a specified subkey. Given the UTXO commitment we can find all the counts and parameters.

	struct Key
	{
		static const uint16_t s_BitsCommitment = ECC::uintBig::nBits + 1; // curve point

		struct Data {
			ECC::Point m_Commitment;
			Height m_Maturity;
			Data& operator = (const Key&);
		};

		static const uint16_t s_Bits = s_BitsCommitment + sizeof(Height) * 8; // maturity
		static const uint16_t s_Bytes = (s_Bits + 7) >> 3;

		Key& operator = (const Data&);

		uintBig_t<s_Bytes> V;
	};

	struct MyLeaf :public Leaf
	{
		Key m_Key;
		Input::Count get_Count() const;

		struct IDNode {
			TxoID m_ID;
			Ptr<IDNode> m_pNext;
		};

		struct IDQueue {
			Ptr<IDNode> m_pTop;
			Input::Count m_Count;
		};

		union {
			TxoID m_ID;
			Ptr<IDQueue> m_pIDs;
		};

		bool IsExt() const;
		bool IsCommitmentDuplicated() const;

		void get_Hash(Merkle::Hash&) const;
		static void get_Hash(Merkle::Hash&, const Key&, Input::Count);
	};

	typedef RadixTree::Cursor_T<Key::s_Bits> Cursor;

	MyLeaf* Find(CursorBase& cu, const Key& key, bool& bCreate)
	{
		return Cast::Up<MyLeaf>(RadixTree::Find(cu, key.V.m_pData, key.s_Bits, bCreate));
	}

	~UtxoTree() { Clear(); }

	void PushID(TxoID, MyLeaf&);
	TxoID PopID(MyLeaf&);

    template<typename Archive>
    Archive& save(Archive& ar) const
	{
		Serializer<Archive> s(ar);
		SaveIntenral(s);
		return ar;
	}

    template<typename Archive>
    Archive& load(Archive& ar)
    {
		Serializer<Archive> s(ar);
		LoadIntenral(s);
		return ar;
	}

	class Compact
	{
		void FlushInternal(uint16_t nBitsCommonNext);

		// compact tree builder. Assumes all the elements are added in correct order
		struct Node {
			Merkle::Hash m_Hash;
			uint16_t m_nBitsCommon; // with prev node
		};

		std::vector<Node> m_vNodes;

		Key m_LastKey;
		Input::Count m_LastCount;

	public:
		bool Add(const Key&);
		void Flush(Merkle::Hash&);
	};

protected:
	virtual Leaf* CreateLeaf() override { return new MyLeaf; }
	virtual uint8_t* GetLeafKey(const Leaf& x) const override { return Cast::Up<MyLeaf>(Cast::NotConst(x)).m_Key.V.m_pData; }
	virtual void DeleteLeaf(Leaf* p) override;
	virtual const Merkle::Hash& get_LeafHash(Node&, Merkle::Hash&) override;

	virtual MyLeaf::IDQueue* CreateIDQueue() { return new MyLeaf::IDQueue; }
	virtual void DeleteIDQueue(MyLeaf::IDQueue* p) { delete p; }
	virtual MyLeaf::IDNode* CreateIDNode() { return new MyLeaf::IDNode; }
	virtual void DeleteIDNode(MyLeaf::IDNode* p) { delete p; }
	virtual void DeleteEmptyLeaf(Leaf* p) { delete Cast::Up<MyLeaf>(p); }

	struct ISerializer {
		virtual void Process(uint32_t&) = 0;
		virtual void Process(uint64_t&) = 0;
		virtual void Process(Key&) = 0;
	};

	template <typename Archive>
	struct Serializer :public ISerializer {
		Archive& m_ar;
		Serializer(Archive& ar) :m_ar(ar) {}

		virtual void Process(uint32_t& n) override { m_ar & n; }
		virtual void Process(uint64_t& n) override { m_ar & n; }
		virtual void Process(Key& k) override { m_ar & k.V.m_pData; }
	};

	void SaveIntenral(ISerializer&) const;
	void LoadIntenral(ISerializer&);

	void PushIDRaw(TxoID, MyLeaf::IDQueue&);
	TxoID PopIDRaw(MyLeaf::IDQueue&);
};

class UtxoTreeMapped
	:public UtxoTree
{
	MappedFile m_Mapping;

	struct Type {
		enum Enum {
			Leaf,
			Joint,
			Queue,
			Node,
			count
		};
	};

protected:

	template <typename T>
	T* Allocate(Type::Enum eType)
	{
		return (T*) m_Mapping.Allocate(eType, sizeof(T));

	}

	virtual intptr_t get_Base() const override;

	virtual Leaf* CreateLeaf() override;
	virtual void DeleteEmptyLeaf(Leaf*) override;
	virtual Joint* CreateJoint() override;
	virtual void DeleteJoint(Joint*) override;

	virtual MyLeaf::IDQueue* CreateIDQueue() override;
	virtual void DeleteIDQueue(MyLeaf::IDQueue*) override;
	virtual MyLeaf::IDNode* CreateIDNode() override;
	virtual void DeleteIDNode(MyLeaf::IDNode*) override;

public:

	virtual void OnDirty() override;

	typedef Merkle::Hash Stamp;

	~UtxoTreeMapped() { Close(); }

	bool Open(const char* sz, const Stamp&);
	bool IsOpen() const { return m_Mapping.get_Base() != nullptr; }

	void Close();
	void FlushStrict(const Stamp&);

	void EnsureReserve();

#pragma pack(push, 1)
	struct Hdr
	{
		MappedFile::Offset m_Root;
		MappedFile::Offset m_Dirty; // boolean, just aligned
		Stamp m_Stamp;
	};
#pragma pack(pop)

	Hdr& get_Hdr();
};

} // namespace beam
