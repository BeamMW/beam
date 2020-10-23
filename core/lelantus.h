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
namespace Sigma {

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

		uint32_t get_N() const; // n^M if parameters are sane, 0 otherwise
		uint32_t get_F() const; // M * (n - 1)

		Cfg() {}
		Cfg(uint32_t n_, uint32_t M_)
			:n(n_)
			,M(M_)
		{}

		bool operator == (const Cfg&) const;

		void Expose(ECC::Oracle& oracle) const;
	};

	struct Proof
	{
		struct Part1
		{
			ECC::Point m_A, m_B, m_C, m_D;
			std::vector<ECC::Point> m_vG;

			void Expose(ECC::Oracle& oracle) const;

		} m_Part1;
		// <- x
		struct Part2
		{
			ECC::Scalar m_zA, m_zC, m_zR;
			std::vector<ECC::Scalar> m_vF;
		} m_Part2;

		bool IsValid(ECC::InnerProduct::BatchContext& bc, ECC::Oracle& oracle, const Cfg&, ECC::Scalar::Native* pKs, ECC::Scalar::Native& kBias) const;
	};

	class Prover
	{
		CmList& m_List;
		const Cfg& m_Cfg;

		std::unique_ptr<ECC::Scalar::Native[]> m_vBuf; // all the needed data as one array

		struct Idx
		{
			enum Enum {
				rA,
				rB,
				rC,
				rD,
				count
			};
		};

		// nonces
		ECC::Scalar::Native* m_a;
		ECC::Scalar::Native* m_Tau;

		// precalculated coeffs
		ECC::Scalar::Native* m_p;

		void InitNonces(const ECC::uintBig& seed);
		void CalculateP();
		void ExtractABCD();
		void ExtractG(const ECC::Point::Native& ptOut);
		struct GB;
		void ExtractG_Part(GB*, uint32_t i0, uint32_t i1);
		void ExtractPart2(ECC::Oracle&, bool bIsSinglePass);

		static void ExtractBlinded(ECC::Scalar& out, const ECC::Scalar::Native& sk, const ECC::Scalar::Native& challenge, const ECC::Scalar::Native& nonce);

	public:

		struct NonceGen;

		Prover(CmList& lst, const Cfg& cfg, Proof& proof)
			:m_List(lst)
			,m_Cfg(cfg)
			,m_Proof(proof)
		{
		}

		// witness data
		struct Witness
		{
			uint32_t m_L;
			ECC::Scalar::Native m_R;
		};
		Witness m_Witness;

		class UserData
		{
			static void RecoverOnce(ECC::Scalar& out, const ECC::Scalar& res, ECC::Scalar::Native& a0, ECC::Scalar::Native& a1, const ECC::Scalar::Native& x);
		public:

			ECC::Scalar m_pS[2];
			void Recover(ECC::Oracle& oracle, const Proof&, const ECC::uintBig& seed);
		};

		const UserData* m_pUserData = nullptr;

		enum struct Phase {
			SinglePass, // regular
			Step1, // export Part1
			// other party can add tau[], and produce m_zR with additional blinding factor
			Step2, // import initial value of m_zR
		};

		void Generate(const ECC::uintBig& seed, ECC::Oracle& oracle, const ECC::Point::Native& ptBias, Phase ePhase = Phase::SinglePass);

		// result
		Proof& m_Proof;
	};

} // namespace Sigma

namespace Lelantus
{
	typedef Sigma::CmList CmList;
	typedef Sigma::CmListVec CmListVec;
	typedef Sigma::Cfg Cfg;

	namespace SpendKey {
		void ToSerial(ECC::Scalar::Native& serial, const ECC::Point& pk);
	}

	struct Proof
		:public Sigma::Proof
	{
		Cfg m_Cfg;

		ECC::Point m_Commitment;
		ECC::Point m_SpendPk;
		void Expose0(ECC::Oracle& oracle, ECC::Hash::Value&) const;

		ECC::SignatureGeneralized<2> m_Signature;

		bool IsValid(ECC::InnerProduct::BatchContext& bc, ECC::Oracle& oracle, ECC::Scalar::Native* pKs, const ECC::Point::Native* pHGen = nullptr) const;
	};

	struct Prover
	{
		Sigma::Prover m_Sigma;

		Prover(CmList& lst, Proof& proof)
			:m_Sigma(lst, proof.m_Cfg, proof)
		{
		}

		// witness data
		struct Witness
			:public Sigma::Prover::Witness
		{
			// all the following is ignored in MPC mode
			Amount m_V;
			ECC::Scalar::Native m_R_Output; // 'true' blinding factor of the being-spent output
			ECC::Scalar::Native m_R_Adj; // Assets: effective blinding factor (includes the blinding factor of the generator multiplied by value).
			ECC::Scalar::Native m_SpendSk;
		};
		Witness m_Witness;

		ECC::Hash::Value m_hvSigGen;
		void GenerateSigGen(const ECC::Point::Native* pHGen);

		typedef Sigma::Prover::Phase Phase;

		void Generate(const ECC::uintBig& seed, ECC::Oracle& oracle, const ECC::Point::Native* pHGen = nullptr, Phase ePhase = Phase::SinglePass);
	};

} // namespace Lelantus
} // namespace beam
