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
#	define NOMINMAX
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
	parent_class& get_ParentObj() { return * (parent_class*) (((uint8_t*) this) + 1 - (uint8_t*) (&((parent_class*) 1)->this_var)); }

#include "ecc.h"

#include <iostream>
namespace beam
{
	// sorry for replacing 'using' by 'typedefs', some compilers don't support it
	typedef uint64_t Timestamp;
	typedef uint64_t Height;
	typedef ECC::uintBig_t<256> uint256_t;
	typedef std::vector<uint8_t> ByteBuffer;
	typedef ECC::Amount Amount;

	struct AmountBig
	{
		Amount Lo;
		Amount Hi;

		void operator += (const Amount);
		void operator += (const AmountBig&);

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

	struct Input
	{
		typedef std::unique_ptr<Input> Ptr;
		typedef uint32_t Count; // the type for count of duplicate UTXOs in the system

		ECC::Point	m_Commitment; // If there are multiple UTXOs matching this commitment (which is supported) the Node always selects the most mature one.
	
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
	{
		typedef std::unique_ptr<Output> Ptr;

		ECC::Point	m_Commitment;
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

		bool IsValid() const;

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
		Height			m_HeightMin;
		Height			m_HeightMax;

		TxKernel()
			:m_Multiplier(0) // 0-based, 
			,m_Fee(0)
			,m_HeightMin(0)
			,m_HeightMax(Height(-1))
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

		std::unique_ptr<Contract> m_pContract;

		std::vector<Ptr> m_vNested; // nested kernels, included in the signature.

		bool IsValid(AmountBig& fee, ECC::Point::Native& exc) const;

		void get_HashForSigning(Merkle::Hash&) const; // Includes the contents, but not the excess and the signature

		void get_HashTotal(Merkle::Hash&) const; // Includes everything. 
		bool IsValidProof(const Merkle::Proof&, const Merkle::Hash& root) const;

		void get_HashForContract(ECC::Hash::Value&, const ECC::Hash::Value& msg) const;

		int cmp(const TxKernel&) const;
		COMPARISON_VIA_CMP(TxKernel)

	private:
		bool Traverse(ECC::Hash::Value&, AmountBig*, ECC::Point::Native*) const;
	};

	inline bool operator < (const TxKernel::Ptr& a, const TxKernel::Ptr& b) { return *a < *b; }

	struct TxBase
	{
		std::vector<Input::Ptr> m_vInputs;
		std::vector<Output::Ptr> m_vOutputs;
		std::vector<TxKernel::Ptr> m_vKernelsInput;
		std::vector<TxKernel::Ptr> m_vKernelsOutput;
		ECC::Scalar m_Offset;

		void Sort(); // w.r.t. the standard
		size_t DeleteIntermediateOutputs(); // assumed to be already sorted. Retruns the num deleted

		// tests the validity of all the components, overall arithmetics, and the lexicographical order of the components.
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
		// Define: Sigma = Sum(Outputs) - Sum(Inputs) + Sum(TxKernels.Excess) + m_Offset*G
		// Sigma is either zero or -Sum(Fee)*H, depending on what we validate

		struct Context;
		bool ValidateAndSummarize(Context&) const;

		int cmp(const TxBase&) const;
		COMPARISON_VIA_CMP(TxBase)
	};

	struct Transaction
		:public TxBase
	{
		typedef std::shared_ptr<Transaction> Ptr;
		// Explicit fees are considered "lost" in the transactions (i.e. would be collected by the miner)
		bool IsValid(Context&) const;
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

			typedef ECC::uintBig_t<104> NonceType;
			NonceType m_Nonce; // 13 bytes. The overall solution size is 64 bytes.
			uint8_t m_Difficulty;

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

			struct Full {
				Height			m_Height;
				Merkle::Hash	m_Prev;			// explicit referebce to prev
				Merkle::Hash	m_History;		// Objects that are only added and never deleted. Currently: previous states.
				Merkle::Hash	m_LiveObjects;	// Objects that can be both added and deleted. Currently: UTXOs and kernels
				Timestamp		m_TimeStamp;
				PoW				m_PoW;

				void get_Hash(Merkle::Hash&) const; // Calculated from all the above
				void get_ID(ID&) const;

				bool IsSane() const;
				bool IsValidPoW() const;
				bool GeneratePoW(const PoW::Cancel& = [](bool) { return false; });
			};
		};

		struct Rules
		{
			static const Amount Coin; // how many quantas in a single coin. Just cosmetic, has no meaning to the processing (which is in terms of quantas)
			static const Amount CoinbaseEmission; // the maximum allowed coinbase in a single block
			static const Height MaturityCoinbase;
			static const Height MaturityStd;

			static const Height HeightGenesis; // height of the 1st block, defines the convention. Currently =1

			static const size_t MaxBodySize;

			// timestamp & difficulty. Basically very close to those from bitcoin, except the desired rate is 1 minute (instead of 10 minutes)
			static const uint32_t DesiredRate_s = 60; // 1 minute
			static const uint32_t DifficultyReviewCycle = 24 * 60 * 7; // 10,080 blocks, 1 week roughly
			static const uint32_t MaxDifficultyChange = 3; // i.e. x8 roughly. (There's no equivalent to this in bitcoin).
			static const uint32_t TimestampAheadThreshold_s = 60 * 60 * 2; // 2 hours. Timestamps ahead by more than 2 hours won't be accepted
			static const uint32_t WindowForMedian = 25; // Timestamp for a block must be (strictly) higher than the median of preceding window

			static void AdjustDifficulty(uint8_t&, Timestamp tCycleBegin_s, Timestamp tCycleEnd_s);
		};


		struct Body
			:public TxBase
		{
			AmountBig m_Subsidy;

			Body();

			// Test the following:
			//		Validity of all the components, and overall arithmetics, whereas explicit fees are already collected by extra UTXO(s) put by the miner
			//		All components are specified in a lexicographical order, to conceal the actual transaction graph
			//		Liquidity of the components wrt height and maturity policies
			// Not tested by this function (but should be tested by nodes!)
			//		Existence of all the input UTXOs
			//		Existence of the coinbase non-confidential output UTXO, with the sum amount equal to the new coin emission.
			//		Existence of the treasury output UTXO, if needed by the policy.
			bool IsValid(Height h0, Height h1) const;
		};
	};

	enum struct KeyType
	{
		Comission,
		Coinbase,
		Kernel,
		Regular
	};
	void DeriveKey(ECC::Scalar::Native&, const ECC::Kdf&, Height, KeyType, uint32_t nIdx = 0);

	std::ostream& operator << (std::ostream&, const Block::SystemState::ID&);

} // namespace beam
