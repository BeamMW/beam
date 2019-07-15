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
#include <limits>
#include "ecc_native.h"
#include "merkle.h"
#include "difficulty.h"

namespace beam
{
	class IExternalPOW;

	const Height MaxHeight = std::numeric_limits<Height>::max();

	typedef ECC::Hash::Value PeerID;
	typedef uint64_t BbsChannel;
	typedef ECC::Hash::Value BbsMsgID;
	typedef PeerID AssetID;
	typedef uint64_t TxoID;

	using ECC::Key;

	namespace MasterKey
	{
		Key::IKdf::Ptr get_Child(Key::IKdf&, Key::Index);
		Key::IKdf::Ptr get_Child(const Key::IKdf::Ptr&, const Key::IDV&);
	}

	Timestamp getTimestamp();
	uint32_t GetTime_ms(); // platform-independent GetTickCount
	uint32_t GetTimeNnz_ms(); // guaranteed non-zero

	void HeightAdd(Height& trg, Height val); // saturates if overflow

	struct HeightRange
	{
		// Convention: inclusive, i.e. both endings are part of the range.
		// m_Min == m_Max means the range includes a single height. Therefore (m_Min > m_Max) is NOT invalid, it just denotes an empty range.
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

	namespace AmountBig
	{

		typedef uintBig_t<sizeof(Amount) + sizeof(Height)> Type; // 128 bits
		Amount get_Lo(const Type&);
		Amount get_Hi(const Type&);

		void AddTo(ECC::Point::Native&, const Type&);
	};

	typedef int64_t AmountSigned;
	static_assert(sizeof(Amount) == sizeof(AmountSigned), "");

	struct HeightHash
	{
		Merkle::Hash	m_Hash;
		Height			m_Height;

		int cmp(const HeightHash&) const;
		COMPARISON_VIA_CMP
	};

	struct Rules
	{
		Rules();
		static Rules& get();

		static const Height HeightGenesis; // height of the 1st block, defines the convention. Currently =1
		static const Amount Coin; // how many quantas in a single coin. Just cosmetic, has no meaning to the processing (which is in terms of quantas)

		struct {
			// emission parameters
			Amount Value0	= Coin * 80; // Initial emission. Each drop it will be halved. In case of odd num it's rounded to the lower value.
			Height Drop0	= 1440 * 365; // 1 year roughly. This is the height of the last block that still has the initial emission, the drop is starting from the next block
			Height Drop1	= 1440 * 365 * 4; // 4 years roughly. Each such a cycle there's a new drop
		} Emission;

		struct {
			Height Coinbase	= 240; // 4 hours
			Height Std		= 0; // not restricted. Can spend even in the block of creation (i.e. spend it before it becomes visible)
		} Maturity;

		struct {
			// timestamp & difficulty.
			uint32_t Target_s		= 60; // 1 minute
			uint32_t WindowWork		= 120; // 2 hours roughly (under normal operation)
			uint32_t MaxAhead_s		= 60 * 15; // 15 minutes. Timestamps ahead by more than 15 minutes won't be accepted
			uint32_t WindowMedian0	= 25; // Timestamp for a block must be (strictly) higher than the median of preceding window
			uint32_t WindowMedian1	= 7; // Num of blocks taken at both endings of WindowWork, to pick medians.
			Difficulty Difficulty0	= Difficulty(22 << Difficulty::s_MantissaBits); // 2^22 = 4,194,304. For GPUs producing 7 sol/sec this is roughly equivalent to 10K GPUs.

			struct {
				// damp factor. Adjustment of actual dt toward expected, effectively dampens
				uint32_t M = 1; // Multiplier of the actual dt
				uint32_t N = 3; // Denominator. The goal is multiplied by (N-M)
			} Damp;
		} DA;

		struct {
			bool Enabled = false;
			bool Deposit = true; // CA emission in exchage for beams. If not specified - the emission is free
		} CA;

		struct {
			uint32_t MaxRollback = 1440; // 1 day roughly
			uint32_t Granularity = 720; // i.e. should be created for heights that are multiples of this. This should make it more likely for different nodes to have the same macroblocks
		} Macroblock;

		size_t MaxBodySize = 0x100000; // 1MB

		bool AllowPublicUtxos = false;
		bool FakePoW = false;

		ECC::Hash::Value Prehistoric; // Prev hash of the 1st block
		ECC::Hash::Value TreasuryChecksum;

		void UpdateChecksum();

