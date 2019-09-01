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

namespace beam {
namespace Lelantus {

	struct CmList
	{
		virtual bool get_At(ECC::Point::Storage&, uint32_t iIdx) = 0;

		void Import(ECC::MultiMac&, uint32_t iPos, uint32_t nCount);
		void Calculate(ECC::Point::Native&, uint32_t iPos, uint32_t nCount, const ECC::Scalar::Native* pKs);
	};

	struct CmListVec
		:public CmList
	{
		std::vector<ECC::Point::Storage> m_vec;
		virtual bool get_At(ECC::Point::Storage& res, uint32_t iIdx) override
		{
			if (iIdx >= m_vec.size())
				return false;

			res = m_vec[iIdx];
			return true;
		}
	};

	struct Cfg
	{
		// bitness selection
		uint32_t n = 4;
		uint32_t M = 5;

		struct Max {
			static const uint32_t n = 128; // typically it's 2 or 4
			static const uint32_t M = 31;
			static const uint32_t nM = ECC::InnerProduct::nDim * 2 - 1; // otherwise we won't have enough generators
			static const uint32_t N = 0x100000; // 1mln. Typically it's VERY MUCH smaller.
		};

		uint32_t get_N() const; // n^M is parameters are sane, 0 otherwise
		uint32_t get_F() const; // M * (n - 1)
	};

	namespace SpendKey {
		void ToSerial(ECC::Scalar::Native& serial, const ECC::Point& pk);
	}

	struct Proof
	{
		Cfg m_Cfg;

		struct Output
		{
			// not a part of the proof, a by-product
			ECC::Point m_Commitment;
			ECC::Point::Native m_Pt; // same
		};

		struct Part1
		{
			ECC::Point m_SpendPk;
			ECC::Point m_A, m_B, m_C, m_D;
			ECC::Point m_NonceG; // consists of G only. Used to sign both balance and spend proofs.

			struct GQ {
				ECC::Point m_G;
				ECC::Point m_Q;
			};

			std::vector<GQ> m_vGQ;

			void Expose(ECC::Oracle& oracle) const;

		} m_Part1;
		// <- x
		struct Part2
		{
			ECC::Scalar m_zA, m_zC, m_zV, m_zR;
			std::vector<ECC::Scalar> m_vF;
			ECC::Scalar m_ProofG; // Both balance and spend proofs

		} m_Part2;

		bool IsValid(ECC::InnerProduct::BatchContext& bc, ECC::Oracle& oracle, const Output&, ECC::Scalar::Native* pKs) const;
	};

	class Prover
	{
		CmList& m_List;

		std::unique_ptr<ECC::Scalar::Native[]> m_vBuf; // all the needed data as one array

		struct Idx
		{
			static const uint32_t rA = 0;
			static const uint32_t rB = 1;
			static const uint32_t rC = 2;
			static const uint32_t rD = 3;
			static const uint32_t rBalance = 4;
			static const uint32_t Serial = 5;

			static const uint32_t CountFixed = 6;

			// 
		};

		// nonces
		ECC::Scalar::Native* m_a;
		ECC::Scalar::Native* m_Gamma;
		ECC::Scalar::Native* m_Ro;
		ECC::Scalar::Native* m_Tau;

		// precalculated coeffs
		ECC::Scalar::Native* m_p;

		void InitNonces(const ECC::uintBig& seed);
		void CalculateP();
		void ExtractABCD();
		void ExtractGQ();
		void ExtractPart2(ECC::Oracle&);

		static void ExtractBlinded(ECC::Scalar& out, const ECC::Scalar::Native& sk, const ECC::Scalar::Native& challenge, const ECC::Scalar::Native& nonce);

	public:

		Prover(CmList& lst, Proof& proof)
			:m_List(lst)
			,m_Proof(proof)
		{
		}

		// witness data
		struct Witness
		{
			uint32_t m_L;
			Amount m_V;
			ECC::Scalar::Native m_R;
			ECC::Scalar::Native m_R_Output;
			ECC::Scalar::Native m_SpendSk;
		};
		ECC::NoLeak<Witness> m_Witness;

		void Generate(Proof::Output&, const ECC::uintBig& seed, ECC::Oracle& oracle);

		// result
		Proof& m_Proof;
	};

} // namespace Lelantus
} // namespace beam
