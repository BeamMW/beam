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

#include <vector>
#include <array>
#include <list>
#include <map>
#include <utility>
#include <cstdint>
#include <memory>
#include <assert.h>
#include <functional>

#ifdef WIN32
#	include <winsock2.h>
#endif // WIN32

#ifndef verify
#	ifdef  NDEBUG
#		define verify(x) ((void)(x))
#	else //  NDEBUG
#		define verify(x) assert(x)
#	endif //  NDEBUG
#endif // verify

#define IMPLEMENT_GET_PARENT_OBJ(parent_class, this_var) \
	parent_class& get_ParentObj() const { \
		parent_class*  p = (parent_class*) (((uint8_t*) this) + 1 - (uint8_t*) (&((parent_class*) 1)->this_var)); \
		assert(this == &p->this_var); /* this also tests that the variable of the correct type */ \
		return *p; \
	}

#include "ecc.h"
#include <iostream>
#include <fstream>

namespace std
{
	void ThrowIoError();
	void TestNoError(const ios& obj);

	// wrapper for std::fstream, with semantics suitable for serialization
	class FStream
	{
		std::fstream m_F;
		uint64_t m_Remaining; // used in read-stream, to indicate the EOF before trying to deserialize something

		static void NotImpl();

	public:

		bool Open(const char*, bool bRead, bool bStrict = false); // strict - throw exc if error
		void Close();
		bool IsDataRemaining() const;
		void Restart(); // for read-stream - jump to the beginning of the file

		// read/write always return the size requested. Exception is thrown if underflow or error
		size_t read(void* pPtr, size_t nSize);
		size_t write(const void* pPtr, size_t nSize);
		void Flush();

		char getch();
		char peekch() const;
		void ungetch(char);
	};
}

namespace beam
{
	// sorry for replacing 'using' by 'typedefs', some compilers don't support it
	typedef uint64_t Timestamp;
	typedef uint64_t Height;
    const Height MaxHeight = static_cast<Height>(-1);
	typedef std::vector<uint8_t> ByteBuffer;
	typedef ECC::Amount Amount;
	typedef ECC::Hash::Value PeerID;
	typedef uint64_t BbsChannel;
	typedef ECC::Hash::Value BbsMsgID;

	Timestamp getTimestamp();
	uint32_t GetTime_ms(); // platform-independent GetTickCount
	uint32_t GetTimeNnz_ms(); // guaranteed non-zero

	struct Difficulty
	{
		uint32_t m_Packed;
		static const uint32_t s_MantissaBits = 24;

		Difficulty(uint32_t d = 0) :m_Packed(d) {}

		typedef ECC::uintBig Raw;

		// maximum theoretical difficulty value, which corresponds to 'infinite' (only Zero hash value meet the target).
		// Corresponds to 0xffff...fff raw value.
		static const uint32_t s_MaxOrder = Raw::nBits - s_MantissaBits - 1;
		static const uint32_t s_Inf = (s_MaxOrder + 1) << s_MantissaBits;

		bool IsTargetReached(const ECC::uintBig&) const;

		void Unpack(Raw&) const;

		void Unpack(uint32_t& order, uint32_t& mantissa) const;
		void Pack(uint32_t order, uint32_t mantissa);

		void Adjust(uint32_t src, uint32_t trg, uint32_t nMaxOrderChange);

	private:
		static void Adjust(uint32_t src, uint32_t trg, uint32_t nMaxOrderChange, uint32_t& order, uint32_t& mantissa);
	};

	std::ostream& operator << (std::ostream&, const Difficulty&);

	struct HeightRange
	{
		// Convention: inclusive, i.e. both endings are part of the range.
		Height m_Min;
		Height m_Max;

		HeightRange() {
			Reset();
		}

		HeightRange(Height h0, Height h1) {
			m_Min = h0;
			m_Max = h1;
		}

		HeightRange(Height h) {
			m_Min = m_Max = h;
		}

		void Reset();
		void Intersect(const HeightRange&);

		bool IsEmpty() const;
		bool IsInRange(Height) const;
		bool IsInRangeRelative(Height) const; // assuming m_Min was already subtracted
	};

	struct AmountBig
	{
		Amount Lo;
		Amount Hi;

		void operator += (const Amount);
		void operator -= (const Amount);
		void operator += (const AmountBig&);
		void operator -= (const AmountBig&);

		void Export(ECC::uintBig&) const;
		void AddTo(ECC::Point::Native&) const;
	};

	namespace Merkle
	{
		typedef ECC::Hash::Value Hash;
		typedef std::pair<bool, Hash>	Node;
		typedef std::vector<Node>		Proof;

