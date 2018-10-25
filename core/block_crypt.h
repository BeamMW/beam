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
#include "ecc_native.h"
#include "merkle.h"

namespace beam
{
    const Height MaxHeight = static_cast<Height>(-1);

	typedef ECC::Hash::Value PeerID;
	typedef uint64_t BbsChannel;
	typedef ECC::Hash::Value BbsMsgID;

	using ECC::Key;

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
		void Inc(Raw&) const;
		void Inc(Raw&, const Raw& base) const;
		void Dec(Raw&, const Raw& base) const;


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

		typedef uintBig_t<(sizeof(Amount) << 4)> uintBig; // 128 bits

		void operator += (const Amount);
		void operator -= (const Amount);
		void operator += (const AmountBig&);
		void operator -= (const AmountBig&);

		void Export(uintBig&) const;
		void AddTo(ECC::Point::Native&) const;
	};

	struct CommitmentAndMaturity
	{
		ECC::Point m_Commitment;
		Height m_Maturity;

		CommitmentAndMaturity() :m_Maturity(0) {}

		int cmp_CaM(const CommitmentAndMaturity&) const;
		int cmp(const CommitmentAndMaturity&) const;
		COMPARISON_VIA_CMP
	};

	struct Rules
	{
		static Rules& get();

		static const Height HeightGenesis; // height of the 1st block, defines the convention. Currently =1
		static const Amount Coin; // how many quantas in a single coin. Just cosmetic, has no meaning to the processing (which is in terms of quantas)

		Amount CoinbaseEmission	= Coin * 80; // the maximum allowed coinbase in a single block
		Height MaturityCoinbase = 60; // 1 hour
		Height MaturityStd		= 0; // not restricted. Can spend even in the block of creation (i.e. spend it before it becomes visible)

		size_t MaxBodySize		= 0x100000; // 1MB

		// timestamp & difficulty. Basically very close to those from bitcoin, except the desired rate is 1 minute (instead of 10 minutes)
		uint32_t DesiredRate_s				= 60; // 1 minute
		uint32_t DifficultyReviewCycle		= 24 * 60; // 1,440 blocks, 1 day roughly
		uint32_t MaxDifficultyChange		= 2; // (x4, same as in bitcoin).
		uint32_t TimestampAheadThreshold_s	= 60 * 60 * 2; // 2 hours. Timestamps ahead by more than 2 hours won't be accepted
		uint32_t WindowForMedian			= 25; // Timestamp for a block must be (strictly) higher than the median of preceding window
		Difficulty StartDifficulty			= Difficulty(2 << Difficulty::s_MantissaBits); // FAST start, good for QA

		bool AllowPublicUtxos = false;
		bool FakePoW = false;
		uint32_t MaxRollbackHeight = 1440; // 1 day roughly
		uint32_t MacroblockGranularity = 720; // i.e. should be created for heights that are multiples of this. This should make it more likely for different nodes to have the same macroblocks

		ECC::Hash::Value Checksum;

		void UpdateChecksum();
		void AdjustDifficulty(Difficulty&, Timestamp tCycleBegin_s, Timestamp tCycleEnd_s) const;
	};

	struct Input
		:public CommitmentAndMaturity
	{
		typedef std::unique_ptr<Input> Ptr;
		typedef uint32_t Count; // the type for count of duplicate UTXOs in the system

		struct State
		{
			Height m_Maturity;
			Input::Count m_Count;

			void get_ID(Merkle::Hash&, const Input&) const;

			template <typename Archive>
			void serialize(Archive& ar)
			{
				ar
					& m_Maturity
					& m_Count;
			}
		};

		struct Proof
		{
			State m_State;
			Merkle::Proof m_Proof;

			template <typename Archive>
			void serialize(Archive& ar)
			{
				ar
					& m_State
					& m_Proof;
			}

			static const uint32_t s_EntriesMax = 20; // if this is the size of the vector - the result is probably trunacted
		};

		int cmp(const Input&) const;
		COMPARISON_VIA_CMP
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
		void Create(ECC::Scalar::Native&, Key::IKdf&, const Key::IDV&);

		bool Recover(Key::IPKdf&, Key::IDV&) const;

		bool IsValid(ECC::Point::Native& comm) const;
		Height get_MinMaturity(Height h) const; // regardless to the explicitly-overridden

		void operator = (const Output&);
		int cmp(const Output&) const;
		COMPARISON_VIA_CMP

	private:
		void CreateInternal(const ECC::Scalar::Native&, Amount, bool bPublic, Key::IKdf*, const Key::ID*);
		void get_SeedKid(ECC::uintBig&, Key::IPKdf&) const;
	};

	inline bool operator < (const Output::Ptr& a, const Output::Ptr& b) { return *a < *b; }

	struct TxKernel
	{
		typedef std::unique_ptr<TxKernel> Ptr;

		// Mandatory
		ECC::Point		m_Excess;
		ECC::Signature	m_Signature;	// For the whole body, including nested kernels
		uint64_t		m_Multiplier;
		Amount			m_Fee;			// can be 0 (for instance for coinbase transactions)
		HeightRange		m_Height;

		TxKernel()
			:m_Multiplier(0) // 0-based, 
			,m_Fee(0)
		{
		}

		struct HashLock
		{
			ECC::uintBig m_Preimage;
		};

		std::unique_ptr<HashLock> m_pHashLock;
		std::vector<Ptr> m_vNested; // nested kernels, included in the signature.

		static const uint32_t s_MaxRecursionDepth = 2;

		static void TestRecursion(uint32_t n)
		{
			if (n > s_MaxRecursionDepth)
				throw std::runtime_error("recursion too deep");
		}

		void get_Hash(Merkle::Hash&, const ECC::Hash::Value* pLockImage = NULL) const; // for signature. Contains all, including the m_Excess and m_Multiplier (i.e. the public key)
		void get_ID(Merkle::Hash&, const ECC::Hash::Value* pLockImage = NULL) const; // unique kernel identifier in the system.

		bool IsValid(AmountBig& fee, ECC::Point::Native& exc) const;

		void operator = (const TxKernel&);
		int cmp(const TxKernel&) const;
		COMPARISON_VIA_CMP

	private:
		bool Traverse(ECC::Hash::Value&, AmountBig*, ECC::Point::Native*, const TxKernel* pParent, const ECC::Hash::Value* pLockImage) const;
		void HashToID(Merkle::Hash& hv) const;
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

			void Compare(IReader&& rOther, bool& bICover, bool& bOtherCovers);
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
		COMPARISON_VIA_CMP

		bool IsValid(Context&) const; // Explicit fees are considered "lost" in the transactions (i.e. would be collected by the miner)

		static const uint32_t s_KeyBits = ECC::nBits; // key len for map of transactions. Can actually be less than 256 bits.
		typedef uintBig_t<s_KeyBits> KeyType;

		void get_Key(KeyType&) const;
	};

	struct Block
	{
		// Different parts of the block are split into different structs, so that they can be manipulated (transferred, processed, saved and etc.) independently
		// For instance, there is no need to keep PoW (at least in SPV client) once it has been validated.

		struct PoW
		{
			// equihash parameters. 
			// Parameters recommended by BTG are 144/5, to make it asic-resistant (~1GB average, spikes about 1.5GB). On CPU solve time about 1 minutes
			// The following are the parameters for testnet, to make it of similar size, and much faster solve time, to test concurrency and difficulty adjustment
			static const uint32_t N = 120;
			static const uint32_t K = 5;

			static const uint32_t nNumIndices		= 1 << K; // 32
			static const uint32_t nBitsPerIndex		= N / (K + 1) + 1; // 21

			static const uint32_t nSolutionBits		= nNumIndices * nBitsPerIndex; // 672 bits

			static_assert(!(nSolutionBits & 7), "PoW solution should be byte-aligned");
			static const uint32_t nSolutionBytes	= nSolutionBits >> 3; // 84 bytes

			std::array<uint8_t, nSolutionBytes>	m_Indices;

			typedef uintBig_t<64> NonceType;
			NonceType m_Nonce; // 8 bytes. The overall solution size is 96 bytes.
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
				COMPARISON_VIA_CMP
			};

			struct Sequence
			{
				struct Prefix {
					Height				m_Height;
					Merkle::Hash		m_Prev;			// explicit referebce to prev
					Difficulty::Raw		m_ChainWork;
				};

				struct Element
				{
					Merkle::Hash	m_Definition; // Defined as Hash[ History | Hash[Utxos | Kernels] ]
					Timestamp		m_TimeStamp;
					PoW				m_PoW;

					// The following not only interprets the proof, but also verifies the knwon part of its structure.
					bool IsValidProofUtxo(const Input&, const Input::Proof&) const;
					bool IsValidProofKernel(const TxKernel&, const Merkle::Proof&) const;
				};
			};

			struct Full
				:public Sequence::Prefix
				,public Sequence::Element
			{
				void NextPrefix();

				void get_HashForPoW(Merkle::Hash&) const; // all except PoW
				void get_Hash(Merkle::Hash&) const; // all

				void get_ID(ID&) const;

				bool IsSane() const;
				bool IsValidPoW() const;
				bool GeneratePoW(const PoW::Cancel& = [](bool) { return false; });

				// the most robust proof verification - verifies the whole proof structure
				bool IsValidProofState(const ID&, const Merkle::HardProof&) const;

			private:
				void get_HashInternal(Merkle::Hash&, bool bTotal) const;
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

		struct ChainWorkProof;
	};

	void ExtractOffset(ECC::Scalar::Native& kKernel, ECC::Scalar::Native& kOffset, Height = 0, uint32_t nIdx = 0);

	std::ostream& operator << (std::ostream&, const Block::SystemState::ID&);


	class TxBase::Context
	{
		bool ShouldVerify(uint32_t& iV) const;
		bool ShouldAbort() const;

		bool HandleElementHeight(const HeightRange&);

	public:
		// Tests the validity of all the components, overall arithmetics, and the lexicographical order of the components.
		// Determines the min/max block height that the transaction can fit, wrt component heights and maturity policies
		// Does *not* check the existence of the input UTXOs
		//
		// Validation formula
		//
		// Sum(Input UTXOs) + Sum(Input Kernels.Excess) = Sum(Output UTXOs) + Sum(Output Kernels.Excess) + m_Offset*G [ + Sum(Fee)*H ]
		//
		// For transaction validation fees are considered as implicit outputs (i.e. Sum(Fee)*H should be added for the right equation side)
		//
		// For a block validation Fees are not accounted for, since they are consumed by new outputs injected by the miner.
		// However Each block contains extra outputs (coinbase) for block closure, which should be subtracted from the outputs for sum validation.
		//
		// Define: Sigma = Sum(Output UTXOs) - Sum(Input UTXOs) + Sum(Output Kernels.Excess) - Sum(Input Kernels.Excess) + m_Offset*G
		// In other words Sigma = <all outputs> - <all inputs>
		// Sigma is either zero or -Sum(Fee)*H, depending on what we validate


		ECC::Point::Native m_Sigma;

		AmountBig m_Fee;
		AmountBig m_Coinbase;
		HeightRange m_Height;

		bool m_bBlockMode; // in 'block' mode the hMin/hMax on input denote the range of heights. Each element is verified wrt it independently.
		// i.e. different elements may have non-overlapping valid range, and it's valid.
		// Suitable for merged block validation

		// for multi-tasking, parallel verification
		uint32_t m_nVerifiers;
		uint32_t m_iVerifier;
		volatile bool* m_pAbort;

		Context() { Reset(); }
		void Reset();

		bool ValidateAndSummarize(const TxBase&, IReader&&);
		bool Merge(const Context&);

		// hi-level functions, should be used after all parts were validated and merged
		bool IsValidTransaction();
		bool IsValidBlock(const Block::BodyBase&, bool bSubsidyOpen);
	};

	class Block::BodyBase::RW
		:public Block::BodyBase::IMacroReader
		,public Block::BodyBase::IMacroWriter
	{

	public:

#define MBLOCK_DATA_Types(macro) \
		macro(hd) \
		macro(ui) \
		macro(uo) \
		macro(ki) \
		macro(ko)

		struct Type
		{
			enum Enum {
#define THE_MACRO(x) x,
				MBLOCK_DATA_Types(THE_MACRO)
#undef THE_MACRO
				count
			};
		};

		static const char* const s_pszSufix[Type::count];

	private:

		std::FStream m_pS[Type::count];

		Input::Ptr m_pGuardUtxoIn[2];
		Output::Ptr m_pGuardUtxoOut[2];
		TxKernel::Ptr m_pGuardKernelIn[2];
		TxKernel::Ptr m_pGuardKernelOut[2];

		template <typename T>
		void LoadInternal(const T*& pPtr, int, typename T::Ptr* ppGuard);

		template <typename T>
		void WriteInternal(const T&, int);

		bool OpenInternal(int iData);
		void PostOpen(int iData);
		void Open(bool bRead);

	public:

		RW() :m_bAutoDelete(false) {}
		~RW();

		// do not modify between Open() and Close()
		bool m_bRead;
		bool m_bAutoDelete;
		std::string m_sPath;
		Merkle::Hash m_hvContentTag; // needed to make sure all the files indeed belong to the same data set

		void GetPath(std::string&, int iData) const;

		void ROpen();
		void WCreate();

		void Flush();
		void Close();
		void Delete(); // must be closed

		// IReader
		virtual void Clone(Ptr&) override;
		virtual void Reset() override;
		virtual void NextUtxoIn() override;
		virtual void NextUtxoOut() override;
		virtual void NextKernelIn() override;
		virtual void NextKernelOut() override;
		// IMacroReader
		virtual void get_Start(BodyBase&, SystemState::Sequence::Prefix&) override;
		virtual bool get_NextHdr(SystemState::Sequence::Element&) override;
		// IWriter
		virtual void WriteIn(const Input&) override;
		virtual void WriteIn(const TxKernel&) override;
		virtual void WriteOut(const Output&) override;
		virtual void WriteOut(const TxKernel&) override;
		// IMacroWriter
		virtual void put_Start(const BodyBase&, const SystemState::Sequence::Prefix&) override;
		virtual void put_NextHdr(const SystemState::Sequence::Element&) override;
	};

	struct Block::ChainWorkProof
	{
		// Compressed consecutive states (likely to appear at the end)
		struct Heading {
			SystemState::Sequence::Prefix m_Prefix;
			std::vector<SystemState::Sequence::Element> m_vElements;
		} m_Heading;
		// other states
		std::vector<SystemState::Full> m_vArbitraryStates;
		// compressed proof
		Merkle::MultiProof m_Proof;
		// last node to go from History to Definition.
		Merkle::Hash m_hvRootLive;
		// crop thereshold. Off by default
		Difficulty::Raw m_LowerBound;

		struct ISource
		{
			virtual void get_StateAt(SystemState::Full&, const Difficulty::Raw&) = 0;
			virtual void get_Proof(Merkle::IProofBuilder&, Height) = 0;
		};

		ChainWorkProof()
		{
			ZeroInit();
		}

		void Reset();
		void Create(ISource&, const SystemState::Full& sRoot);
		bool IsValid(Block::SystemState::Full* pTip = NULL) const;
		bool Crop(); // according to current bound
		bool Crop(const ChainWorkProof& src);
		bool IsEmpty() const { return m_Heading.m_vElements.empty(); }

		template <typename Archive>
		void serialize(Archive& ar)
		{
			ar
				& m_Heading.m_Prefix
				& m_Heading.m_vElements
				& m_vArbitraryStates
				& m_Proof
				& m_hvRootLive
				& m_LowerBound;
		}

	private:
		struct Sampler;
		bool IsValidInternal(size_t& iState, size_t& iHash, const Difficulty::Raw& lowerBound, Block::SystemState::Full* pTip) const;
		void ZeroInit();
	};

}
