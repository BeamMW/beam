#include <utility> // std::swap
#include <algorithm>
#include "block_crypt.h"

namespace beam
{

	/////////////
	// HeightRange
	void HeightRange::Reset()
	{
		m_Min = 0;
		m_Max = MaxHeight;
	}

	void HeightRange::Intersect(const HeightRange& x)
	{
		m_Min = std::max(m_Min, x.m_Min);
		m_Max = std::min(m_Max, x.m_Max);
	}

	bool HeightRange::IsEmpty() const
	{
		return m_Min > m_Max;
	}

	bool HeightRange::IsInRange(Height h) const
	{
		return IsInRangeRelative(h - m_Min);
	}

	bool HeightRange::IsInRangeRelative(Height dh) const
	{
		return dh <= (m_Max - m_Min);
	}

	/////////////
	// Merkle
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
	bool TxKernel::Traverse(ECC::Hash::Value& hv, AmountBig* pFee, ECC::Point::Native* pExcess, const TxKernel* pParent) const
	{
		if (pParent)
		{
			// nested kernel restrictions
			if (m_Multiplier != pParent->m_Multiplier) // Multipliers must be equal
				return false; 

			if ((m_Height.m_Min > pParent->m_Height.m_Min) ||
				(m_Height.m_Max < pParent->m_Height.m_Max))
				return false; // parent Height range must be contained in ours.
		}

		ECC::Hash::Processor hp;
		hp	<< m_Fee
			<< m_Height.m_Min
			<< m_Height.m_Max
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

			if (!v.Traverse(hv, pFee, pExcess, this))
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

				ECC::Point::Native pt2(pt);
				pt = pt2 * (m_Multiplier + 1);
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
		Traverse(out, NULL, NULL, NULL);
	}

	bool TxKernel::IsValid(AmountBig& fee, ECC::Point::Native& exc) const
	{
		ECC::Hash::Value hv;
		return Traverse(hv, &fee, &exc, NULL);
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

		// Some kernel hash values are reserved for the system usage
		if (hv == ECC::Zero)
		{
			ECC::Hash::Processor() << hv >> hv;
			assert(!(hv == ECC::Zero));
		}
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
		CMP_MEMBER(m_Height.m_Min)
		CMP_MEMBER(m_Height.m_Max)
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

		ZeroObject(m_Fee);
		ZeroObject(m_Coinbase);
		m_Height.Reset();
		m_bBlockMode = false;
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

	bool TxBase::Context::HandleElementHeight(const HeightRange& hr)
	{
		HeightRange r = m_Height;
		r.Intersect(hr);
		if (r.IsEmpty())
			return false;

		if (!m_bBlockMode)
			m_Height = r; // shrink permitted range

		return true;
	}

	bool TxBase::Context::Merge(const Context& x)
	{
		assert(m_bBlockMode == x.m_bBlockMode);

		if (!HandleElementHeight(x.m_Height))
			return false;

		m_Sigma += x.m_Sigma;
		m_Fee += x.m_Fee;
		m_Coinbase += x.m_Coinbase;
		return true;
	}

	bool TxBase::Context::ValidateAndSummarize(const TxBase& txb, IReader& r)
	{
		if (m_Height.IsEmpty())
			return false;

		m_Sigma = -m_Sigma;
		AmountBig feeInp; // dummy var

		assert(m_nVerifiers);
		uint32_t iV = m_iVerifier;

		// Inputs
		r.Reset();

		for (const Input* pPrev = NULL; ; )
		{
			if (ShouldAbort())
				return false;

			const Input* pInp = r.get_NextUtxoIn();
			if (!pInp)
				break;

			if (ShouldVerify(iV))
			{
				if (pPrev && (*pPrev > *pInp))
					return false;

				m_Sigma += ECC::Point::Native(pInp->m_Commitment);
			}

			pPrev = pInp;
		}

		const TxKernel* pKrnOut = r.get_NextKernelOut();

		for (const TxKernel* pPrev = NULL; ; )
		{
			if (ShouldAbort())
				return false;

			const TxKernel* pKrn = r.get_NextKernelIn();
			if (!pKrn)
				break;

			// locate the corresponding output kernel. Use the fact that kernels are sorted by excess, and then by multiplier
			// Do it regardless to the milti-verifier logic, to ensure we're not confused (muliple identical inputs, less outputs, and etc.)
			while (true)
			{
				if (!pKrnOut)
					return false;

				const TxKernel& vOut = *pKrnOut;
				pKrnOut = r.get_NextKernelOut();

				if (vOut.m_Excess > pKrn->m_Excess)
					return false;

				if (vOut.m_Excess == pKrn->m_Excess)
				{
					if (vOut.m_Multiplier <= pKrn->m_Multiplier)
						return false;
					break; // ok
				}
			}

			if (ShouldVerify(iV))
			{
				if (pPrev && (*pPrev > *pKrn))
					return false;

				if (!pKrn->IsValid(feeInp, m_Sigma))
					return false;
			}

			pPrev = pKrn;
		}

		m_Sigma = -m_Sigma;

		// Outputs
		r.Reset();

		for (const Output* pPrev = NULL; ; )
		{
			if (ShouldAbort())
				return false;

			const Output* pOut = r.get_NextUtxoOut();
			if (!pOut)
				break;

			if (ShouldVerify(iV))
			{
				if (pPrev && (*pPrev > *pOut))
					return false;

				if (!pOut->IsValid())
					return false;

				m_Sigma += ECC::Point::Native(pOut->m_Commitment);

				if (pOut->m_Coinbase)
				{
					if (!m_bBlockMode)
						return false; // regular transactions should not produce coinbase outputs, only the miner should do this.

					assert(pOut->m_pPublic); // must have already been checked
					m_Coinbase += pOut->m_pPublic->m_Value;
				}

				if (pOut->m_hDelta)
				{
					if (!m_bBlockMode)
						return false; // this should only be used in merged blocks

					if (!m_Height.IsInRangeRelative(pOut->m_hDelta))
						return false;
				}
			}


			pPrev = pOut;
		}

		for (const TxKernel* pPrev = NULL; ; )
		{
			if (ShouldAbort())
				return false;

			const TxKernel* pKrn = r.get_NextKernelOut();
			if (!pKrn)
				break;

			if (ShouldVerify(iV))
			{
				if (pPrev && (*pPrev > *pKrn))
					return false;

				if (!pKrn->IsValid(m_Fee, m_Sigma))
					return false;

				if (!HandleElementHeight(pKrn->m_Height))
					return false;
			}

			pPrev = pKrn;
		}

		if (ShouldVerify(iV))
			m_Sigma += ECC::Context::get().G * txb.m_Offset;

		assert(!m_Height.IsEmpty());
		return true;
	}

	void TxVectors::Sort()
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

	size_t TxVectors::DeleteIntermediateOutputs()
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

	int Transaction::cmp(const Transaction& v) const
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
		assert(!(m_Coinbase.Lo || m_Coinbase.Hi)); // must have already been checked

		m_Fee.AddTo(m_Sigma);

		return m_Sigma == ECC::Zero;
	}