		void Interpret(Hash&, const Proof&);
		void Interpret(Hash&, const Node&);
		void Interpret(Hash&, const Hash& hLeft, const Hash& hRight);
		void Interpret(Hash&, const Hash& hNew, bool bNewOnRight);
	}

	struct CommitmentAndMaturity
	{
		ECC::Point m_Commitment;
		Height m_Maturity;

		CommitmentAndMaturity() :m_Maturity(0) {}

		int cmp_CaM(const CommitmentAndMaturity&) const;
		int cmp(const CommitmentAndMaturity&) const;
		COMPARISON_VIA_CMP(CommitmentAndMaturity)
	};

	struct Rules
	{
		static Rules& get();

		static const Height HeightGenesis; // height of the 1st block, defines the convention. Currently =1
		static const Amount Coin; // how many quantas in a single coin. Just cosmetic, has no meaning to the processing (which is in terms of quantas)

		Amount CoinbaseEmission	= Coin * 40; // the maximum allowed coinbase in a single block
		Height MaturityCoinbase = 60; // 1 hour
		Height MaturityStd		= 0; // not restricted. Can spend even in the block of creation (i.e. spend it before it becomes visible)

		size_t MaxBodySize		= 0x100000; // 1MB

		// timestamp & difficulty. Basically very close to those from bitcoin, except the desired rate is 1 minute (instead of 10 minutes)
		uint32_t DesiredRate_s				= 60; // 1 minute
		uint32_t DifficultyReviewCycle		= 24 * 60; // 1,440 blocks, 1 day roughly
		uint32_t MaxDifficultyChange		= 2; // (x4, same as in bitcoin).
		uint32_t TimestampAheadThreshold_s	= 60 * 60 * 2; // 2 hours. Timestamps ahead by more than 2 hours won't be accepted
		uint32_t WindowForMedian			= 25; // Timestamp for a block must be (strictly) higher than the median of preceding window

		bool FakePoW = false;

		ECC::Hash::Value Checksum;

		void UpdateChecksum();
		void AdjustDifficulty(Difficulty&, Timestamp tCycleBegin_s, Timestamp tCycleEnd_s) const;
	};

	struct Input
		:public CommitmentAndMaturity
	{
		typedef std::unique_ptr<Input> Ptr;
		typedef uint32_t Count; // the type for count of duplicate UTXOs in the system

		struct Proof
		{
			Height m_Maturity;
			Input::Count m_Count;
			Merkle::Proof m_Proof;

			bool IsValid(const Input&, const Merkle::Hash& root) const;

			template <typename Archive>
			void serialize(Archive& ar)
			{
				ar
					& m_Maturity
					& m_Count
					& m_Proof;
			}

			static const uint32_t s_EntriesMax = 20; // if this is the size of the vector - the result is probably trunacted
		};

		int cmp(const Input&) const;
		COMPARISON_VIA_CMP(Input)
	};

	inline bool operator < (const Input::Ptr& a, const Input::Ptr& b) { return *a < *b; }

	struct Output
		:public CommitmentAndMaturity
	{
		typedef std::unique_ptr<Output> Ptr;

		bool		m_Coinbase;
		Height		m_Incubation; // # of blocks before it's mature

		Output()
			:m_Coinbase(false)
			,m_Incubation(0)
		{
		}

		static const Amount s_MinimumValue = 1;

		// one of the following *must* be specified
		std::unique_ptr<ECC::RangeProof::Confidential>	m_pConfidential;
		std::unique_ptr<ECC::RangeProof::Public>		m_pPublic;

		void Create(const ECC::Scalar::Native&, Amount, bool bPublic = false);
		bool IsValid(ECC::Point::Native& comm) const;
		Height get_MinMaturity(Height h) const; // regardless to the explicitly-overridden

		void operator = (const Output&);
		int cmp(const Output&) const;
		COMPARISON_VIA_CMP(Output)
	};

	inline bool operator < (const Output::Ptr& a, const Output::Ptr& b) { return *a < *b; }

	struct TxKernel
	{
		typedef std::unique_ptr<TxKernel> Ptr;

		// Mandatory
		ECC::Point		m_Excess;
		ECC::Signature	m_Signature;	// For the whole tx body, including nested kernels, excluding contract signature
		uint64_t		m_Multiplier;
		Amount			m_Fee;			// can be 0 (for instance for coinbase transactions)
		HeightRange		m_Height;

		TxKernel()
			:m_Multiplier(0) // 0-based, 
			,m_Fee(0)
		{
		}

		// Optional
		struct Contract
		{
			ECC::Hash::Value	m_Msg;
			ECC::Point			m_PublicKey;
			ECC::Signature		m_Signature;

			int cmp(const Contract&) const;
			COMPARISON_VIA_CMP(Contract)
		};

		struct HashLock
		{
			ECC::Hash::Value	m_Hash;
			ECC::uintBig		m_Preimage;
		};

