#include <utility> // std::swap
#include <algorithm>
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
		return m_Commitment.cmp(v.m_Commitment);
	}

	void Input::get_Hash(Merkle::Hash& out, Count n) const
	{
		ECC::Hash::Processor()
			<< m_Commitment
			<< n
			>> out;
	}

	bool Input::IsValidProof(Count n, const Merkle::Proof& proof, const Merkle::Hash& root) const
	{
		Merkle::Hash hv;
		get_Hash(hv, n);
		Merkle::Interpret(hv, proof);
		return hv == root;
	}

	/////////////
	// Output
	bool Output::IsValid() const
	{
		if (m_pConfidential)
		{
			if (m_Coinbase)
				return false; // coinbase must have visible amount

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
		CMP_MEMBER_EX(m_Commitment)
		CMP_MEMBER(m_Coinbase)
		CMP_MEMBER_PTR(m_pConfidential)
		CMP_MEMBER_PTR(m_pPublic)

		return 0;
	}

	void Output::Create(const ECC::Scalar::Native& k, Amount v, bool bPublic /* = false */)
	{
		m_Commitment = ECC::Commitment(k, v);

		if (bPublic)
		{
			m_pPublic.reset(new ECC::RangeProof::Public);
			m_pPublic->m_Value = v;
			m_pPublic->Create(k);
		} else
		{
			m_pConfidential.reset(new ECC::RangeProof::Confidential);
			m_pConfidential->Create(k, v);
		}
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

			// The hash of this kernel should account for the signature and the excess of the internal kernels.
			hp	<< v.m_Excess
				<< v.m_Multiplier
				<< v.m_Signature.m_e
				<< v.m_Signature.m_k
				<< hv;
		}

		hp >> hv;

		if (pExcess)
		{
			ECC::Point::Native pt(m_Excess);
			if (m_Multiplier)
				pt += pt * m_Multiplier;

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

	void TxKernel::get_HashForSigning(Merkle::Hash& out) const
	{
		Traverse(out, NULL, NULL);
	}

	bool TxKernel::IsValid(Amount& fee, ECC::Point::Native& exc) const
	{
		ECC::Hash::Value hv;
		return Traverse(hv, &fee, &exc);
	}

	void TxKernel::get_HashTotal(Merkle::Hash& hv) const
	{
		get_HashForSigning(hv);
		ECC::Hash::Processor()
			<< hv
			<< m_Excess
			<< m_Multiplier
			<< m_Signature.m_e
			<< m_Signature.m_k
			>> hv;
	}

	bool TxKernel::IsValidProof(const Merkle::Proof& proof, const Merkle::Hash& root) const
	{
		Merkle::Hash hv;
		get_HashTotal(hv);
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
		CMP_MEMBER(m_Multiplier)
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
	void TxBase::Context::Reset()
	{
		m_Fee = m_Coinbase = 0;
		m_hMin = 0;
		m_hMax = -1;
	}

	bool TxBase::ValidateAndSummarize(Context& ctx, ECC::Point::Native& sigma) const
	{
		sigma = ECC::Zero;
		Amount feeInp = 0; // dummy var

		// Inputs

		const Input* p0Inp = NULL;
		for (auto it = m_vInputs.begin(); m_vInputs.end() != it; it++)
		{
			const Input& v = *(*it);

			if (p0Inp && (*p0Inp > v))
				return false;
			p0Inp = &v;

			sigma += ECC::Point::Native(v.m_Commitment);
		}

		auto itKrnOut = m_vKernelsOutput.begin();
		const TxKernel* p0Krn = NULL;

		for (auto it = m_vKernelsInput.begin(); m_vKernelsInput.end() != it; it++)
		{
			const TxKernel& v = *(*it);

			// locate the corresponding output kernel. Use the fact that kernels are sorted by excess, and then by multiplier
			while (true)
			{
				if (m_vKernelsOutput.end() == itKrnOut)
					return false;

				const TxKernel& vOut = *(*itKrnOut);
				itKrnOut++;

				if (vOut.m_Excess > v.m_Excess)
					return false;

				if (vOut.m_Excess == v.m_Excess)
				{
					if (vOut.m_Multiplier <= v.m_Multiplier)
						return false;
					break; // ok
				}
			}

			if (!v.IsValid(feeInp, sigma))
				return false;

			if (p0Krn && (*p0Krn > v))
				return false;
			p0Krn = &v;

			ctx.m_hMin = std::max(ctx.m_hMin, v.m_HeightMin);
			ctx.m_hMax = std::min(ctx.m_hMax, v.m_HeightMax);
		}

		sigma = -sigma;

		// Outputs

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

			if (v.m_Coinbase)
			{
				assert(v.m_pPublic);
				ctx.m_Coinbase += v.m_pPublic->m_Value;
			}
		}

		p0Krn = NULL;
		for (auto it = m_vKernelsOutput.begin(); m_vKernelsOutput.end() != it; it++)
		{
			const TxKernel& v = *(*it);
			if (!v.IsValid(ctx.m_Fee, sigma))
				return false;

			if (p0Krn && (*p0Krn > v))
				return false;
			p0Krn = &v;

			ctx.m_hMin = std::max(ctx.m_hMin, v.m_HeightMin);
			ctx.m_hMax = std::min(ctx.m_hMax, v.m_HeightMax);
		}

		sigma += ECC::Context::get().G * m_Offset;

		return ctx.m_hMin <= ctx.m_hMax;
	}

	void TxBase::Sort()
	{
		std::sort(m_vInputs.begin(), m_vInputs.end());
		std::sort(m_vOutputs.begin(), m_vOutputs.end());
		std::sort(m_vKernelsInput.begin(), m_vKernelsInput.end());
		std::sort(m_vKernelsOutput.begin(), m_vKernelsOutput.end());
	}

	template <class T>
	void RebuildVectorWithoutNulls(std::vector<T>& v, size_t nDel)
	{
		std::vector<T> vSrc;
		vSrc.swap(v);
		v.reserve(vSrc.size() - nDel);

		for (size_t i = 0; i < vSrc.size(); i++)
			if (vSrc[i])
				v.push_back(std::move(vSrc[i]));
	}

	size_t TxBase::DeleteIntermediateOutputs()
	{
		size_t nDel = 0;

		size_t i1 = m_vOutputs.size();
		for (size_t i0 = 0; i0 < m_vInputs.size(); i0++)
		{
			Input::Ptr& pInp = m_vInputs[i0];

			for (; i1 < m_vOutputs.size(); i1++)
			{
				Output::Ptr& pOut = m_vOutputs[i1];

				int n = pInp->m_Commitment.cmp(pOut->m_Commitment);
				if (n <= 0)
				{
					if (!n)
					{
						pInp.reset();
						pOut.reset();
						nDel++;
					}
					break;
				}
			}
		}

		if (nDel)
		{
			RebuildVectorWithoutNulls(m_vInputs, nDel);
			RebuildVectorWithoutNulls(m_vOutputs, nDel);
		}

		return nDel;
	}

	bool Transaction::IsValid(Context& ctx) const
	{
		ECC::Point::Native sigma(ECC::Zero);

		if (!ValidateAndSummarize(ctx, sigma))
			return false;

		if (ctx.m_Coinbase)
			return false; // regular transactions should not produce coinbase outputs, only the miner should do this.

		sigma += ECC::Context::get().H * ctx.m_Fee;

		return sigma == ECC::Zero;
	}

	/////////////
	// Block
	const Amount Block::s_CoinbaseEmission = 1000000 * 15; // the maximum allowed coinbase in a single block
	const Height Block::s_MaturityCoinbase	= 60; // 1 hour
	const Height Block::s_MaturityStd		= 0; // not restricted. Can spend even in the block of creation (i.e. spend it before it becomes visible)

	int Block::SystemState::ID::cmp(const ID& v) const
	{
		CMP_MEMBER(m_Height)
		CMP_MEMBER(m_Hash)
		return 0;
	}

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
		//			m_LiveObjects
		//			]
		//		]
		//	]

		Merkle::Hash h0, h1;

		ECC::Hash::Processor() << m_Difficulty >> h1;
		ECC::Hash::Processor() << m_TimeStamp >> h0;
		Merkle::Interpret(h1, h0, true); // [ m_Difficulty, m_TimeStamp]

		ECC::Hash::Processor() << m_Height >> h0;
		Merkle::Interpret(h0, h1, true); // [ m_Height, [ m_Difficulty, m_TimeStamp] ]

		Merkle::Interpret(h1, m_Prev, m_History);
		Merkle::Interpret(h1, m_LiveObjects, true); // [ [m_Prev, m_States], m_LiveObjects ]

		Merkle::Interpret(out, h0, h1);
	}

	void Block::SystemState::Full::get_ID(ID& out) const
	{
		out.m_Height = m_Height;
		get_Hash(out.m_Hash);
	}

	bool Block::Body::IsValid(Height h0, Height h1) const
	{
		assert(h0 <= h1);

		Context ctx;
		ctx.m_hMin = h0;
		ctx.m_hMax = h1;

		ECC::Point::Native sigma;
		if (!ValidateAndSummarize(ctx, sigma))
			return false;

		sigma = -sigma;
		sigma += ECC::Context::get().H * ctx.m_Coinbase;

		if (!(sigma == ECC::Zero)) // No need to add fees explicitly, they must have already been consumed
			return false;

		Amount nCoinbaseMax = s_CoinbaseEmission * (h1 - h0 + 1); // TODO: overflow!
		return (ctx.m_Coinbase <= nCoinbaseMax);
	}

} // namespace beam
