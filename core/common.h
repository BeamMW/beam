#pragma once

#include <vector>
#include <array>
#include <utility>
#include <cstdint>
#include <memory>

#include "ecc.h"

namespace beam
{
	// sorry for replacing 'using' by 'typedefs', some compilers don't support it
	typedef uint64_t Timestamp;
	typedef uint64_t Difficulty;

#pragma pack(push, 1)

	struct Block
	{
		// Different parts of the block are split into different structs, so that they can be manipulated (transferred, processed, saved and etc.) independently
		// For instance, there is no need to keep PoW (at least in SPV client) once it has been validated.

		// TBD: decide the serialization format. Basically it consists entirely of structs and ordinal data types, can be stored as-is. Only the matter of big/little-endian should be defined.

		struct Header
		{
			ECC::Hash::Value	m_HashPrev;
			ECC::Hash::Value	m_FullDescription; // merkle hash
		    uint64_t			m_Height;
		    Timestamp			m_TimeStamp;
		    Difficulty			m_TotalDifficulty;
		};

		struct PoW
		{
			// equihash parameters
			static const uint32_t N = 200;
			static const uint32_t K = 9;

			static const uint32_t nNumIndices		= 1 << K; // 512 
			static const uint32_t nBitsPerIndex		= N / (K + 1); // 20. actually tha last index may be wider (equal to max bound), but since indexes are sorted it can be encoded as 0.

			static const uint32_t nSolutionBits		= nNumIndices * nBitsPerIndex;

			static_assert(!(nSolutionBits & 7), "PoW solution should be byte-aligned");
			static const uint32_t nSolutionBytes	= nSolutionBits >> 3; // 1280 bytes

			ECC::uintBig_t<256>	m_Nonce;
			uint8_t				m_Difficulty;
			uint8_t				m_pSolution[nSolutionBytes];

			bool IsValid(const Header&) const;
		};

		struct Body
		{
			uint64_t m_NumInputs;
			uint64_t m_NumOutputs;
			uint64_t m_NumKernels;

			// Probably should account for additional parameters, such as block explicit subsidy, sidechains and etc.

			// followed by inputs, outputs and kernels in a lexicographical order

			void Verify(); // format & arithmetics only, regardless to the existence of the input UTXOs
		};
	};

#pragma pack(pop)

}