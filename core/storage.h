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
	};

	struct Joint :public Node {
		Node* m_ppC[2];
		const uint8_t* m_pKeyPtr; // should be equal to one of the ancestors
	};

public:

	struct Leaf :public Node {
	};


protected:
	Node* get_Root() const { return m_pRoot; }
	const uint8_t* get_NodeKey(const Node&) const;

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
		uint32_t m_nBits;
		uint32_t m_nPtrs;
		uint32_t m_nPosInLastNode;

		Node** const m_pp;

		uint8_t get_BitRaw(const uint8_t* p0) const;
		uint8_t get_Bit(const uint8_t* p0) const;

		friend class RadixTree;

		CursorBase(Node** pp) :m_pp(pp) {}

	public:
		Leaf& get_Leaf() const;
		void Invalidate();

		Node** get_pp() const { return m_pp; }
		uint32_t get_Depth() const { return m_nPtrs; }
	};

	template <uint32_t nKeyBits>
	class Cursor_T :public CursorBase
	{
		Node* m_ppBuf[nKeyBits + 1];
	public:
		Cursor_T() :CursorBase(m_ppBuf) {}
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
	virtual void DeleteJoint(Joint* p) override { delete (MyJoint*) p; }

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
		static_assert(sizeof(key.m_pData) << 3 == ECC::nBits, "");
		return (MyLeaf*) RadixTree::Find(cu, key.m_pData, ECC::nBits, bCreate);
	}

	~RadixHashOnlyTree() { Clear(); }

protected:
	virtual Leaf* CreateLeaf() override { return new MyLeaf; }
	virtual uint8_t* GetLeafKey(const Leaf& x) const override { return ((MyLeaf&) x).m_Hash.m_pData; }
	virtual void DeleteLeaf(Leaf* p) override { delete (MyLeaf*) p; }
	virtual const Merkle::Hash& get_LeafHash(Node& n, Merkle::Hash&) override { return ((MyLeaf&) n).m_Hash; }
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
		static const uint32_t s_BitsCommitment = sizeof(ECC::uintBig) * 8 + 1; // curve point

		struct Data {
			ECC::Point m_Commitment;
			Height m_Maturity;
			Data& operator = (const Key&);
		};

		static const uint32_t s_Bits = s_BitsCommitment + sizeof(Height) * 8; // maturity
		static const uint32_t s_Bytes = (s_Bits + 7) >> 3;

		Key& operator = (const Data&);

		int cmp(const Key&) const;

		uint8_t m_pArr[s_Bytes];
	};

	struct Value
	{
		Input::Count m_Count;
		void get_Hash(Merkle::Hash&, const Key&) const;
	};

	struct MyLeaf :public Leaf
	{
		Key m_Key;
		Value m_Value;
	};

	typedef RadixTree::Cursor_T<Key::s_Bits> Cursor;

	MyLeaf* Find(CursorBase& cu, const Key& key, bool& bCreate)
	{
		return (MyLeaf*) RadixTree::Find(cu, key.m_pArr, key.s_Bits, bCreate);
	}

	~UtxoTree() { Clear(); }

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


protected:
	virtual Leaf* CreateLeaf() override { return new MyLeaf; }
	virtual uint8_t* GetLeafKey(const Leaf& x) const override { return ((MyLeaf&) x).m_Key.m_pArr; }
	virtual void DeleteLeaf(Leaf* p) override { delete (MyLeaf*) p; }
	virtual const Merkle::Hash& get_LeafHash(Node&, Merkle::Hash&) override;

	struct ISerializer {
		virtual void Process(uint32_t&) = 0;
		virtual void Process(Key&) = 0;
		virtual void Process(Value&) = 0;
	};

	template <typename Archive>
	struct Serializer :public ISerializer {
		Archive& m_ar;
		Serializer(Archive& ar) :m_ar(ar) {}

		virtual void Process(uint32_t& n) override { m_ar & n; }
		virtual void Process(Key& k) override { m_ar & k.m_pArr; }
		virtual void Process(Value& v) override { m_ar & v.m_Count; }
	};

	void SaveIntenral(ISerializer&) const;
	void LoadIntenral(ISerializer&);
};

namespace Merkle
{
	struct Mmr
	{
		uint64_t m_Count;
		Mmr() :m_Count(0) {}

		void Append(const Hash&);

		void get_Hash(Hash&) const;
		void get_Proof(Proof&, uint64_t i) const;
		void get_PredictedHash(Hash&, const Hash& hvAppend) const;

	protected:
		bool get_HashForRange(Hash&, uint64_t n0, uint64_t n) const;