		std::unique_ptr<Contract> m_pContract;
		std::unique_ptr<HashLock> m_pHashLock;
		std::vector<Ptr> m_vNested; // nested kernels, included in the signature.

		static const uint32_t s_MaxRecursionDepth = 2;

		static void TestRecursion(uint32_t n)
		{
			if (n > s_MaxRecursionDepth)
				throw std::runtime_error("recursion too deep");
		}

		void get_HashForSigning(Merkle::Hash&) const; // Includes the contents, but not the excess and the signature
		void get_HashForContract(ECC::Hash::Value&, const ECC::Hash::Value& msg) const;
		void get_HashTotal(Merkle::Hash&) const; // Includes everything. 

		bool IsValid(AmountBig& fee, ECC::Point::Native& exc) const;
		bool IsValidProof(const Merkle::Proof&, const Merkle::Hash& root) const;

		void operator = (const TxKernel&);
		int cmp(const TxKernel&) const;
		COMPARISON_VIA_CMP(TxKernel)

	private:
		bool Traverse(ECC::Hash::Value&, AmountBig*, ECC::Point::Native*, const TxKernel* pParent) const;
		void HashForSigningToTotal(Merkle::Hash& hv) const;
	};

	inline bool operator < (const TxKernel::Ptr& a, const TxKernel::Ptr& b) { return *a < *b; }

	struct TxBase
	{
		class Context;

		struct IReader
		{
			typedef std::unique_ptr<IReader> Ptr;

			// during iterations those pointers are guaranteed to be valid during at least 1 consequent iteration
			const Input* m_pUtxoIn;
			const Output* m_pUtxoOut;
			const TxKernel* m_pKernelIn;
			const TxKernel* m_pKernelOut;

			virtual void Clone(Ptr&) = 0;
			virtual void Reset() = 0;
			// For all the following methods: the returned pointer should be valid during at least 2 consequent calls!
			virtual void NextUtxoIn() = 0;
			virtual void NextUtxoOut() = 0;
			virtual void NextKernelIn() = 0;
			virtual void NextKernelOut() = 0;
		};

		struct IWriter
		{
			virtual void WriteIn(const Input&) = 0;
			virtual void WriteIn(const TxKernel&) = 0;
			virtual void WriteOut(const Output&) = 0;
			virtual void WriteOut(const TxKernel&) = 0;

			void Dump(IReader&&);
			bool Combine(IReader** ppR, int nR, const volatile bool& bStop); // combine consequent blocks, merge-sort and delete consumed outputs
			// returns false if aborted
			bool Combine(IReader&& r0, IReader&& r1, const volatile bool& bStop);
		};


		ECC::Scalar m_Offset;
	};

	struct TxVectors
	{
		std::vector<Input::Ptr> m_vInputs;
		std::vector<Output::Ptr> m_vOutputs;
		std::vector<TxKernel::Ptr> m_vKernelsInput;
		std::vector<TxKernel::Ptr> m_vKernelsOutput;

		void Sort(); // w.r.t. the standard
		size_t DeleteIntermediateOutputs(); // assumed to be already sorted. Retruns the num deleted

		void TestNoNulls() const; // valid object should not have NULL members. Should be used during (de)serialization

		class Reader :public TxBase::IReader {
			size_t m_pIdx[4];
		public:
			const TxVectors& m_Txv;
			Reader(const TxVectors& txv) :m_Txv(txv) {}
			// IReader
			virtual void Clone(Ptr&) override;
			virtual void Reset() override;
			virtual void NextUtxoIn() override;
			virtual void NextUtxoOut() override;
			virtual void NextKernelIn() override;
			virtual void NextKernelOut() override;
		};

		Reader get_Reader() const {
			return Reader(*this);
		}

		struct Writer :public TxBase::IWriter
		{
			TxVectors& m_Txv;
			Writer(TxVectors& txv) :m_Txv(txv) {}

			virtual void WriteIn(const Input&) override;
			virtual void WriteIn(const TxKernel&) override;
			virtual void WriteOut(const Output&) override;
			virtual void WriteOut(const TxKernel&) override;
		};
	};

	struct Transaction
		:public TxBase
		,public TxVectors
	{
		typedef std::shared_ptr<Transaction> Ptr;

		int cmp(const Transaction&) const;
		COMPARISON_VIA_CMP(Transaction)

		bool IsValid(Context&) const; // Explicit fees are considered "lost" in the transactions (i.e. would be collected by the miner)

		static const uint32_t s_KeyBits = ECC::nBits; // key len for map of transactions. Can actually be less than 256 bits.
		typedef ECC::uintBig_t<s_KeyBits> KeyType;

		void get_Key(KeyType&) const;
	};

