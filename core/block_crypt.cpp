#include <utility> // std::swap
#include <algorithm>
#include "block_crypt.h"

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

#define CMP_SIMPLE(a, b) \
		if (a < b) \
			return -1; \
		if (a > b) \
			return 1;

#define CMP_MEMBER(member) CMP_SIMPLE(member, v.member)

#define CMP_MEMBER_EX(member) \
		{ \
			int n = member.cmp(v.member); \
			if (n) \
				return n; \
		}

#define CMP_PTRS(a, b) \
		if (a) \
		{ \
			if (!b) \
				return 1; \
			int n = a->cmp(*b); \
			if (n) \
				return n; \
		} else \
			if (b) \
				return -1;

#define CMP_MEMBER_PTR(member) CMP_PTRS(member, v.member)

	/////////////
	// Input

	int Input::cmp(const Input& v) const
	{
		return m_Commitment.cmp(v.m_Commitment);
	}

	/////////////
	// Output
	bool Output::IsValid() const
	{
		ECC::Oracle oracle;
		oracle << m_Incubation;

		if (m_pConfidential)
		{
			if (m_Coinbase)
				return false; // coinbase must have visible amount

			if (m_pPublic)
				return false;

			return m_pConfidential->IsValid(m_Commitment, oracle);
		}

		if (!m_pPublic)
			return false;

		return m_pPublic->IsValid(m_Commitment, oracle);
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

		ECC::Oracle oracle;
		oracle << m_Incubation;

		if (bPublic)
		{
			m_pPublic.reset(new ECC::RangeProof::Public);
			m_pPublic->m_Value = v;
			m_pPublic->Create(k, oracle);
		} else
		{
			m_pConfidential.reset(new ECC::RangeProof::Confidential);
			m_pConfidential->Create(k, v, oracle);
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
			{
				ECC::Mode::Scope scope(ECC::Mode::Fast);
				pt += pt * m_Multiplier;
			}

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
		m_Sigma = ECC::Zero;

		m_Fee = m_Coinbase = 0;
		m_hMin = 0;
		m_hMax = -1;
		m_bRangeMode = false;
		m_nVerifiers = 1;
		m_iVerifier = 0;
		m_pAbort = NULL;
	}

	bool TxBase::Context::ShouldVerify(uint32_t& iV) const
	{
		if (iV)
		{
			iV--;
			return false;
		}

		iV = m_nVerifiers - 1;
		return true;
	}

	bool TxBase::Context::ShouldAbort() const
	{
		return m_pAbort && *m_pAbort;
	}

	bool TxBase::Context::IsValidHeight() const
	{
		return m_hMin <= m_hMax;
	}

	bool TxBase::Context::HandleElementHeight(Height h0, Height h1)
	{
		h0 = std::max(h0, m_hMin);
		h1 = std::min(h1, m_hMax);

		if (h0 > h1)
			return false;

		if (!m_bRangeMode)
		{
			// shrink permitted range
			m_hMin = h0;
			m_hMax = h1;
		}

		return true;
	}

	bool TxBase::Context::Merge(const Context& x)
	{
		assert(m_bRangeMode == x.m_bRangeMode);

		if (!HandleElementHeight(x.m_hMin, x.m_hMax))
			return false;

		m_Sigma += x.m_Sigma;
		m_Fee += x.m_Fee;
		m_Coinbase += x.m_Coinbase;
		return true;
	}

	bool TxBase::ValidateAndSummarize(Context& ctx) const
	{
		if (!ctx.IsValidHeight())
			return false;

		ctx.m_Sigma = -ctx.m_Sigma;
		Amount feeInp = 0; // dummy var

		assert(ctx.m_nVerifiers);
		uint32_t iV = ctx.m_iVerifier;

		// Inputs

		const Input* p0Inp = NULL;
		for (auto it = m_vInputs.begin(); m_vInputs.end() != it; it++)
		{
			if (ctx.ShouldAbort())
				return false;

			const Input& v = *(*it);

			if (p0Inp && (*p0Inp > v))
				return false;
			p0Inp = &v;

			if (!ctx.ShouldVerify(iV))
				continue;

			ctx.m_Sigma += ECC::Point::Native(v.m_Commitment);
		}

		auto itKrnOut = m_vKernelsOutput.begin();
		const TxKernel* p0Krn = NULL;

		for (auto it = m_vKernelsInput.begin(); m_vKernelsInput.end() != it; it++)
		{
			if (ctx.ShouldAbort())
				return false;

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

			if (p0Krn && (*p0Krn > v))
				return false;
			p0Krn = &v;

			if (!ctx.ShouldVerify(iV))
				return false;

			if (!v.IsValid(feeInp, ctx.m_Sigma))
				return false;
		}

		ctx.m_Sigma = -ctx.m_Sigma;

		// Outputs

		const Output* p0Out = NULL;
		for (auto it = m_vOutputs.begin(); m_vOutputs.end() != it; it++)
		{
			if (ctx.ShouldAbort())
				return false;

			const Output& v = *(*it);

			if (p0Out && (*p0Out > v))
				return false;
			p0Out = &v;

			if (!ctx.ShouldVerify(iV))
				continue;

			if (!v.IsValid())
				return false;

			ctx.m_Sigma += ECC::Point::Native(v.m_Commitment);

			if (v.m_Coinbase)
			{
				assert(v.m_pPublic);
				ctx.m_Coinbase += v.m_pPublic->m_Value;
			}
		}

		p0Krn = NULL;
		for (auto it = m_vKernelsOutput.begin(); m_vKernelsOutput.end() != it; it++)
		{
			if (ctx.ShouldAbort())
				return false;

			const TxKernel& v = *(*it);

			if (p0Krn && (*p0Krn > v))
				return false;
			p0Krn = &v;

			if (!ctx.ShouldVerify(iV))
				continue;

			if (!v.IsValid(ctx.m_Fee, ctx.m_Sigma))
				return false;

			if (!ctx.HandleElementHeight(v.m_HeightMin, v.m_HeightMax))
				return false;
		}

		if (ctx.ShouldVerify(iV))
			ctx.m_Sigma += ECC::Context::get().G * m_Offset;

		assert(ctx.IsValidHeight());
		return true;
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

	template <typename T>
	int CmpPtrVectors(const std::vector<T>& a, const std::vector<T>& b)
	{
		CMP_SIMPLE(a.size(), b.size())

		for (size_t i = 0; i < a.size(); i++)
		{
			CMP_PTRS(a[i], b[i])
		}

		return 0;
	}

	#define CMP_MEMBER_VECPTR(member) \
		{ \
			int n = CmpPtrVectors(member, v.member); \
			if (n) \
				return n; \
		}

	int TxBase::cmp(const TxBase& v) const
	{
		CMP_MEMBER(m_Offset)
		CMP_MEMBER_VECPTR(m_vInputs)
		CMP_MEMBER_VECPTR(m_vOutputs)
		CMP_MEMBER_VECPTR(m_vKernelsInput)
		CMP_MEMBER_VECPTR(m_vKernelsOutput)
		return 0;
	}

	bool TxBase::Context::IsValidTransaction()
	{
		if (m_Coinbase)
			return false; // regular transactions should not produce coinbase outputs, only the miner should do this.

		m_Sigma += ECC::Context::get().H * m_Fee;

		return m_Sigma == ECC::Zero;
	}

	bool Transaction::IsValid(Context& ctx) const
	{
		return
			ValidateAndSummarize(ctx) &&
			ctx.IsValidTransaction();
	}

	/////////////
	// Block
	const Amount Block::Rules::CoinbaseEmission = 1000000 * 15; // the maximum allowed coinbase in a single block
	const Height Block::Rules::MaturityCoinbase	= 60; // 1 hour
	const Height Block::Rules::MaturityStd		= 0; // not restricted. Can spend even in the block of creation (i.e. spend it before it becomes visible)
	const Height Block::Rules::HeightGenesis	= 1;
	const size_t Block::Rules::MaxBodySize		= 0x100000; // 1MB

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
		//			m_TimeStamp
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

		ECC::Hash::Processor() << m_Height >> h0;
		ECC::Hash::Processor() << m_TimeStamp >> h1;
		Merkle::Interpret(h0, h1, true); // [ m_Height, m_TimeStamp ]

		Merkle::Interpret(h1, m_Prev, m_History);
		Merkle::Interpret(h1, m_LiveObjects, true); // [ [m_Prev, m_States], m_LiveObjects ]

		Merkle::Interpret(out, h0, h1);
	}

	bool Block::SystemState::Full::IsSane() const
	{
		if (m_Height < Block::Rules::HeightGenesis)
			return false;
		if ((m_Height == Block::Rules::HeightGenesis) && !(m_Prev == ECC::Zero))
			return false;

		return true;
	}

	void Block::SystemState::Full::get_ID(ID& out) const
	{
		out.m_Height = m_Height;
		get_Hash(out.m_Hash);
	}

	bool Block::SystemState::Full::IsValidPoW() const
	{
		Merkle::Hash hv;
		get_Hash(hv);
		return m_PoW.IsValid(hv.m_pData, sizeof(hv.m_pData));
	}

	bool Block::SystemState::Full::GeneratePoW(const PoW::Cancel& fnCancel)
	{
		Merkle::Hash hv;
		get_Hash(hv);
		return m_PoW.Solve(hv.m_pData, sizeof(hv.m_pData), fnCancel);
	}

	bool TxBase::Context::IsValidBlock()
	{
		Height nBlocksInRange = m_hMax - m_hMin + 1;
		Amount nEmission = Block::Rules::CoinbaseEmission * nBlocksInRange; // TODO: overflow!

		m_Sigma = -m_Sigma;
		m_Sigma += ECC::Context::get().H * nEmission;

		if (!(m_Sigma == ECC::Zero)) // No need to add fees explicitly, they must have already been consumed
			return false;

		// ensure there's a minimal unspent coinbase UTXOs
		Height nUnmatureBlocks = std::min(Block::Rules::MaturityCoinbase, nBlocksInRange);
		Amount nEmissionUnmature = Block::Rules::CoinbaseEmission * nUnmatureBlocks; // TODO: overflow!

		return (m_Coinbase >= nEmissionUnmature);
	}

	bool Block::Body::IsValid(Height h0, Height h1) const
	{
		assert((h0 >= Block::Rules::HeightGenesis) && (h0 <= h1));

		Context ctx;
		ctx.m_hMin = h0;
		ctx.m_hMax = h1;
		ctx.m_bRangeMode = true;

		return
			ValidateAndSummarize(ctx) &&
			ctx.IsValidBlock();
	}

    void DeriveKey(ECC::Scalar::Native& out, const ECC::Kdf& kdf, Height h, KeyType eType, uint32_t nIdx /* = 0 */)
    {
        kdf.DeriveKey(out, h, static_cast<uint32_t>(eType), nIdx);
    }

	void Block::Rules::AdjustDifficulty(uint8_t& d, Timestamp tCycleBegin_s, Timestamp tCycleEnd_s)
	{
		static_assert(DesiredRate_s * DifficultyReviewCycle < uint32_t(-1), "overflow?");
		const uint32_t dtTrg_s = DesiredRate_s * DifficultyReviewCycle;

		uint32_t dt_s; // evaluate carefully, avoid possible overflow
		if (tCycleEnd_s <= tCycleBegin_s)
			dt_s = 0;
		else
		{
			tCycleEnd_s -= tCycleBegin_s;
			dt_s = (tCycleEnd_s < uint32_t(-1)) ? uint32_t(tCycleEnd_s) : uint32_t(-1);
		}

		// Formula:
		//		While dt_s is smaller than dtTrg_s / sqrt(2) - raise the difficulty.
		//		While dt_s is bigger than dtTrg_s * sqrt(2) - lower the difficulty.
		//		There's a limit for adjustment
		//
		// Instead of calculating sqrt(2) we'll square both parameters, and the factor now is 2.

		uint64_t src = uint64_t(dt_s) * uint64_t(dt_s);
		const uint64_t trg = uint64_t(dtTrg_s) * uint64_t(dtTrg_s);

		for (uint32_t i = 0; i < MaxDifficultyChange; i++)
		{
			if (src >= (trg >> 1))
				break;

			if (d == uint8_t(-1))
				return;

			d++;
			src <<= 2;
		}

		for (uint32_t i = 0; i < MaxDifficultyChange; i++)
		{
			if (src <= (trg << 1))
				break;

			if (d == 0)
				return;

			d--;
			src >>= 2;
		}
	}

} // namespace beam