		static Amount get_Emission(Height);
		static void get_Emission(AmountBig::Type&, const HeightRange&);
		static void get_Emission(AmountBig::Type&, const HeightRange&, Amount base);

		HeightHash pForks[3];

		const HeightHash& get_LastFork() const;
		const HeightHash* FindFork(const Merkle::Hash&) const;
		std::string get_SignatureStr() const;

	private:
		Amount get_EmissionEx(Height, Height& hEnd, Amount base) const;
		bool IsForkHeightsConsistent() const;
	};

	class SwitchCommitment
	{
		static void get_sk1(ECC::Scalar::Native& res, const ECC::Point::Native& comm0, const ECC::Point::Native& sk0_J);
		void CreateInternal(ECC::Scalar::Native&, ECC::Point::Native&, bool bComm, Key::IKdf& kdf, const Key::IDV& kidv) const;
		void AddValue(ECC::Point::Native& comm, Amount) const;
		static void get_Hash(ECC::Hash::Value&, const Key::IDV&);
	public:

		ECC::Point::Native m_hGen;
		SwitchCommitment(const AssetID* pAssetID = nullptr);

		void Create(ECC::Scalar::Native& sk, Key::IKdf&, const Key::IDV&) const;
		void Create(ECC::Scalar::Native& sk, ECC::Point::Native& comm, Key::IKdf&, const Key::IDV&) const;
		void Create(ECC::Scalar::Native& sk, ECC::Point& comm, Key::IKdf&, const Key::IDV&) const;
		void Recover(ECC::Point::Native& comm, Key::IPKdf&, const Key::IDV&) const;
	};

	struct TxElement
	{
		ECC::Point m_Commitment;
		Height m_Maturity; // Used in macroblocks only.

		TxElement() :m_Maturity(0) {}

		static thread_local bool s_IgnoreMaturity; // should maturity be ignored incomparison?

		int cmp(const TxElement&) const;
		COMPARISON_VIA_CMP
	};

