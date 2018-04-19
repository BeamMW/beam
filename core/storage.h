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

private:
	Node* m_pRoot;

	void DeleteNode(Node*);
	void ReplaceTip(CursorBase& cu, Node* pNew);
};


} // namespace beam