	bool Transaction::IsValid(Context& ctx) const
	{
		return
			ctx.ValidateAndSummarize(*this, get_Reader()) &&
			ctx.IsValidTransaction();
	}

	template <typename T>
	void TestNotNull(const std::unique_ptr<T>& p)
	{
		if (!p)
			throw std::runtime_error("invalid NULL ptr");
	}

	void TxVectors::TestNoNulls() const
	{
		for (auto it = m_vInputs.begin(); m_vInputs.end() != it; it++)
			TestNotNull(*it);

		for (auto it = m_vKernelsInput.begin(); m_vKernelsInput.end() != it; it++)
			TestNotNull(*it);

		for (auto it = m_vOutputs.begin(); m_vOutputs.end() != it; it++)
			TestNotNull(*it);

		for (auto it = m_vKernelsOutput.begin(); m_vKernelsOutput.end() != it; it++)
			TestNotNull(*it);
	}

	void Transaction::get_Key(KeyType& key) const
	{
		if (m_Offset.m_Value == ECC::Zero)
		{
			// proper transactions must contain non-trivial offset, and this should be enough to identify it with sufficient probability
			// However in case it's not specified - construct the key from contents
			key = ECC::Zero;

			for (auto i = 0; i < m_vInputs.size(); i++)
				key ^= m_vInputs[i]->m_Commitment.m_X;

			for (auto i = 0; i < m_vOutputs.size(); i++)
				key ^= m_vOutputs[i]->m_Commitment.m_X;

			for (auto i = 0; i < m_vKernelsOutput.size(); i++)
				key ^= m_vKernelsOutput[i]->m_Excess.m_X;
		}
		else
			key = m_Offset.m_Value;
	}

	void Transaction::Reader::Reset()
	{
		ZeroObject(m_pIdx);
	}

	template <typename T>
	const T* get_NextFromVector(const std::vector<std::unique_ptr<T> >& v, size_t& idx)
	{
		return (idx >= v.size()) ? NULL : v[idx++].get();
	}

	const Input* Transaction::Reader::get_NextUtxoIn()
	{
		return get_NextFromVector(m_Txv.m_vInputs, m_pIdx[0]);
	}

	const Output* Transaction::Reader::get_NextUtxoOut()
	{
		return get_NextFromVector(m_Txv.m_vOutputs, m_pIdx[1]);
	}

