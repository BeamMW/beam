#include <utility> // std::swap
#include "common.h"
#include "ecc_native.h"

namespace beam
{

	namespace Merkle
	{
		void Interpret(Hash& out, const Hash& hLeft, const Hash& hRight)
		{
			ECC::Hash::Processor() << hLeft << hRight >> out;
		}

		void Interpret(Hash& hOld, const Hash& hNew, bool bNewOnRight)
		{
			if (bNewOnRight)
				Interpret(hOld, hOld, hNew);
			else
				Interpret(hOld, hNew, hOld);
		}

		void Interpret(Hash& hash, const Node& n)
		{
			Interpret(hash, n.second, n.first);
		}

		void Interpret(Hash& hash, const Proof& p)
		{
			for (Proof::const_iterator it = p.begin(); p.end() != it; it++)
				Interpret(hash, *it);
		}
	}

#define CMP_MEMBER(member) \
		if (member < v.member) \
			return -1; \
		if (member > v.member) \
			return 1;

#define CMP_MEMBER_EX(member) \
		{ \
			int n = member.cmp(v.member); \
			if (n) \
				return n; \
		}

#define CMP_MEMBER_PTR(member) \
		if (member) \
		{ \
			if (!v.member) \
				return 1; \
			int n = member->cmp(*v.member); \
			if (n) \
				return n; \
		} else \
			if (v.member) \
				return -1;

	/////////////
	// Input

	int Input::cmp(const Input& v) const
	{
		CMP_MEMBER(m_Coinbase)
		CMP_MEMBER_EX(m_Commitment)
		CMP_MEMBER(m_Height)
		return 0;
	}

	void Input::get_Hash(Merkle::Hash& out) const
	{
		ECC::Hash::Processor()
			<< m_Coinbase
			<< m_Commitment
			<< m_Height
			>> out;
	}

	bool Input::IsValidProof(const Merkle::Proof& proof, const Merkle::Hash& root) const
	{
		Merkle::Hash hv;
		get_Hash(hv);
		Merkle::Interpret(hv, proof);
		return hv == root;
	}

	/////////////
	// Output
	bool Output::IsValid() const
	{
		if (m_pConfidential)
		{
			if (m_pPublic)
				return false;

			return m_pConfidential->IsValid(m_Commitment);
		}

		if (!m_pPublic)
			return false;

		return m_pPublic->IsValid(m_Commitment);
	}

	int Output::cmp(const Output& v) const
	{
		CMP_MEMBER(m_Coinbase)
		CMP_MEMBER_EX(m_Commitment)
		CMP_MEMBER_PTR(m_pConfidential)
		CMP_MEMBER_PTR(m_pPublic)

		return 0;
	}

	/////////////
	// TxKernel
	bool TxKernel::Traverse(ECC::Hash::Value& hv, Amount* pFee, ECC::Point::Native* pExcess) const
	{
		ECC::Hash::Processor hp;
		hp	<< m_Fee
			<< m_HeightMin
			<< m_HeightMax
			<< (bool) m_pContract;

		if (m_pContract)
		{
			hp	<< m_pContract->m_Msg
				<< m_pContract->m_PublicKey;
		}

		const TxKernel* p0Krn = NULL;
		for (auto it = m_vNested.begin(); m_vNested.end() != it; it++)
		{
			const TxKernel& v = *(*it);
			if (p0Krn && (*p0Krn > v))
				return false;
			p0Krn = &v;

			if (!v.Traverse(hv, pFee, pExcess))
				return false;
			hp << hv;
		}

		hp >> hv;

		if (pExcess)
		{
			ECC::Point::Native pt(m_Excess);

			if (!m_Signature.IsValid(hv, pt))
				return false;

			*pExcess += pt;

			if (m_pContract)
			{
				ECC::Hash::Value hv2;
				get_HashForContract(hv2, hv);

				if (!m_pContract->m_Signature.IsValid(hv2, ECC::Point::Native(m_pContract->m_PublicKey)))
					return false;
			}
		}

		if (pFee)
			*pFee += m_Fee;

		return true;
	}

	void TxKernel::get_HashForContract(ECC::Hash::Value& out, const ECC::Hash::Value& msg) const
	{
		ECC::Hash::Processor()
			<< msg
			<< m_Excess
			>> out;
	}

	void TxKernel::get_Hash(Merkle::Hash& out) const
	{
		Traverse(out, NULL, NULL);
	}

	bool TxKernel::IsValid(Amount& fee, ECC::Point::Native& exc) const
	{
		ECC::Hash::Value hv;
		return Traverse(hv, &fee, &exc);
	}

