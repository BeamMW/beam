#pragma once

#include <vector>
#include <array>
#include <list>
#include <map>
#include <utility>
#include <cstdint>
#include <memory>
#include <assert.h>

#ifdef WIN32
#	define NOMINMAX
#	include <windows.h>
#endif // WIN32

#ifndef verify
#	ifdef  NDEBUG
#		define verify(x) ((void)(x))
#	else //  NDEBUG
#		define verify(x) assert(x)
#	endif //  NDEBUG
#endif // verify

#include "ecc.h"

#include <iostream>
namespace beam
{
	// sorry for replacing 'using' by 'typedefs', some compilers don't support it
	typedef uint64_t Timestamp;
	typedef uint64_t Difficulty;
	typedef uint64_t Height;
	typedef ECC::uintBig_t<256> uint256_t;
	typedef std::vector<uint8_t> ByteBuffer;
	typedef ECC::Amount Amount;

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

		ECC::Point	m_Commitment;
		bool		m_Coinbase;
		Height		m_Height;

		// In case there are multiple UTXOs with the same commitment value (which we permit) the height should be used to distinguish between them
		//
		// Transactions:
		//		In case there's no UTXO with the specified height - the node is allowed to increase it to match the existing UTXO.
		//		So that if m_Height is the minimum height to spend. Set to 0 (by default?) to spend the most mature.
		//
		// In the block
		//		The m_Height must exactly match the existing UTXO, no auto-adjustments.

		int cmp(const Input&) const;
		COMPARISON_VIA_CMP(Input)

		void get_Hash(Merkle::Hash&) const;
		bool IsValidProof(const Merkle::Proof&, const Merkle::Hash& root) const;
	};

	inline bool operator < (const Input::Ptr& a, const Input::Ptr& b) { return *a < *b; }

	struct Output
	{
		typedef std::unique_ptr<Output> Ptr;

		ECC::Point	m_Commitment;
		bool		m_Coinbase;

		static const Amount s_MinimumValue = 1;

		// one of the following *must* be specified
		std::unique_ptr<ECC::RangeProof::Confidential>	m_pConfidential;
		std::unique_ptr<ECC::RangeProof::Public>		m_pPublic;

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
		Amount			m_Fee;			// can be 0 (for instance for coinbase transactions)
		Height			m_HeightMin;
		Height			m_HeightMax;

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

		bool IsValid(Amount& fee, ECC::Point::Native& exc) const;

		void get_Hash(Merkle::Hash&) const; // Hash doesn't include signatures
		bool IsValidProof(const Merkle::Proof&, const Merkle::Hash& root) const;

		void get_HashForContract(ECC::Hash::Value&, const ECC::Hash::Value& msg) const;

		int cmp(const TxKernel&) const;
		COMPARISON_VIA_CMP(TxKernel)

	private:
		bool Traverse(ECC::Hash::Value&, Amount*, ECC::Point::Native*) const;
	};

	inline bool operator < (const TxKernel::Ptr& a, const TxKernel::Ptr& b) { return *a < *b; }

	struct TxBase
	{
		std::vector<Input::Ptr> m_vInputs;
		std::vector<Output::Ptr> m_vOutputs;
		std::vector<TxKernel::Ptr> m_vKernels;
		ECC::Scalar m_Offset;

		void Sort(); // w.r.t. the standard

		// tests the validity of all the components, overall arithmetics, and the lexicographical order of the components.
		// Determines the min/max block height that the transaction can fit, wrt component heights and maturity policies
		// Does *not* check the existence of the input UTXOs
		//
		// Validation formula
		//
		// Sum(Inputs) - Sum(Outputs) = Sum(TxKernels.Excess) + m_Offset*G [ + Sum(Fee)*H ]
		//
		// For a block validation Fees are not accounted for, since they are consumed by new outputs injected by the miner.
		//
		// Define: Sigma = Sum(Outputs) - Sum(Inputs) + Sum(TxKernels.Excess) + m_Offset*G
		// Sigma is either zero or -Sum(Fee)*H, depending on what we validate

		struct Context
		{
			Amount m_Fee; // TODO: may overflow!
			Amount m_Coinbase; // TODO: may overflow!
			Height m_hMin;
			Height m_hMax;

			Context() { Reset(); }
			void Reset();
		};

		bool ValidateAndSummarize(Context&, ECC::Point::Native& sigma) const;
	};

	struct Transaction
		:public TxBase
	{
		// Explicit fees are considered "lost" in the transactions (i.e. would be collected by the miner)
		bool IsValid(Context&) const;
	};

	struct Block
	{
		// Different parts of the block are split into different structs, so that they can be manipulated (transferred, processed, saved and etc.) independently
		// For instance, there is no need to keep PoW (at least in SPV client) once it has been validated.

		struct SystemState
		{
			struct ID {
				Merkle::Hash	m_Hash; // explained later
				Height			m_Height;
			};

			struct Full {
				Height			m_Height;
				Merkle::Hash	m_Prev;			// explicit referebce to prev
				Merkle::Hash	m_States;		// all previous states
				Merkle::Hash	m_Utxos;		// current UTXOs
				Merkle::Hash	m_Kernels;		// kernels
				Difficulty		m_Difficulty;
				Timestamp		m_TimeStamp;

				void get_Hash(Merkle::Hash&) const; // Calculated from all the above
				void get_ID(ID&) const;
			};
		};


		struct Header
		{
			SystemState::Full	m_StateNew; // after the block changes are applied
			SystemState::Full	m_StatePrev;

			// Normally the difference between m_StatePrev and m_StateNew corresponds to 1 original block, Height is increased by 1
			// But if/when history is compressed, blocks can encode compressed diff of several original blocks

		    template<typename Buffer>
			void serializeTo(Buffer& b)
			{

			}
		} header;

		struct PoW
		{
			// equihash parameters
			static const uint32_t N = 200;
			static const uint32_t K = 9;

			static const uint32_t nNumIndices		= 1 << K; // 512
			static const uint32_t nBitsPerIndex		= N / (K + 1) + 1; // 20. actually tha last index may be wider (equal to max bound), but since indexes are sorted it can be encoded as 0.

			static const uint32_t nSolutionBits		= nNumIndices * nBitsPerIndex;

			static_assert(!(nSolutionBits & 7), "PoW solution should be byte-aligned");
			static const uint32_t nSolutionBytes	= nSolutionBits >> 3; // !TODO: 1280 bytes, 1344 for now due to current implementation

			uint256_t							m_Nonce; // does it always have to be 256-bit long?
			std::array<uint8_t, nSolutionBytes>	m_Indices;

			bool IsValid(const SystemState::Full& prev, const SystemState::Full& next) const;
		};
		typedef std::unique_ptr<PoW> PoWPtr;
		PoWPtr m_ProofOfWork;

		static const Amount s_CoinbaseEmission = 1000000 * 15; // the maximum allowed coinbase in a single block
		static const Height s_MaturityCoinbase	= 60; // 1 hour
		static const Height s_MaturityStd		= 0; // not restricted. Can spend even in the block of creation (i.e. spend it before it becomes visible)

		struct Body
			:public TxBase
		{
			// TODO: additional parameters, such as block explicit subsidy, sidechains and etc.

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

	typedef std::unique_ptr<Block> BlockPtr;
}
