#include "ecc_native.h"

namespace beam
{

	namespace Merkle
	{
		typedef ECC::Hash::Value Hash;
		typedef std::pair<bool, Hash>	Node;
		typedef std::list<Node>			Proof;

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

	bool Output::IsValid() const
	{
		if (m_pCondidential)
		{
			if (m_pPublic)
				return false;

			return m_pCondidential->IsValid(m_Commitment);
		}

		if (!m_pPublic)
			return false;

		return m_pPublic->IsValid(m_Commitment);
	}

	int Output::cmp(const Output& v) const
	{
		CMP_MEMBER(m_Coinbase)
		CMP_MEMBER_EX(m_Commitment)
		CMP_MEMBER_PTR(m_pCondidential)
		CMP_MEMBER_PTR(m_pPublic)

		return 0;
	}


} // namespace beam