	bool TxKernel::IsValidProof(const Merkle::Proof& proof, const Merkle::Hash& root) const
	{
		Merkle::Hash hv;
		get_Hash(hv);
		Merkle::Interpret(hv, proof);
		return hv == root;
	}

	int TxKernel::Contract::cmp(const Contract& v) const
	{
		CMP_MEMBER_EX(m_Msg)
		CMP_MEMBER_EX(m_PublicKey)
		CMP_MEMBER_EX(m_Signature)
		return 0;
	}

	int TxKernel::cmp(const TxKernel& v) const
	{
		CMP_MEMBER_EX(m_Excess)
		CMP_MEMBER_EX(m_Signature)
		CMP_MEMBER(m_Fee)
		CMP_MEMBER(m_HeightMin)
		CMP_MEMBER(m_HeightMax)
		CMP_MEMBER_PTR(m_pContract)

		auto it0 = m_vNested.begin();
		auto it1 = v.m_vNested.begin();

		for ( ; m_vNested.end() != it0; it0++, it1++)
		{
			if (v.m_vNested.end() == it1)
				return 1;

			int n = (*it0)->cmp(*(*it1));
			if (n)
				return n;
		}

		if (v.m_vNested.end() != it1)
			return -1;

		return 0;
	}

	/////////////
	// Transaction
	bool TxBase::ValidateAndSummarize(Amount& fee, ECC::Point::Native& sigma, Height nHeight) const
	{
		const Input* p0Inp = NULL;
		for (auto it = m_vInputs.begin(); m_vInputs.end() != it; it++)
		{
			const Input& v = *(*it);

			if (p0Inp && (*p0Inp > v))
				return false;
			p0Inp = &v;
			sigma += ECC::Point::Native(v.m_Commitment);
		}

		sigma = -sigma;

		const Output* p0Out = NULL;
		for (auto it = m_vOutputs.begin(); m_vOutputs.end() != it; it++)
		{
			const Output& v = *(*it);
			if (!v.IsValid())
				return false;

			if (p0Out && (*p0Out > v))
				return false;
			p0Out = &v;

			sigma += ECC::Point::Native(v.m_Commitment);
		}

		const TxKernel* p0Krn = NULL;
		for (auto it = m_vKernels.begin(); m_vKernels.end() != it; it++)
		{
			const TxKernel& v = *(*it);
			if (nHeight < v.m_HeightMin || nHeight > v.m_HeightMax)
				return false;
			if (!v.IsValid(fee, sigma))
				return false;

			if (p0Krn && (*p0Krn > v))
				return false;
			p0Krn = &v;
		}

		sigma += ECC::Context::get().G * m_Offset;

		return true;
	}

	void TxBase::Sort()
	{
		std::sort(m_vInputs.begin(), m_vInputs.end());
		std::sort(m_vOutputs.begin(), m_vOutputs.end());
		std::sort(m_vKernels.begin(), m_vKernels.end());
	}

	bool Transaction::IsValid(Amount& fee, Height nHeight) const
	{
		ECC::Point::Native sigma(ECC::Zero);
		fee = 0;

		if (!ValidateAndSummarize(fee, sigma, nHeight))
			return false;

		sigma += ECC::Context::get().H * fee;

		return sigma == ECC::Zero;
	}

	/////////////
	// Block
	void Block::SystemState::Full::get_Hash(Merkle::Hash& out) const
	{
		// Our formula:
		//
		//	[
		//		[
		//			m_Height
		//			[
		//				m_Difficulty
		//				m_TimeStamp
		//			]
		//		]
		//		[
		//			[
		//				m_Prev
		//				m_States
		//			]
		//			[
		//				m_Utxos
		//				m_Kernels
		//			]
		//		]
		//	]

		Merkle::Hash h0, h1, h2;

		ECC::Hash::Processor() << m_Difficulty >> h1;
		ECC::Hash::Processor() << m_TimeStamp >> h0;
		Merkle::Interpret(h1, h0, true); // [ m_Difficulty, m_TimeStamp]

		ECC::Hash::Processor() << m_Height >> h0;
		Merkle::Interpret(h0, h1, true); // [ m_Height, [ m_Difficulty, m_TimeStamp] ]

		Merkle::Interpret(h1, m_Prev, m_States);
		Merkle::Interpret(h2, m_Utxos, m_Kernels);
		Merkle::Interpret(h1, h2, true); // [ [m_Prev, m_States], [m_States, m_Utxos] ]

		Merkle::Interpret(out, h0, h1);
	}

	void Block::SystemState::Full::get_ID(ID& out) const
	{
		out.m_Height = m_Height;
		get_Hash(out.m_Hash);
	}

} // namespace beam
