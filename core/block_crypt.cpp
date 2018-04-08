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
				{
					const ECC::uintBig& v = *pp[i];
					hp.Write(v.m_pData, sizeof(v.m_pData));
				}

				hp.Finalize(hash);
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
		ECC::Hash::Processor hp;
		hp.Write(m_Coinbase);
		hp.Write(m_Commitment.m_X);
		hp.Write(m_Commitment.m_bQuadraticResidue);
		hp.WriteOrd(m_Height);
		hp.Finalize(out);
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

		hp.WriteOrd(m_Fee);
		hp.Write(m_Excess);
		hp.WriteOrd(m_Height);

		if (m_pContract)
		{
			hp.Write("contract");
			hp.Write(m_pContract->m_Msg);
			hp.Write(m_pContract->m_PublicKey);
		}

		for (List::const_iterator it = m_vNested.begin(); m_vNested.end() != it; it++)
		{
			if (!(*it)->Traverse(hv, pFee, pExcess))
				return false;
			hp.Write(hv);
		}

		hp.Finalize(hv);

		if (pExcess)
		{
			ECC::Point::Native pt;
			pt.Import(m_Excess);

			if (!m_Signature.IsValid(hv, pt))
				return false;

			pExcess->Add(pt);

			if (m_pContract)
			{
				hp.Reset();
				hp.Write(hv);
				hp.Write(m_Excess);

				ECC::Hash::Value hv2;
				hp.Finalize(hv2);

				pt.Import(m_pContract->m_PublicKey);
				if (!m_pContract->m_Signature.IsValid(hv2, pt))
					return false;
			}
		}

		if (pFee)
			*pFee += m_Fee;

		return true;
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
		CMP_MEMBER(m_Height)
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


} // namespace beam
