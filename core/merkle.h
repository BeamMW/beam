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
#include "ecc.h"

namespace beam {
namespace Merkle {

	typedef ECC::Hash::Value Hash;
	typedef std::pair<bool, Hash>	Node;
	typedef std::vector<Node>		Proof;
	typedef std::vector<Hash>		HardProof;

	struct Position {
		uint8_t H;
		uint64_t X;
	};

	struct IProofBuilder {
		virtual bool AppendNode(const Node&, const Position&) = 0;
	};

	struct ProofBuilderStd
		:public IProofBuilder
	{
		Proof m_Proof;

		virtual bool AppendNode(const Node& n, const Position&) override
		{
			m_Proof.push_back(n);
			return true;
		}
	};

	struct ProofBuilderHard
		:public IProofBuilder
	{
		HardProof m_Proof;

		virtual bool AppendNode(const Node& n, const Position&) override
		{
			m_Proof.push_back(n.second);
			return true;
		}
	};

	void Interpret(Hash&, const Proof&);
	void Interpret(Hash&, const Node&);
	void Interpret(Hash&, const Hash& hLeft, const Hash& hRight);
	void Interpret(Hash&, const Hash& hNew, bool bNewOnRight);

	struct Mmr
	{
		uint64_t m_Count;
		Mmr() :m_Count(0) {}

		void Append(const Hash&);

		void get_Hash(Hash&) const;
		void get_PredictedHash(Hash&, const Hash& hvAppend) const;

		bool get_Proof(IProofBuilder&, uint64_t i) const;
		void get_Proof(Proof&, uint64_t i) const;

	protected:
		bool get_HashForRange(Hash&, uint64_t n0, uint64_t n) const;

		virtual void LoadElement(Hash&, const Position&) const = 0;
		virtual void SaveElement(const Hash&, const Position&) = 0;
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
		void get_Proof(IProofBuilder&, uint64_t i) const;
		void get_PredictedHash(Hash&, const Hash& hvAppend) const;

	protected:
		// Get the data of the node referenced by Key. The data of this node will only be used until this function is called for another node.
		virtual const void* get_NodeData(Key) const = 0;
		virtual void get_NodeHash(Hash&, Key) const = 0;

		struct Impl;
	};

	struct CompactMmr
	{
		// Only used to recalculate the new root hash after appending the element
		// Can't generate proofs.

		uint64_t m_Count;
		std::vector<Hash> m_vNodes; // rightmost branch, in top-down order

		CompactMmr() :m_Count(0) {}

		void Append(const Hash&);

		void get_Hash(Hash&) const;
		void get_PredictedHash(Hash&, const Hash& hvAppend) const;
	};

	// A variant where the max number of elements is known in advance. All hashes are stored in a flat array.
	class FixedMmmr
		:public Mmr
	{
		std::vector<Hash> m_vHashes;
		uint64_t m_Total;

		uint64_t Pos2Idx(const Position& pos) const;

	public:
		FixedMmmr(uint64_t nTotal = 0) { Reset(nTotal); }
		void Reset(uint64_t nTotal);
	protected:
		// Mmr
		virtual void LoadElement(Hash& hv, const Position& pos) const override;
		virtual void SaveElement(const Hash& hv, const Position& pos) override;
	};

	// On-the-fly hash or proof calculation, without storing extra elements. They are all calculated internally during every invocation.
	// Applicable when used rarely, and you want to avoid extra mem allocation
	class FlyMmr
	{
		struct Inner;
	public:

		uint64_t m_Count;
		FlyMmr(uint64_t nCount = 0) :m_Count(nCount) {}

		void get_Hash(Hash&) const;
		bool get_Proof(IProofBuilder&, uint64_t i) const;

	protected:
		virtual void LoadElement(Hash& hv, uint64_t n) const = 0;
	};

	// Structure to effective encode proofs to multiple elements at-once.
	// The elements must be specified in a sorter order (straight or reverse).
	// All the proofs are "merged", so that no hash is added twice.
	// There still exists a better encoding, where some proof elements can be constructed completely from other elements, but it'd be more complex and require more memory during operation.
	// In addition - this encoding can easily be cropped, if we decide to cut-off the included elements sequence.
	struct MultiProof
	{
		std::vector<Hash> m_vData; // all together

		template <typename Archive>
		void serialize(Archive& ar)
		{
			ar
				& m_vData;
		}

		typedef std::vector<Hash>::const_iterator Iterator;

		class Builder
			:private IProofBuilder
		{
			MultiProof& m_This;
			std::vector<Position> m_vLast;
			std::vector<Position> m_vLastRev;

			virtual bool AppendNode(const Node& n, const Position& pos) override;
			virtual void get_Proof(IProofBuilder&, uint64_t i) = 0;

		public:
			bool m_bSkipSibling;

			Builder(MultiProof& x);
			void Add(uint64_t i);
		};

		class Verifier
			:private IProofBuilder
			,private Mmr
		{
			struct MyNode {
				Hash m_hv; // correct value at this position
				Position m_Pos;
			};

			Iterator m_itPos;
			Iterator m_itEnd;
			std::vector<MyNode> m_vLast;
			std::vector<MyNode> m_vLastRev;

			virtual bool AppendNode(const Node& n, const Position& pos) override;
			virtual void LoadElement(Hash&, const Position&) const override {}
			virtual void SaveElement(const Hash&, const Position&) override {}

			virtual bool IsRootValid(const Hash&) = 0;

		public:
			Hash m_hvPos;
			const Hash* m_phvSibling;
			bool m_bVerify; // in/out. Set to true to verify vs root hash, would be reset to false upon error. Set to false to use in "crop" mode.

			Verifier(const MultiProof& x, uint64_t nCount);
			void Process(uint64_t i);

			// for cropping
			Iterator get_Pos() const { return m_itPos; }
		};
	};

} // namespace Merkle
} // namespace beam