		virtual void LoadElement(Hash&, uint64_t nIdx, uint8_t nHeight) const = 0;
		virtual void SaveElement(const Hash&, uint64_t nIdx, uint8_t nHeight) = 0;
	};

	struct DistributedMmr
	{
		typedef uint64_t Key;

		uint64_t m_Count;
		Key m_kLast;

		DistributedMmr()
			:m_Count(0)
			,m_kLast(0)
		{
		}

		static uint32_t get_NodeSize(uint64_t n);

		void Append(Key, void* pBuf, const Hash&);

		void get_Hash(Hash&) const;
		void get_Proof(Proof&, uint64_t i) const;
		void get_PredictedHash(Hash&, const Hash& hvAppend) const;

	protected:
		// Get the data of the node referenced by Key. The data of this node will only be used until this function is called for another node.
		virtual const void* get_NodeData(Key) const = 0;
		virtual void get_NodeHash(Hash&, Key) const = 0;

		struct Impl;
	};

	class CompactMmr
	{
		// Only used to recalculate the new root hash after appending the element
		// Can't generate proofs.

		uint64_t m_Count;
		std::vector<Hash> m_vNodes; // rightmost branch, in top-down order

	public:

		CompactMmr() :m_Count(0) {}

		void Append(const Hash&);

		void get_Hash(Hash&) const;
		void get_PredictedHash(Hash&, const Hash& hvAppend) const;
	};
}

class StorageManager
{
public:

	// All save methods are guaranteed to be atmoic!

	// Blockchain state entry lifecycle.
	//
	//	1. State reported. Only the basic info received (height, hash, difficulty, etc.). 
	//		If the state is already known - ignore
	//		If the difficulty is past the defined horizon and it has no known following state before the horizon - ignore (drop), but it may be re-requested later.
	//		If state is orphan, i.e. its predecessor is unknown - it's requested, and nothing performed further.
	//		If state is NOT orphan - PoW is requested.
	//	2. PoW received.
	//		If PoW was already received - ignore
	//		If PoW is invalid - the PoW is dropped, peer should be tagged as inadequate, and PoW should be re-requested from other peers (logic with suspending peers, timers).
	//		If PoW is valid - appropriate block is requested.
	//	3. Block received.
	//		If block was already received - ignore
	//		Otherwise sanity is checked: validation of signatures, zero-sum, sort order, compliance to rules w.r.t. emission, treasury. *Existence of source UTXOs is not verified yet*!
	//		If block is invalid - it's dropped, peer should be tagged as inadequate, and block should be re-requested from other peers (logic with suspending peers, timers).
	//		If valid (regardless to the source system state) it's decomposed and saved by components, with additional auxilliary data
	//			Input and Output UTXOs are saved.
	//			TxKernels are saved with search index, and MMR hash structure is built.
	//			Global block parameters are saved (non-encoded blinding offset, and etc.)
	//			MMR structure for the whole blockchain is extended (will grow logarithmically with the height)
	//		The state is tagged as *functional*
	//		If the previous state is not currently present or *reachable* (not all predecessors are *functional*) - no further actions. 
	//		Otherwise this state and all the consequent *functional* states are marked as *reachable*, and the difficulty of reachable *tip* is calculated
	//		If this difficulty is more than the current active difficulty - initiate the TRANSITION to the new tip.
	//	4. Transition.
	//		Transition means going from source state to the destination state, using the UTXOs specified in the block.
	//		Transition can go in both directions (i.e. it's fully reversable).
	//		Valid transition should consume only the existing UTXOs, allowed by the policy (maturity and etc.).
	//		Plus the final system state must match the specified in the state description.
	//		If transition is valid
	//			new system state is appreciated.
	//			The state *active* flag should be set or cleared (depending on the transition direction). All the states included in the current blockchain should have *active* flag.
	//		Otherwise
	//			transition is aborted. Bogus block should be deleted, peer marked as inadequate.
	//			block should be re-requested.
	//		*Note*: Going back should always be possible. Otherwise means the whole system state is corrupted!
	//	5. Going beyond the horizon
	//		Inactive tips beyond the horizon are pruned (deleted completely)
	//		If the state is included in the current blockchain, but far enough (twice the horizon?) - it's compressed
	//	6. Compression
	//		During the compression in/out UTXO data is deleted. Only range proofs are left.
	//		Whenever an active block goes beyond the horizon - it should erase the consumed range proofs from the appropriate compressed blocks.
	//		Kernels and MMR hashes must remain.
	//
	// --------------------------------
	//
	//	Apart from the described blockchain entries there is also a current system state. Call it *Cursor*. It includes the following:
	//		UTXOs that are currently available, each with the description of its originating block
	//		Current Height, Hash, Difficulty.
	//



};

} // namespace beam
