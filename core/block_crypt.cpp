#include <utility> // std::swap
#include "common.h"
#include "ecc_native.h"

namespace beam
{

	namespace Merkle
	{
		void Interpret(Hash& hash, const Proof& p)
		{
			for (Proof::const_iterator it = p.begin(); p.end() != it; it++)
			{
				const Node& n = *it;

				const ECC::uintBig* pp[] = { &hash, &n.second };
				if (!n.first)
					std::swap(pp[0], pp[1]);

				ECC::Hash::Processor hp;

				for (int i = 0; i < _countof(pp); i++)
					hp << *pp[i];

				hp >> hash;
			}
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

		for (List::const_iterator it = m_vNested.begin(); m_vNested.end() != it; it++)
		{
			if (!(*it)->Traverse(hv, pFee, pExcess))
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

		List::const_iterator it0 = m_vNested.begin();
		List::const_iterator it1 = v.m_vNested.begin();

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
		fee = 0;
		sigma = ECC::Zero;
		
		for (auto it = m_vInputs.cbegin(); m_vInputs.cend() != it; it++)
		{
			const Input& v = *(*it);
			sigma += ECC::Point::Native(v.m_Commitment);
		}

		sigma = -sigma;

		for (auto it = m_vOutputs.cbegin(); m_vOutputs.cend() != it; it++)
		{
			const Output& v = *(*it);
			if (!v.IsValid())
				return false;

			sigma += ECC::Point::Native(v.m_Commitment);
		}

		for (auto it = m_vKernels.cbegin(); m_vKernels.cend() != it; it++)
		{
			const TxKernel& v = *(*it);
			if (nHeight < v.m_HeightMin || nHeight > v.m_HeightMax)
				return false;
			if (!v.IsValid(fee, sigma))
				return false;
		}

		sigma += ECC::Context::get().G * m_Offset;

		return true;
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

} // namespace beam