	struct Input
		:public TxElement
	{
		TxoID m_ID = 0; // used internally. Not serialized/transferred

		typedef std::unique_ptr<Input> Ptr;
		typedef uint32_t Count; // the type for count of duplicate UTXOs in the system

		struct State
		{
			Height m_Maturity;
			Input::Count m_Count;

			void get_ID(Merkle::Hash&, const ECC::Point&) const;

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
		:public TxElement
	{
		typedef std::unique_ptr<Output> Ptr;

		bool		m_Coinbase;
		bool		m_RecoveryOnly;
		Height		m_Incubation; // # of blocks before it's mature
		AssetID		m_AssetID;

		Output()
			:m_Coinbase(false)
			,m_RecoveryOnly(false)
			,m_Incubation(0)
		{
			m_AssetID = Zero;
		}

		static const Amount s_MinimumValue = 1;

		// one of the following *must* be specified
		std::unique_ptr<ECC::RangeProof::Confidential>	m_pConfidential;
		std::unique_ptr<ECC::RangeProof::Public>		m_pPublic;

		void Create(Height hScheme, ECC::Scalar::Native&, Key::IKdf& coinKdf, const Key::IDV&, Key::IPKdf& tagKdf, bool bPublic = false);

		bool Recover(Height hScheme, Key::IPKdf& tagKdf, Key::IDV&) const;
		bool VerifyRecovered(Key::IPKdf& coinKdf, const Key::IDV&) const;

		bool IsValid(Height hScheme, ECC::Point::Native& comm) const;
		Height get_MinMaturity(Height h) const; // regardless to the explicitly-overridden

		void operator = (const Output&);
		int cmp(const Output&) const;
		COMPARISON_VIA_CMP

		static void GenerateSeedKid(ECC::uintBig&, const ECC::Point& comm, Key::IPKdf&);
		void Prepare(ECC::Oracle&, Height hScheme) const;
	};

	inline bool operator < (const Output::Ptr& a, const Output::Ptr& b) { return *a < *b; }

	struct TxKernel
		:public TxElement
	{
		typedef std::unique_ptr<TxKernel> Ptr;

		// Mandatory
		ECC::Signature	m_Signature;	// For the whole body, including nested kernels
		Amount			m_Fee;			// can be 0 (for instance for coinbase transactions)
		HeightRange		m_Height;
		AmountSigned	m_AssetEmission; // in case it's non-zero - the kernel commitment is the AssetID
		bool			m_CanEmbed;

		TxKernel()
			:m_Fee(0)
			,m_AssetEmission(0)
			,m_CanEmbed(false)
		{}

		struct HashLock
		{
			ECC::uintBig m_Preimage;

			int cmp(const HashLock&) const;
			COMPARISON_VIA_CMP
		};

		std::unique_ptr<HashLock> m_pHashLock;

		struct RelativeLock
		{
			Merkle::Hash m_ID;
			Height m_LockHeight;

			int cmp(const RelativeLock&) const;
			COMPARISON_VIA_CMP
		};

		std::unique_ptr<RelativeLock> m_pRelativeLock;

		std::vector<Ptr> m_vNested; // nested kernels, included in the signature.

		static const uint32_t s_MaxRecursionDepth = 2;

		static void TestRecursion(uint32_t n)
		{
			if (n > s_MaxRecursionDepth)
				throw std::runtime_error("recursion too deep");
		}

		void get_Hash(Merkle::Hash&, const ECC::Hash::Value* pLockImage = NULL) const; // for signature. Contains all, including the m_Commitment (i.e. the public key)
		void get_ID(Merkle::Hash&, const ECC::Hash::Value* pLockImage = NULL) const; // unique kernel identifier in the system.

		bool IsValid(Height hScheme, AmountBig::Type& fee, ECC::Point::Native& exc) const;
		void Sign(const ECC::Scalar::Native&); // suitable for aux kernels, created by single party

		struct LongProof; // legacy

		void operator = (const TxKernel&);
		int cmp(const TxKernel&) const;
		COMPARISON_VIA_CMP

		size_t get_TotalCount() const; // including self and nested

	private:
		bool Traverse(ECC::Hash::Value&, AmountBig::Type*, ECC::Point::Native*, const TxKernel* pParent, const ECC::Hash::Value* pLockImage, const Height* pScheme) const;
	};

	inline bool operator < (const TxKernel::Ptr& a, const TxKernel::Ptr& b) { return *a < *b; }

	struct TxBase
	{
		class Context;
		static int CmpInOut(const Input&, const Output&);

		struct IReader
		{
			typedef std::unique_ptr<IReader> Ptr;

			// during iterations those pointers are guaranteed to be valid during at least 1 consequent iteration
			const Input* m_pUtxoIn;
			const Output* m_pUtxoOut;
			const TxKernel* m_pKernel;

			virtual ~IReader() {}
			virtual void Clone(Ptr&) = 0;
			virtual void Reset() = 0;
			// For all the following methods: the returned pointer should be valid during at least 2 consequent calls!
			virtual void NextUtxoIn() = 0;
			virtual void NextUtxoOut() = 0;
			virtual void NextKernel() = 0;

			void Compare(IReader&& rOther, bool& bICover, bool& bOtherCovers);
			size_t get_SizeNetto(); // account only for elements. Ignore offset and array sizes
		};

		struct IWriter
		{
			virtual ~IWriter() {}
			virtual void Write(const Input&) = 0;
			virtual void Write(const Output&) = 0;
			virtual void Write(const TxKernel&) = 0;

			void Dump(IReader&&);
			bool Combine(IReader** ppR, int nR, const volatile bool& bStop); // combine consequent blocks, merge-sort and delete consumed outputs
			// returns false if aborted
			bool Combine(IReader&& r0, IReader&& r1, const volatile bool& bStop);
		};


		ECC::Scalar m_Offset;
	};

	struct TxVectors
	{
		struct Perishable
		{
			std::vector<Input::Ptr> m_vInputs;
			std::vector<Output::Ptr> m_vOutputs;
			size_t NormalizeP(); // w.r.t. the standard, delete spent outputs. Returns the num deleted
		};

		struct Eternal
		{
			std::vector<TxKernel::Ptr> m_vKernels;
			void NormalizeE();
		};

		class Reader :public TxBase::IReader {
			size_t m_pIdx[3];
		public:
			const Perishable& m_P;
			const Eternal& m_E;
			Reader(const Perishable& p, const Eternal& e) :m_P(p) ,m_E(e) {}
			// IReader
			virtual void Clone(Ptr&) override;
			virtual void Reset() override;
			virtual void NextUtxoIn() override;
			virtual void NextUtxoOut() override;
			virtual void NextKernel() override;
		};

		struct Writer :public TxBase::IWriter
		{
			Perishable& m_P;
			Eternal& m_E;
			Writer(Perishable& p, Eternal& e) :m_P(p), m_E(e) {}

			virtual void Write(const Input&) override;
			virtual void Write(const Output&) override;
			virtual void Write(const TxKernel&) override;
		};

		struct Full
			:public TxVectors::Perishable
			,public TxVectors::Eternal
		{
			Reader get_Reader() const {
				return Reader(*this, *this);
			}

			size_t Normalize();

			void MoveInto(Full& trg);
		};
	};

	struct Transaction
		:public TxBase
		,public TxVectors::Full
	{
		typedef std::shared_ptr<Transaction> Ptr;

		bool IsValid(Context&) const; // Explicit fees are considered "lost" in the transactions (i.e. would be collected by the miner)

		typedef uintBig_t<ECC::nBytes> KeyType; // key len for map of transactions. Can actually be less than 256 bits.

		void get_Key(KeyType&) const;

		struct FeeSettings
		{
			Amount m_Output;
			Amount m_Kernel; // nested kernels are accounted too

			FeeSettings(); // defaults

			Amount Calculate(const Transaction&) const;
		};
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
			static const uint32_t N = 150;
			static const uint32_t K = 5;

			static const uint32_t nNumIndices		= 1 << K; // 32
			static const uint32_t nBitsPerIndex		= N / (K + 1) + 1; // 26

			static const uint32_t nSolutionBits		= nNumIndices * nBitsPerIndex; // 832 bits

			static_assert(!(nSolutionBits & 7), "PoW solution should be byte-aligned");
			static const uint32_t nSolutionBytes	= nSolutionBits >> 3; // 104 bytes

			std::array<uint8_t, nSolutionBytes>	m_Indices;

			typedef uintBig_t<8> NonceType;
			NonceType m_Nonce; // 8 bytes. The overall solution size is 96 bytes.
			Difficulty m_Difficulty;

			bool IsValid(const void* pInput, uint32_t nSizeInput, Height) const;

			using Cancel = std::function<bool(bool bRetrying)>;
			// Difficulty and Nonce must be initialized. During the solution it's incremented each time by 1.
			// returns false only if cancelled
			bool Solve(const void* pInput, uint32_t nSizeInput, Height, const Cancel& = [](bool) { return false; });

		private:
			struct Helper;
		};

		struct SystemState
		{
			typedef HeightHash ID;

			struct Sequence
			{
				struct Prefix {
					Height				m_Height;
					Merkle::Hash		m_Prev;			// explicit referebce to prev
					Difficulty::Raw		m_ChainWork;
				};

				struct Element
				{
					Merkle::Hash	m_Kernels; // of this block only
					Merkle::Hash	m_Definition; // Defined as Hash[ History | Utxos ]
					Timestamp		m_TimeStamp;
					PoW				m_PoW;

					// The following not only interprets the proof, but also verifies the knwon part of its structure.
					bool IsValidProofUtxo(const ECC::Point&, const Input::Proof&) const;
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
				bool IsValid() const {
					return IsSane() && IsValidPoW(); 
				}
                bool GeneratePoW(const PoW::Cancel& = [](bool) { return false; });

				// the most robust proof verification - verifies the whole proof structure
				bool IsValidProofState(const ID&, const Merkle::HardProof&) const;

				bool IsValidProofKernel(const TxKernel&, const TxKernel::LongProof&) const;
				bool IsValidProofKernel(const Merkle::Hash& hvID, const TxKernel::LongProof&) const;

				int cmp(const Full&) const;
				COMPARISON_VIA_CMP

				bool IsNext(const Full& sNext) const;

			private:
				void get_HashInternal(Merkle::Hash&, bool bTotal) const;
			};

			struct IHistory
			{
				// should provide access to some recent states of the active branch

				struct IWalker {
					virtual bool OnState(const Block::SystemState::Full&) = 0;
				};

				virtual bool Enum(IWalker&, const Height* pBelow) = 0;
				virtual bool get_At(Full&, Height) = 0;
				virtual void AddStates(const Full*, size_t nCount) = 0;
				virtual void DeleteFrom(Height) = 0;

				bool get_Tip(Full&); // zero-inits if no tip
			};

			struct HistoryMap
				:public IHistory
			{
				// simple impl
				std::map<Height, Full> m_Map;
				void ShrinkToWindow(Height dh);

				virtual bool Enum(IWalker&, const Height* pBelow) override;
				virtual bool get_At(Full&, Height) override;
				virtual void AddStates(const Full*, size_t nCount) override;
				virtual void DeleteFrom(Height) override;
			};

		};

		struct BodyBase
			:public TxBase
		{
			void ZeroInit();

			// Test the following:
			//		Validity of all the components, and overall arithmetics, whereas explicit fees are already collected by extra UTXO(s) put by the miner
			//		All components are specified in a lexicographical order, to conceal the actual transaction graph
			//		Liquidity of the components wrt height and maturity policies
			// Not tested by this function (but should be tested by nodes!)
			//		Existence of all the input UTXOs
			//		Existence of the coinbase non-confidential output UTXO, with the sum amount equal to the new coin emission.
			bool IsValid(const HeightRange&, TxBase::IReader&&) const;

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
			,public TxVectors::Full
		{
			bool IsValid(const HeightRange& hr) const
			{
				return BodyBase::IsValid(hr, get_Reader());
			}
		};

		struct ChainWorkProof;

		struct Builder
		{
			ECC::Scalar::Native m_Offset; // the sign is opposite
			TxVectors::Full m_Txv;

			Key::Index m_SubIdx;
			Key::IKdf& m_Coin;
			Key::IPKdf& m_Tag;
			Height m_Height;

			Builder(Key::Index, Key::IKdf& coin, Key::IPKdf& tag, Height);

			void AddCoinbaseAndKrn();
			void AddCoinbaseAndKrn(Output::Ptr&, TxKernel::Ptr&);
			void AddFees(Amount fees);
			void AddFees(Amount fees, Output::Ptr&);
		};
	};

	struct TxKernel::LongProof
	{
		Merkle::Proof m_Inner;
		Block::SystemState::Full m_State;
		Merkle::HardProof m_Outer;

		bool empty() const { return !m_State.m_Height; }

		template <typename Archive>
		void serialize(Archive& ar)
		{
			ar
				& m_Inner
				& m_State
				& m_Outer;
		}
	};

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
		// Sum(Input UTXOs) = Sum(Output UTXOs) + Sum(Output Kernels.Excess) + m_Offset*G [ + Sum(Fee)*H ]
		//
		// For transaction validation fees are considered as implicit outputs (i.e. Sum(Fee)*H should be added for the right equation side)
		//
		// For a block validation Fees are not accounted for, since they are consumed by new outputs injected by the miner.
		// However Each block contains extra outputs (coinbase) for block closure, which should be subtracted from the outputs for sum validation.
		//
		// Define: Sigma = Sum(Output UTXOs) - Sum(Input UTXOs) + Sum(Output Kernels.Excess) + m_Offset*G
		// In other words Sigma = <all outputs> - <all inputs>
		// Sigma is either zero or -Sum(Fee)*H, depending on what we validate

		struct Params
		{
			bool m_bBlockMode; // in 'block' mode the hMin/hMax on input denote the range of heights. Each element is verified wrt it independently.
			// i.e. different elements may have non-overlapping valid range, and it's valid.
			// Suitable for merged block validation

			bool m_bVerifyOrder; // check the correct order, as well as elimination of spent outputs. On by default. Turned Off only for specific internal validations (such as treasury).
			bool m_bAllowUnsignedOutputs; // allow outputs without signature (commitment only). Applicable for cut-through blocks only, outputs that are supposed to be consumed in the later block.

			// for multi-tasking, parallel verification
			uint32_t m_nVerifiers;
			volatile bool* m_pAbort;

			Params(); // defaults
		};

		const Params& m_Params;

		ECC::Point::Native m_Sigma;

		AmountBig::Type m_Fee;
		AmountBig::Type m_Coinbase;
		HeightRange m_Height;

		uint32_t m_iVerifier;

		Context(const Params& p)
			:m_Params(p)
		{
			Reset();
		}

		void Reset();

		bool ValidateAndSummarize(const TxBase&, IReader&&);
		bool Merge(const Context&);

		// hi-level functions, should be used after all parts were validated and merged
		bool IsValidTransaction();
		bool IsValidBlock();
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
		bool IsValid(SystemState::Full* pTip = NULL) const;
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

		struct IStateWalker
		{
			virtual bool OnState(const SystemState::Full&, bool bIsTip) = 0;
		};

		// enumerates all the embedded states in standard order (from lo to hi)
		bool EnumStates(IStateWalker&) const;
		void UnpackStates(std::vector<SystemState::Full>&) const;

	private:
		struct Sampler;
		bool IsValidInternal(size_t& iState, size_t& iHash, const Difficulty::Raw& lowerBound, SystemState::Full* pTip) const;
		void ZeroInit();
		bool EnumStatesHeadingOnly(IStateWalker&) const; // skip arbitrary
	};
}