	const TxKernel* Transaction::Reader::get_NextKernelIn()
	{
		return get_NextFromVector(m_Txv.m_vKernelsInput, m_pIdx[2]);
	}

	const TxKernel* Transaction::Reader::get_NextKernelOut()
	{
		return get_NextFromVector(m_Txv.m_vKernelsOutput, m_pIdx[3]);
	}

	/////////////
	// AmoutBig
	void AmountBig::operator += (Amount x)
	{
		Lo += x;
		if (Lo < x)
			Hi++;
	}

	void AmountBig::operator -= (Amount x)
	{
		if (Lo < x)
			Hi--;
		Lo -= x;
	}

	void AmountBig::operator += (const AmountBig& x)
	{
		operator += (x.Lo);
		Hi += x.Hi;
	}

	void AmountBig::operator -= (const AmountBig& x)
	{
		operator -= (x.Lo);
		Hi -= x.Hi;
	}

	void AmountBig::Export(ECC::uintBig& x) const
	{
		x = ECC::Zero;
		x.AssignRange<Amount, 0>(Lo);
		x.AssignRange<Amount, (sizeof(Lo) << 3) >(Hi);
	}

	void AmountBig::AddTo(ECC::Point::Native& res) const
	{
		if (Hi)
		{
			ECC::Scalar s;
			Export(s.m_Value);
			res += ECC::Context::get().H_Big * s;
		}
		else
			if (Lo)
				res += ECC::Context::get().H * Lo;
	}

	/////////////
	// Block
	const Amount Block::Rules::Coin				= 1000000;
	const Amount Block::Rules::CoinbaseEmission = Coin * 40; // the maximum allowed coinbase in a single block
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

	void Block::SystemState::Full::Set(Prefix& p, const Element& x)
	{
		((Prefix&) *this) = p;
		((Element&) *this) = x;

		get_Hash(p.m_Prev);
		p.m_Height++;
	}

	void Block::SystemState::Full::get_Hash(Merkle::Hash& out) const
	{
		// Our formula:
		ECC::Hash::Processor()
			<< m_Height
			<< m_TimeStamp
			<< m_Prev
			<< m_Definition
			>> out;
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

	bool TxBase::Context::IsValidBlock(const Block::BodyBase& bb, bool bSubsidyOpen)
	{
		m_Sigma = -m_Sigma;

		bb.m_Subsidy.AddTo(m_Sigma);

		if (!(m_Sigma == ECC::Zero))
			return false;

		if (bSubsidyOpen)
			return true;

		if (bb.m_SubsidyClosing)
			return false; // already closed

		// For non-genesis blocks we have the following restrictions:
		// Subsidy is bounded by num of blocks multiplied by coinbase emission
		// There must at least some unspent coinbase UTXOs wrt maturity settings

		// check the subsidy is within allowed range
		Height nBlocksInRange = m_Height.m_Max - m_Height.m_Min + 1;

		ECC::uintBig ubSubsidy, ubCoinbase, mul;
		bb.m_Subsidy.Export(ubSubsidy);

		mul = Block::Rules::CoinbaseEmission;
		ubCoinbase = nBlocksInRange;
		ubCoinbase = ubCoinbase * mul;

		if (ubSubsidy > ubCoinbase)
			return false;

		// ensure there's a minimal unspent coinbase UTXOs
		if (nBlocksInRange > Block::Rules::MaturityCoinbase)
		{
			// some UTXOs may be spent already. Calculate the minimum remaining
			nBlocksInRange -= Block::Rules::MaturityCoinbase;
			ubCoinbase = nBlocksInRange;
			ubCoinbase = ubCoinbase * mul;

			if (ubSubsidy > ubCoinbase)
			{
				ubCoinbase.Negate();
				ubSubsidy += ubCoinbase;

			} else
				ubSubsidy = ECC::Zero;
		}

		m_Coinbase.Export(ubCoinbase);
		return (ubCoinbase >= ubSubsidy);
	}

	void Block::BodyBase::ZeroInit()
	{
		ZeroObject(m_Subsidy);
		ZeroObject(m_Offset);
		m_SubsidyClosing = false;
	}

	bool Block::BodyBase::IsValid(const HeightRange& hr, bool bSubsidyOpen, TxBase::IReader& r) const
	{
		assert((hr.m_Min >= Block::Rules::HeightGenesis) && !hr.IsEmpty());

		TxBase::Context ctx;
		ctx.m_Height = hr;
		ctx.m_bBlockMode = true;

		return
			ctx.ValidateAndSummarize(*this, r) &&
			ctx.IsValidBlock(*this, bSubsidyOpen);
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

	std::ostream& operator << (std::ostream& s, const Block::SystemState::ID& id)
	{
		s << id.m_Height << "-" << id.m_Hash;
		return s;
	}

} // namespace beam
