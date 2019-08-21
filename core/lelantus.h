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
		static const uint32_t n = 4; // binary
		static const uint32_t M = 8;
		static const uint32_t N = Power<M>::Of<n>::V;
	};

	namespace SpendKey {
		void ToSerial(ECC::Scalar::Native& serial, const ECC::Point& pk);
	}

	struct Proof
	{
		struct Part1
		{
			ECC::Point m_SpendPk;
			ECC::Point m_Output; // result commitment. Must have the same value as the commitment being-spent
			ECC::Point m_A, m_B, m_C, m_D;
			ECC::Point m_pG[Cfg::M];
			ECC::Point m_pQ[Cfg::M];
			ECC::Point m_NonceG; // consists of G only. Used to sign both balance and spend proofs.

			void Expose(ECC::Oracle& oracle) const;

		} m_Part1;
		// <- x
		struct Part2
		{
			ECC::Scalar m_zA, m_zC, m_zV, m_zR;
			ECC::Scalar m_pF[Cfg::M][Cfg::n - 1];
			ECC::Scalar m_ProofG; // Both balance and spend proofs

		} m_Part2;

		bool IsValid(ECC::InnerProduct::BatchContext& bc, ECC::Oracle& oracle, ECC::Scalar::Native* pKs) const;
	};

	class Prover
	{
		CmList& m_List;

		// nonces
		ECC::Scalar::Native m_rA, m_rB, m_rC, m_rD;
		ECC::Scalar::Native m_a[Cfg::M][Cfg::n];
		ECC::Scalar::Native m_Gamma[Cfg::M];
		ECC::Scalar::Native m_Ro[Cfg::M];
		ECC::Scalar::Native m_Tau[Cfg::M];
		ECC::Scalar::Native m_rBalance;
		ECC::Scalar::Native m_Serial;

		// precalculated coeffs
		ECC::Scalar::Native m_p[Cfg::M][Cfg::N]; // very large

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

		void Generate(const ECC::uintBig& seed, ECC::Oracle& oracle);

		// result
		Proof& m_Proof;
	};

} // namespace Lelantus
} // namespace beam