	struct Block
	{
		// Different parts of the block are split into different structs, so that they can be manipulated (transferred, processed, saved and etc.) independently
		// For instance, there is no need to keep PoW (at least in SPV client) once it has been validated.

		struct PoW
		{
			// equihash parameters
			static const uint32_t N = 120;
			static const uint32_t K = 4;

			static const uint32_t nNumIndices		= 1 << K; // 16
			static const uint32_t nBitsPerIndex		= N / (K + 1) + 1; // 25

			static const uint32_t nSolutionBits		= nNumIndices * nBitsPerIndex; // 400 bits

			static_assert(!(nSolutionBits & 7), "PoW solution should be byte-aligned");
			static const uint32_t nSolutionBytes	= nSolutionBits >> 3; // 50 bytes

			std::array<uint8_t, nSolutionBytes>	m_Indices;

			typedef ECC::uintBig_t<88> NonceType;
			NonceType m_Nonce; // 11 bytes. The overall solution size is 64 bytes.
			Difficulty m_Difficulty;

			bool IsValid(const void* pInput, uint32_t nSizeInput) const;

			using Cancel = std::function<bool(bool bRetrying)>;
			// Difficulty and Nonce must be initialized. During the solution it's incremented each time by 1.
			// returns false only if cancelled
			bool Solve(const void* pInput, uint32_t nSizeInput, const Cancel& = [](bool) { return false; });

		private:
			struct Helper;
		};

		struct SystemState
		{
			struct ID {
				Merkle::Hash	m_Hash; // explained later
				Height			m_Height;

				int cmp(const ID&) const;
				COMPARISON_VIA_CMP(ID)
			};

			struct Sequence
			{
				struct Prefix {
					Height			m_Height;
					Merkle::Hash	m_Prev;			// explicit referebce to prev
				};

				struct Element {
					Merkle::Hash	m_Definition;	// defined as H ( PrevStates | LiveObjects )
					Timestamp		m_TimeStamp;
					PoW				m_PoW;
				};
			};

			struct Full
				:public Sequence::Prefix
				,public Sequence::Element
			{
				void Set(Prefix&, const Element&);
				void get_Hash(Merkle::Hash&) const; // Calculated from all the above
				void get_ID(ID&) const;

				bool IsSane() const;
				bool IsValidPoW() const;
				bool GeneratePoW(const PoW::Cancel& = [](bool) { return false; });
			};
		};

		struct BodyBase
			:public TxBase
		{
			AmountBig m_Subsidy; // the overall amount created by the block
								 // For standard blocks this should be equal to the coinbase emission.
								 // Genesis block(s) may have higher emission (aka premined)

			bool m_SubsidyClosing; // Last block that contains arbitrary subsidy.

			void ZeroInit();

			// Test the following:
			//		Validity of all the components, and overall arithmetics, whereas explicit fees are already collected by extra UTXO(s) put by the miner
			//		All components are specified in a lexicographical order, to conceal the actual transaction graph
			//		Liquidity of the components wrt height and maturity policies
			// Not tested by this function (but should be tested by nodes!)
			//		Existence of all the input UTXOs
			//		Existence of the coinbase non-confidential output UTXO, with the sum amount equal to the new coin emission.
			bool IsValid(const HeightRange&, bool bSubsidyOpen, TxBase::IReader&&) const;

			struct IMacroReader
				:public IReader
			{
				virtual void get_Start(BodyBase&, SystemState::Sequence::Prefix&) = 0;
				virtual bool get_NextHdr(SystemState::Sequence::Element&) = 0;
			};

			struct IMacroWriter
				:public IWriter
			{
				virtual void put_Start(const BodyBase&, const SystemState::Sequence::Prefix&) = 0;
				virtual void put_NextHdr(const SystemState::Sequence::Element&) = 0;

				bool CombineHdr(IMacroReader&& r0, IMacroReader&& r1, const volatile bool& bStop);
			};

			void Merge(const BodyBase& next);

			// suitable for big (compressed) blocks
			class RW;
		};

		struct Body
			:public BodyBase
			,public TxVectors
		{
			bool IsValid(const HeightRange& hr, bool bSubsidyOpen) const
			{
				return BodyBase::IsValid(hr, bSubsidyOpen, get_Reader());
			}
		};
	};

	enum struct KeyType
	{
		Comission,
		Coinbase,
		Kernel,
		Regular,
		Identity,
		SChannelNonce
	};
	void DeriveKey(ECC::Scalar::Native&, const ECC::Kdf&, Height, KeyType, uint32_t nIdx = 0);
	void ExtractOffset(ECC::Scalar::Native& kKernel, ECC::Scalar::Native& kOffset, Height = 0, uint32_t nIdx = 0);

	std::ostream& operator << (std::ostream&, const Block::SystemState::ID&);

} // namespace beam
