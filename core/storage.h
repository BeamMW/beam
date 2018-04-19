#pragma once

#include "common.h"

namespace beam
{

class RadixTree
{
protected:

	struct Node
	{
		uint16_t m_Bits;
		static const uint16_t s_Clean = 1 << 0xf;
		static const uint16_t s_Leaf = 1 << 0xe;

		uint16_t get_Bits() const;
		const uint8_t* get_Key() const;
	};

	struct Joint :public Node {
		Node* m_ppC[2];
		const uint8_t* m_pKeyPtr; // should be equal to one of the ancestors
	};

	struct Leaf :public Node {
		uint8_t m_pKeyArr[1]; // to be extended
	};

	Node* get_Root() const { return m_pRoot; }

	virtual Joint* CreateJoint() = 0;
	virtual Leaf* CreateLeaf() = 0;

	virtual void DeleteJoint(Joint*) = 0;
	virtual void DeleteLeaf(Leaf*) = 0;

public:

	RadixTree();
	~RadixTree();

	void Clear();

	class CursorBase
	{
		uint32_t m_nBits;
		uint32_t m_nPtrs;
		uint32_t m_nPosInLastNode;

		Node* m_pp[1];

		uint8_t get_BitRaw(const uint8_t* p0) const;
		uint8_t get_Bit(const uint8_t* p0) const;

		friend class RadixTree;

	public:
		Leaf& get_Leaf() const;
		void Invalidate();
	};

	template <uint32_t nKeyBits>
	class Cursor_T :public CursorBase
	{
		Node* m_ppExtra[nKeyBits];
	};

	bool Goto(CursorBase& cu, const uint8_t* pKey, uint32_t nBits) const;

	Leaf* Find(CursorBase& cu, const uint8_t* pKey, uint32_t nBits, bool& bCreate);

	void Delete(CursorBase& cu);

	struct ITraveler {
		virtual bool OnLeaf(const Leaf&) = 0;
	};

	bool Traverse(ITraveler&) const;
	static bool Traverse(const CursorBase&, ITraveler&);

	size_t Count() const; // implemented via the whole tree traversing, shouldn't use frequently.

private:
	Node* m_pRoot;

	void DeleteNode(Node*);
	void ReplaceTip(CursorBase& cu, Node* pNew);
	static bool Traverse(const Node&, ITraveler&);
};


class UtxoTree
	:public beam::RadixTree
{
public:

	struct Key
	{
		static const uint32_t s_Bits =
			257 +	// curve point
			2 +		// coinbase flag, confidential flag
			64;		// block height

		static const uint32_t s_Bytes = (s_Bits + 7) >> 3;

		uint8_t m_pArr[s_Bytes];
	};

	struct MyJoint :public Joint {
		Merkle::Hash m_Hash;
	};

	struct MyLeaf :public Leaf
	{
		uint8_t m_pPlaceholder[Key::s_Bytes - 1]; // 1 byte is already included in the base
		uint32_t m_Count;
	};

	typedef RadixTree::Cursor_T<Key::s_Bits> Cursor;

	MyLeaf* Find(CursorBase& cu, const Key& key, bool& bCreate)
	{
		return (MyLeaf*) RadixTree::Find(cu, key.m_pArr, key.s_Bits, bCreate);
	}

	~UtxoTree() { Clear(); }

	void get_Hash(Merkle::Hash&);

protected:
	virtual Joint* CreateJoint() override { return new MyJoint; }
	virtual Leaf* CreateLeaf() override { return new MyLeaf; }

	virtual void DeleteJoint(Joint* p) override { delete (MyJoint*) p; }
	virtual void DeleteLeaf(Leaf* p) override { delete (MyLeaf*) p; }

	static const Merkle::Hash& get_Hash(Node&, Merkle::Hash&);
};

} // namespace beam
