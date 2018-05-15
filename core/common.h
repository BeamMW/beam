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
		typedef uint32_t Count; // the type for count of duplicate UTXOs in the system

		ECC::Point	m_Commitment; // If there are multiple UTXOs matching this commitment (which is supported) the Node always selects the most mature one.
	
		int cmp(const Input&) const;
		COMPARISON_VIA_CMP(Input)

		void get_Hash(Merkle::Hash&, Count) const;
		bool IsValidProof(Count, const Merkle::Proof&, const Merkle::Hash& root) const;
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
			, m_Incubation(0)
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
			, m_HeightMin(0)
			, m_HeightMax(Height(-1))
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

		bool IsValid(Amount& fee, ECC::Point::Native& exc) const;

		void get_HashForSigning(Merkle::Hash&) const; // Includes the contents, but not the excess and the signature

		void get_HashTotal(Merkle::Hash&) const; // Includes everything. 
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
		typedef std::unique_ptr<Transaction> Ptr;
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

			typedef ECC::uintBig_t<112> NonceType;
			NonceType m_Nonce; // 14 bytes. The overall solution size is 64 bytes.

			bool IsValid(const void* pInput, uint32_t nSizeInput, Difficulty) const;

			using Cancel = std::function<bool(bool bRetrying)>;
			// Nonce must be initialized. During the solution it's incremented each time by 1.
			// returns false only if cancelled
			bool Solve(const void* pInput, uint32_t nSizeInput, Difficulty, const Cancel& = [](bool) { return false; });

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
				Difficulty		m_Difficulty;
				Timestamp		m_TimeStamp;
				PoW				m_PoW;

				void get_Hash(Merkle::Hash&) const; // Calculated from all the above
				void get_ID(ID&) const;

				bool IsValidPoW() const;
				bool GeneratePoW(const PoW::Cancel& = [](bool) { return false; });
			};
		};

		static const Amount s_CoinbaseEmission; // the maximum allowed coinbase in a single block
		static const Height s_MaturityCoinbase;
		static const Height s_MaturityStd;

		static const size_t s_MaxBodySize;

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
}
