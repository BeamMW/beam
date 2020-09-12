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

#define _CRT_SECURE_NO_WARNINGS // sprintf
#include "bvm.h"
#include "bvm_opcodes.h"
#include <sstream>

#define MY_TOKENIZE2(a, b) a##b
#define MY_TOKENIZE1(a, b) MY_TOKENIZE2(a, b)

namespace beam {
namespace bvm {

	void get_Cid(ContractID& cid, const Blob& data, const Blob& args)
	{
		ECC::Hash::Processor()
			<< "bvm.cid"
			<< data.n
			<< args.n
			<< data
			<< args
			>> cid;
	}

	void get_AssetOwner(PeerID& pidOwner, const ContractID& cid, const Asset::Metadata& md)
	{
		ECC::Hash::Processor()
			<< "bvm.a.own"
			<< cid
			<< md.m_Hash
			>> pidOwner;
	}

	/////////////////////////////////////////////
	// Processor

	void Processor::InitStack(const Buf& args)
	{
		Test(args.n <= Limits::StackSize - sizeof(StackFrame));
		memcpy(m_pStack, args.p, args.n);
		memset0(m_pStack + args.n, Limits::StackSize - args.n);

		m_Sp = static_cast<Type::Size>(args.n + sizeof(StackFrame));
		LogStackPtr();
		m_Ip = 0;
	}

	void Processor::LogStackPtr()
	{
		if (m_pDbg)
			*m_pDbg << "sp=" << m_Sp << std::endl;
	}

	void Processor::CallFar(const ContractID& cid, Type::Size iMethod)
	{
		Test(m_FarCalls.m_Stack.size() < Limits::FarCallDepth);
		auto& x = *m_FarCalls.m_Stack.Create_back();

		x.m_Cid = cid;
		x.m_LocalDepth = 0;

		VarKey vk;
		SetVarKey(vk);
		LoadVar(vk, x.m_Data);

		m_Data = x.m_Data;

		Ptr ptr;
		Cast::Down<Buf>(ptr) = m_Data;
		ptr.m_Writable = false;

		const auto* pHdr = ptr.RGet<Header>();

		Type::Size n;
		pHdr->m_Version.Export(n);
		Test(Header::s_Version == n);

		pHdr->m_NumMethods.Export(n);
		Test(iMethod < n);

		Test(ptr.Move(sizeof(Header) - sizeof(Header::MethodEntry) * Header::s_MethodsMin + sizeof(Header::MethodEntry) * iMethod));

		DoJmp(*ptr.RGet<Header::MethodEntry>());
	}

	const uint8_t* Processor::FetchInstruction(Type::Size n)
	{
		Type::Size nIp = m_Ip + n;
		Test((nIp >= m_Ip) && (nIp <= m_Data.n));

		const uint8_t* pRet = m_Data.p + m_Ip;
		m_Ip = nIp;

		return pRet;
	}

	uint8_t Processor::FetchBit(BitReader& br)
	{
		if (br.m_Bits)
			br.m_Bits--;
		else
		{
			br.m_Value = *FetchInstruction(1);
			br.m_Bits = 7;
		}

		uint8_t ret = 1 & br.m_Value;
		br.m_Value >>= 1;
		return ret;
	}

	void Processor::FetchPtr(BitReader& br, Ptr& out)
	{
		const auto* pAddr = reinterpret_cast<const Type::uintSize*>(FetchInstruction(Type::uintSize::nBytes));
		FetchPtr(br, out, *pAddr);
	}

	void Processor::FetchPtr(BitReader& br, Ptr& out, const Type::uintSize& addr)
	{
		uint8_t nRel = 1;
		uint8_t bData = FetchBit(br); // data or stack
		if (bData)
		{
			nRel = FetchBit(br); // data or stack absolute?
			if (!nRel)
				bData = 0;
		}

		Type::Size n;
		addr.Export(n);

		if (bData)
			SetPtrData(out, n);
		else
		{
			if (nRel)
				n += m_Sp; // wraparound is ok, negative stack offsets are allowed
			SetPtrStack(out, n);
		}

		if (m_pDbg)
			*m_pDbg << (bData ? 'd' : nRel ? 's' : 'p') << static_cast<int>(out.p - (bData ? m_Data.p : (m_pStack + m_Sp)));
	}

	void Processor::LogDeref()
	{
		if (m_pDbg)
			*m_pDbg << "=>";
	}

	void Processor::LogOpCode(const char* szName)
	{
		LogVarName(szName);
	}

	void Processor::LogVarName(const char* szName)
	{
		if (m_pDbg)
			*m_pDbg << szName << " = ";
	}

	void Processor::LogVarEnd()
	{
		if (m_pDbg)
			*m_pDbg << ", ";
	}

	void Processor::LogValue(const Ptr& x)
	{
		struct Dummy :public uintBigImpl {
			static void Do(const uint8_t* pDst, uint32_t nDst, std::ostream& os) {
				_Print(pDst, nDst, os);
			}
		};

		if (m_pDbg)
			Dummy::Do(x.p, x.n, *m_pDbg);
	}

	template <> void Processor::TestStackPtr<true>(Type::Size n) {
		Test(n <= Limits::StackSize);
	}

	template <> void Processor::TestStackPtr<false>(Type::Size) {
	}

	void Processor::SetPtrStack(Ptr& out, Type::Size n)
	{
		constexpr bool bAlwaysInRage = (static_cast<Type::Size>(-1) <= Limits::StackSize);
		TestStackPtr<!bAlwaysInRage>(n);

		out.m_Writable = true;
		out.p = m_pStack + n;
		out.n = Limits::StackSize - n;
	}

	void Processor::SetPtrData(Ptr& out, Type::Size n)
	{
		Test(n <= m_Data.n);
		out.m_Writable = false;
		out.p = m_Data.p + n;
		out.n = m_Data.n - n;
	}

	Type::Size Processor::FetchSizeX(BitReader& br, bool bSizeX)
	{
		if (!bSizeX || !FetchBit(br))
			return 0;

		// 2-bit code
		uint8_t n = FetchBit(br);
		n = (n << 1) | FetchBit(br);

		return ((Type::Size) 1) << n;
	}

	int Processor::FetchOperand(BitReader& br, Ptr& out, bool bW, int nSize, Type::Size nSizeX)
	{
		bool bCanInline = !bW && (nSize >= 0);

		if (!nSize)
		{
			if (!nSizeX)
			{
				// read operand size
				int nInd = FetchOperand(br, out, false, Type::uintSize::nBytes, 0);
				out.RGet<Type::uintSize>()->Export(nSizeX);

				if (nInd)
					bCanInline = false;
				else
				{
					if (!nSizeX)
					{
						if (m_pDbg)
							*m_pDbg << '-';

						ZeroObject(out); // skipped
						return 0;
					}
				}

			}

			nSize = nSizeX;
		}

		if (bCanInline && FetchBit(br))
		{
			// inline
			out.m_Writable = false;
			out.p = Cast::NotConst(FetchInstruction(static_cast<Type::Size>(nSize)));
			out.n = nSize;

			LogValue(out);

			return 0;
		}

		FetchPtr(br, out);
		int res = 1;

		if (FetchBit(br))
		{
			LogDeref();
			// dereference
			const auto* p = out.RGet<uintBigFor<Type::Size>::Type>();
			FetchPtr(br, out, *p);

			res++;
		}

		if (bW)
			Test(out.m_Writable);

		if (nSize >= 0)
		{
			Test(out.n >= static_cast<uint32_t>(nSize));
			out.n = static_cast<uint32_t>(nSize);

			LogDeref();
			LogValue(out);
		}

		return res;
	}

#define BVMOpParamConst_w
#define BVMOpParamConst_r const
#define BVMOpParamType_1 uintBig_t<1>
#define BVMOpParamType_2 uintBig_t<2>
#define BVMOpParamType_4 uintBig_t<4>
#define BVMOpParamType_8 uintBig_t<8>
#define BVMOpParamType_x Ptr
#define BVMOpParamType_v Ptr

	struct Processor::OpCodesImpl
		:public Processor
	{
		enum class OpCode : uint8_t
		{
#define THE_MACRO(name) n_##name,
			BVM_OpCodes(THE_MACRO)
#undef THE_MACRO

			count
		};


#define THE_MACRO_ParamDecl(name, rw, len) BVMOpParamConst_##rw BVMOpParamType_##len& name##_,
#define THE_MACRO(name) void On_##name(BVMOp_##name(THE_MACRO_ParamDecl) Zero_);

		BVM_OpCodes(THE_MACRO)

#undef THE_MACRO

		void RunOnce();

		struct MyWrap_Ptr
		{
			static Ptr& get(Ptr& x) { return x; }
		};

		template <uint32_t nBytes>
		struct MyWrap_uintBig_t
		{
			static uintBig_t<nBytes>& get(Ptr& x)
			{
				assert(x.n == nBytes);
				return *reinterpret_cast<uintBig_t<nBytes>*>(x.p);
			}

			uintBig_t<nBytes>* m_Val;
			uintBig_t<nBytes>& get() { return *m_Val; }
		};
	};

	void Processor::RunOnce()
	{
		static_assert(sizeof(*this) == sizeof(OpCodesImpl));
		Cast::Up<OpCodesImpl>(*this).RunOnce();
	}

	void Processor::OpCodesImpl::RunOnce()
	{
		if (m_pDbg)
			*m_pDbg << "ip=" << Type::uintSize(m_Ip) << ", ";

		uint8_t nOpCode = *FetchInstruction(1);
		BitReader br;

		constexpr int x = 0;
		constexpr int v = -1;
		constexpr bool r = false;
		constexpr bool w = true;

		switch (nOpCode)
		{
#define THE_MACRO_ParamPass(name, rw, len) MY_TOKENIZE1(MyWrap_, BVMOpParamType_##len)::get(par##name),
#define THE_MACRO_ParamIsX(name, rw, len) (len == 0) |
#define THE_MACRO_ParamRead(name, rw, len) \
				LogVarName(#name); \
				Ptr par##name; \
				FetchOperand(br, par##name, rw, len, nSizeX); \
				LogVarEnd();

#define THE_MACRO(name) \
		case static_cast<uint8_t>(OpCode::n_##name): \
			{ \
				if (m_pDbg) \
					*m_pDbg << #name " "; \
 \
				constexpr bool bSizeX = BVMOp_##name(THE_MACRO_ParamIsX) false; \
				Type::Size nSizeX = FetchSizeX(br, bSizeX); \
				nSizeX; \
 \
				BVMOp_##name(THE_MACRO_ParamRead) \
				On_##name(BVMOp_##name(THE_MACRO_ParamPass) Zero); \
			} \
			break;

		BVM_OpCodes(THE_MACRO)
#undef THE_MACRO

		default:
			Exc::Throw(); // illegal instruction
		}

		if (m_pDbg)
			*m_pDbg << std::endl;

		Test(!br.m_Value); // unused bits must be zero
	}

#define BVM_METHOD(method) void Processor::OpCodesImpl::On_##method(BVMOp_##method(THE_MACRO_ParamDecl) Zero_)

	BVM_METHOD(mov)
	{
		Test(pDst_.n == pSrc_.n);
		memmove(pDst_.p, pSrc_.p, pSrc_.n);
	}

	BVM_METHOD(xor)
	{
		Test(pDst_.n == pSrc_.n);
		memxor(pDst_.p, pSrc_.p, pSrc_.n);
	}

	BVM_METHOD(cmp)
	{
		Test(p1_.n == p2_.n);

		int n = memcmp(p1_.p, p2_.p, p2_.n);
		m_Flags = (n < 0) ? -1 : (n > 0);
	}

	BVM_METHOD(add)
	{
		struct Dummy :public uintBigImpl {
			static uint8_t Do(uint8_t* pDst, uint32_t nDst, const uint8_t* pSrc, uint32_t nSrc) {
				return _Inc(pDst, nDst, pSrc, nSrc);
			}
		};

		m_Flags = Dummy::Do(pDst_.p, pDst_.n, pSrc_.p, pSrc_.n);
	}

	BVM_METHOD(inv)
	{
		struct Dummy :public uintBigImpl {
			static void Do(uint8_t* p, uint32_t n) {
				_Inv(p, n);
			}
		};

		Dummy::Do(pDst_.p, pDst_.n);
	}

	BVM_METHOD(inc)
	{
		struct Dummy :public uintBigImpl {
			static void Do(uint8_t* p, uint32_t n) {
				_Inc(p, n);
			}
		};

		Dummy::Do(pDst_.p, pDst_.n);
	}

	BVM_METHOD(neg)
	{
		On_inv(pDst_, Zero);
		On_inc(pDst_, Zero);
	}

	BVM_METHOD(sub)
	{
		On_neg(pDst_, Zero);
		On_add(pDst_, pSrc_, Zero);
		On_neg(pDst_, Zero);
	}

	BVM_METHOD(or)
	{
		Test(pDst_.n == pSrc_.n);

		for (uint32_t i = 0; i < pSrc_.n; i++)
			pDst_.p[i] |= pSrc_.p[i];
	}

	BVM_METHOD(and)
	{
		Test(pDst_.n == pSrc_.n);

		for (uint32_t i = 0; i < pSrc_.n; i++)
			pDst_.p[i] &= pSrc_.p[i];
	}

	BVM_METHOD(mul)
	{
		struct Dummy :public uintBigImpl {
			static void Do(uint8_t* pDst, uint32_t nDst, const uint8_t* pSrc0, uint32_t nSrc0, const uint8_t* pSrc1, uint32_t nSrc1) {
				_Mul(pDst, nDst, pSrc0, nSrc0, pSrc1, nSrc1);
			}
		};

		Dummy::Do(pDst_.p, pDst_.n, pSrc1_.p, pSrc1_.n, pSrc2_.p, pSrc2_.n);
	}

	BVM_METHOD(div)
	{
		ByteBuffer buf;
		buf.resize(pSrc1_.n * 2);

		uint8_t* pMul = buf.empty() ? nullptr : &buf.front();

		struct Dummy :public uintBigImpl {
			static void Do(uint8_t* pDst, uint32_t nDst, const uint8_t* pSrc0, uint32_t nSrc0, const uint8_t* pSrc1, uint32_t nSrc1, uint8_t* pMul, uint8_t* pTmp) {
				_Div(pDst, nDst, pSrc0, nSrc0, pSrc1, nSrc1, pMul, pTmp);
			}
		};

		Dummy::Do(pDst_.p, pDst_.n, pSrc1_.p, pSrc1_.n, pSrc2_.p, pSrc2_.n, pMul, pMul + pSrc1_.n);
	}

	BVM_METHOD(getsp)
	{
		nRes_ = m_Sp;
	}

	BVM_METHOD(jmp) {
		DoJmp(nAddr_);
	}
	BVM_METHOD(jz) {
		if (!m_Flags)
			DoJmp(nAddr_);
	}
	BVM_METHOD(jnz) {
		if (m_Flags)
			DoJmp(nAddr_);
	}
	BVM_METHOD(jg) {
		if (m_Flags > 0)
			DoJmp(nAddr_);
	}
	BVM_METHOD(jb) {
		if (m_Flags < 0)
			DoJmp(nAddr_);
	}
	BVM_METHOD(jgz) {
		if (m_Flags >= 0)
			DoJmp(nAddr_);
	}
	BVM_METHOD(jbz) {
		if (m_Flags <= 0)
			DoJmp(nAddr_);
	}

	void Processor::DoJmp(const Type::uintSize& nAddr_)
	{
		nAddr_.Export(m_Ip);
		Test(m_Ip < m_Data.n);
	}

	void Processor::PushFrame(const Type::uintSize& nFrame_)
	{
		Type::Size nFrame;
		nFrame_.Export(nFrame);

		m_Sp += nFrame;

		Ptr ptr;
		SetPtrStack(ptr, m_Sp);
		auto* pFrame = ptr.WGet<StackFrame>();

		pFrame->m_Prev = nFrame_;
		pFrame->m_RetAddr = m_Ip;

		m_Sp += sizeof(StackFrame);
		LogStackPtr();
	}

	BVM_METHOD(call)
	{
		PushFrame(nFrame_);
		DoJmp(nAddr_);
		m_FarCalls.m_Stack.back().m_LocalDepth++;
	}

	BVM_METHOD(call_far)
	{
		PushFrame(nFrame_);

		const auto* pID = pContractID_.RGet<ContractID>();
		Type::Size iMethod;
		iMethod_.Export(iMethod);

		CallFar(*pID, iMethod);
	}

	BVM_METHOD(fail) {
		Exc::Throw();
	}

	BVM_METHOD(ret)
	{
		Test(m_Sp >= sizeof(StackFrame));

		m_Sp -= sizeof(StackFrame);
		auto* pFrame = reinterpret_cast<StackFrame*>(m_pStack + m_Sp);

		Type::Size nFrame;
		pFrame->m_Prev.Export(nFrame);

		Test(m_Sp >= nFrame);
		m_Sp -= nFrame;
		LogStackPtr();

		Type::Size& nDepth = m_FarCalls.m_Stack.back().m_LocalDepth;
		if (nDepth)
			nDepth--;
		else
		{
			m_FarCalls.m_Stack.Delete(m_FarCalls.m_Stack.back());
			if (m_FarCalls.m_Stack.empty())
				return; // finished

			m_Data = m_FarCalls.m_Stack.back().m_Data;
		}

		DoJmp(pFrame->m_RetAddr);
	}

	void Processor::VarKey::Set(const ContractID& cid)
	{
		memcpy(m_p, cid.m_pData, ContractID::nBytes);
		m_Size = ContractID::nBytes;
	}

	void Processor::VarKey::Append(uint8_t nTag, const Blob& blob)
	{
		m_p[m_Size++] = nTag;

		assert(m_Size + blob.n <= _countof(m_p));
		memcpy(m_p + m_Size, blob.p, blob.n);
		m_Size += blob.n;
	}

	void Processor::SetVarKey(VarKey& vk)
	{
		vk.Set(m_FarCalls.m_Stack.back().m_Cid);
	}

	void Processor::SetVarKey(VarKey& vk, uint8_t nTag, const Blob& blob)
	{
		SetVarKey(vk);
		vk.Append(nTag, blob);
	}

	void Processor::SetVarKey(VarKey& vk, const Ptr& key, const Type::uintSize& nKey)
	{
		Type::Size nKey_;
		nKey.Export(nKey_);
		Test(nKey_ <= Limits::VarKeySize);

		SetVarKey(vk, VarKey::Tag::Internal, Blob(key.RGet<uint8_t>(nKey_), nKey_));
	}

	BVM_METHOD(load_var)
	{
		VarKey vk;
		SetVarKey(vk, pKey_, nKey_);

		Type::Size nDst;
		nDst_.Export(nDst);
		Test(nDst <= Limits::VarSize);

		LoadVar(vk, pDst_.WGet<uint8_t>(nDst), nDst);

		nDst_ = nDst;
	}

	BVM_METHOD(save_var)
	{
		VarKey vk;
		SetVarKey(vk, pKey_, nKey_);

		Type::Size nDst;
		nDst_.Export(nDst);
		Test(nDst <= Limits::VarSize);

		bool b = SaveVar(vk, pDst_.RGet<uint8_t>(nDst), nDst);
		m_Flags = !!b;
	}

	BVM_METHOD(add_sig)
	{
		if (m_pSigValidate)
			AddSigInternal(*pPubKey_.RGet<ECC::Point>());
	}

	BVM_METHOD(funds_lock)
	{
		HandleAmount(nAmount_, nAssetID_, true);
	}

	BVM_METHOD(funds_unlock)
	{
		HandleAmount(nAmount_, nAssetID_, false);
	}

	BVM_METHOD(ref_add)
	{
		HandleRef(pContractID_, true);
	}

	BVM_METHOD(ref_release)
	{
		HandleRef(pContractID_, false);
	}

	BVM_METHOD(asset_create)
	{
		Type::Size n;
		nMetaData_.Export(n);
		Test(n && (n <= Asset::Info::s_MetadataMaxSize));

		Asset::Metadata md;
		Blob(pMetaData_.RGet<uint8_t>(n), n).Export(md.m_Value);
		md.UpdateHash();

		AssetVar av;
		bvm::get_AssetOwner(av.m_Owner, m_FarCalls.m_Stack.back().m_Cid, md);

		nAid_ = AssetCreate(md, av.m_Owner);
		if (nAid_ != Zero)
		{
			HandleAmountOuter(Rules::get().CA.DepositForList, Zero, true);

			SetAssetKey(av, nAid_);
			SaveVar(av.m_vk, av.m_Owner.m_pData, static_cast<Type::Size>(av.m_Owner.nBytes));
		}
	}

	BVM_METHOD(sort)
	{
		ArrayContext ac;

		nCount_.Export(ac.m_nCount);
		nElementWidth_.Export(ac.m_nElementWidth);
		nKeyPos_.Export(ac.m_nKeyPos);
		nKeyWidth_.Export(ac.m_nKeyWidth);

		ac.Realize();

		uint8_t* p = pArray_.WGet<uint8_t>(ac.m_nSize);

		// Note! Don't call the crt/std qsort function. Its implementation may vary on different platforms (or lib versions), whereas we need 100% accurate repeatability
		// Performance is less important here
		ac.MergeSort(p);
	}

	void Processor::ArrayContext::Realize()
	{
		Test(static_cast<uint32_t>(m_nElementWidth) >= static_cast<uint32_t>(m_nKeyPos) + static_cast<uint32_t>(m_nKeyWidth));

		size_t n = m_nCount;
		n *= m_nElementWidth;

		m_nSize = static_cast<Type::Size>(n);
		Test(n == m_nSize); // overflow test
	}

	void Processor::ArrayContext::MergeSort(uint8_t* p) const
	{
		if (m_nCount <= 1)
			return;

		ByteBuffer buf;
		buf.resize(m_nSize);
		uint8_t* pDst = &buf.front();

		for (Type::Size nW = 1; nW < m_nCount; )
		{
			Type::Size n0 = 0;
			Type::Size nStepBytes = m_nElementWidth * nW;
			Type::Size nW1 = nW;

			for (Type::Size nPos = 0; ; )
			{
				nPos += nW;
				if (nPos >= m_nCount)
				{
					// just copy what's left
					memcpy(pDst + n0, p + n0, m_nSize - n0);
					break;
				}

				Type::Size n1 = n0 + nStepBytes;

				nPos += nW;
				bool bFin = (nPos > m_nCount);
				if (bFin)
					nW1 -= (nPos - m_nCount);

				MergeOnce(pDst, p, n0, nW, n1, nW1);

				if (bFin)
					break;

				n0 = n1 + nStepBytes;
			}

			nW <<= 1;
			Test(nW); // overflow test. Though can't happen with our restricted array sizes

			std::swap(p, pDst);
		}

		if (&buf.front() != pDst)
			memcpy(pDst, p, m_nSize);
	}

	void Processor::ArrayContext::MergeOnce(uint8_t* pDst, const uint8_t* pSrc, Type::Size p0, Type::Size n0, Type::Size p1, Type::Size n1) const
	{
		assert(n0 && n1);

		const uint8_t* ppSrc[] = { pSrc + p0, pSrc + p1 };
		Type::Size pN[] = { n0, n1 };

		pDst += p0;

		unsigned int iIdx = 0;
		while (true)
		{
			int n = memcmp(ppSrc[0] + m_nKeyPos, ppSrc[1] + m_nKeyPos, m_nKeyWidth);
			iIdx = (n > 0);

			memcpy(pDst, ppSrc[iIdx], m_nElementWidth);

			pDst += m_nElementWidth;
			ppSrc[iIdx] += m_nElementWidth;
			pN[iIdx]--;

			if (!pN[iIdx])
			{
				iIdx = !iIdx;
				memcpy(pDst, ppSrc[iIdx], m_nElementWidth * pN[iIdx]);
				break;
			}
		}
	}

	void Processor::SetAssetKey(AssetVar& av, const uintBigFor<Asset::ID>::Type& aid)
	{
		SetVarKey(av.m_vk);
		av.m_vk.Append(VarKey::Tag::OwnedAsset, aid);
	}

	Asset::ID Processor::get_AssetStrict(AssetVar& av, const uintBigFor<Asset::ID>::Type& aid)
	{
		SetAssetKey(av, aid);

		Type::Size n0 = static_cast<Type::Size>(av.m_Owner.nBytes);
		Type::Size n = n0;
		LoadVar(av.m_vk, av.m_Owner.m_pData, n);
		Test(n == n0);

		Asset::ID ret;
		aid.Export(ret);
		return ret;
	}

	BVM_METHOD(asset_emit)
	{
		AssetVar av;
		Asset::ID nAssetID = get_AssetStrict(av, nAid_);

		Amount val;
		nAmount_.Export(val);

		AmountSigned valS(val);
		Test(valS >= 0);

		bool bConsume = (bEmit_ == Zero);
		if (bConsume)
		{
			valS = -valS;
			Test(valS <= 0);
		}

		bool b = AssetEmit(nAssetID, av.m_Owner, valS);
		m_Flags = !!b;

		if (b)
			HandleAmountInner(nAmount_, nAid_, !bConsume);
	}

	BVM_METHOD(asset_destroy)
	{
		AssetVar av;
		Asset::ID nAssetID = get_AssetStrict(av, nAid_);

		bool b = AssetDestroy(nAssetID, av.m_Owner);
		m_Flags = !!b;

		if (b)
		{
			HandleAmountOuter(Rules::get().CA.DepositForList, Zero, false);
			SaveVar(av.m_vk, nullptr, 0);
		}
	}

#undef BVM_METHOD_BinaryVar
#undef BVM_METHOD
#undef THE_MACRO_ParamDecl
#undef THE_MACRO_IsConst_w
#undef THE_MACRO_IsConst_r

	bool Processor::LoadFixedOrZero(const VarKey& vk, uint8_t* pVal, Type::Size n)
	{
		Type::Size n0 = n;
		LoadVar(vk, pVal, n);

		if (n == n0)
			return true;

		memset0(pVal, n0);
		return false;
	}

	bool Processor::SaveNnz(const VarKey& vk, const uint8_t* pVal, Type::Size n)
	{
		return SaveVar(vk, pVal, memis0(pVal, n) ? 0 : n);
	}

	void Processor::HandleAmount(const uintBigFor<Amount>::Type& val, const uintBigFor<Asset::ID>::Type& aid, bool bLock)
	{
		HandleAmountInner(val, aid, bLock);
		HandleAmountOuter(val, aid, bLock);
	}

	void Processor::HandleAmountInner(const uintBigFor<Amount>::Type& val, const uintBigFor<Asset::ID>::Type& aid, bool bLock)
	{
		VarKey vk;
		SetVarKey(vk, VarKey::Tag::LockedAmount, aid);

		uintBigFor<Amount>::Type val0;
		Load_T(vk, val0);

		if (bLock)
		{
			val0 += val;
			Test(val0 >= val); // overflow test
		}
		else
		{
			Test(val0 >= val); // overflow test

			uintBigFor<Amount>::Type val1 = val;
			val1.Negate();
			val0 += val1;
		}

		Save_T(vk, val0);
	}

	void Processor::HandleAmountOuter(const uintBigFor<Amount>::Type& val, const uintBigFor<Asset::ID>::Type& aid, bool bLock)
	{
		if (m_pSigValidate)
		{
			Asset::ID aid_;
			aid.Export(aid_);

			Amount val_;
			val.Export(val_);

			ECC::Point::Native pt;
			CoinID::Generator(aid_).AddValue(pt, val_);

			if (bLock)
				pt = -pt;

			m_FundsIO += pt;
		}
	}

	bool Processor::HandleRefRaw(const VarKey& vk, bool bAdd)
	{
		uintBig_t<4> refs; // more than enough
		Load_T(vk, refs);

		bool ret = false;

		if (bAdd)
		{
			ret = (refs == Zero);
			refs.Inc();
			Test(refs != Zero);
		}
		else
		{
			Test(refs != Zero);
			refs.Negate();
			refs.Inv();
			ret = (refs == Zero);
		}

		Save_T(vk, refs);
		return ret;
	}

	void Processor::HandleRef(const Ptr& cid_, bool bAdd)
	{
		if (bAdd)
			m_Flags = 1;

		const auto& cid = *cid_.RGet<ContractID>();

		VarKey vk;
		SetVarKey(vk, VarKey::Tag::Refs, cid);

		if (HandleRefRaw(vk, bAdd))
		{
			// z/nnz flag changed.
			VarKey vk2;
			vk2.Set(cid);

			if (bAdd)
			{
				// make sure the target contract exists
				Type::Size nData = 0;
				LoadVar(vk2, nullptr, nData);

				if (!nData)
				{
					m_Flags = 0; // oops
					HandleRefRaw(vk, false); // undo
					return;
				}
			}

			vk2.Append(VarKey::Tag::Refs, Blob(nullptr, 0));
			HandleRefRaw(vk2, bAdd);
		}
	}

	ECC::Point::Native& Processor::AddSigInternal(const ECC::Point& pk)
	{
		(*m_pSigValidate) << pk;

		auto& ret = m_vPks.emplace_back();
		Test(ret.ImportNnz(pk));
		return ret;
	}

	void Processor::CheckSigs(const ECC::Point& pt, const ECC::Signature& sig)
	{
		if (!m_pSigValidate)
			return;

		auto& comm = AddSigInternal(pt);
		comm += m_FundsIO;

		ECC::Hash::Value hv;
		(*m_pSigValidate) >> hv;

		ECC::SignatureBase::Config cfg = ECC::Context::get().m_Sig.m_CfgG1; // copy
		cfg.m_nKeys = static_cast<uint32_t>(m_vPks.size());

		Test(Cast::Down<ECC::SignatureBase>(sig).IsValid(cfg, hv, &sig.m_k, &m_vPks.front()));
	}

	/////////////////////////////////////////////
	// Compiler

	Type::Size Compiler::ToSize(size_t n)
	{
		Type::Size ret = static_cast<Type::Size>(n);
		if (n != ret)
			Fail("overflow");

		return ret;
	}

	bool Compiler::MyBlob::IsWhitespace(char c)
	{
		switch (c)
		{
		case ' ':
		case '\t':
			return true;
		}
		return false;
	}

	void Compiler::MyBlob::ExtractToken(Buf& res, char chSep)
	{
		res.p = p;

		auto* pPtr = static_cast<uint8_t*>(memchr(p, chSep, n));
		if (pPtr)
		{
			ptrdiff_t nDiff = pPtr - p;
			res.n = static_cast<uint32_t>(nDiff);

			p = pPtr + 1;
			n -= (res.n + 1);
		}
		else
		{
			res.n = n;
			n = 0;
		}

		// delete whitespaces
		while (n && IsWhitespace(*p))
			Move1();
		while (n && IsWhitespace(p[n - 1]))
			n--;

	}

	bool Compiler::MyBlob::operator == (const char* sz) const
	{
		return
			!memcmp(p, sz, n) &&
			!sz[n];
	}

	bool Compiler::ParseOnce()
	{
		if (!m_Input.n)
			return false;

		MyBlob line1, line2;
		m_Input.ExtractToken(line1, '\n');
		m_iLine++;

		line1.ExtractToken(line2, '#'); // remove comments

		ParseLine(line2);

		if (m_BitWriter.m_Bits)
			BwFlushStrict();

		return true;
	}

	void Compiler::ParseLabel(MyBlob& x)
	{
		x.Move1();
		if (!x.n)
			Fail("");

		m_ScopesActive.back().m_Labels[x.as_Blob()].m_Refs.push_back(ToSize(m_Result.size())); // whoa!

		Type::uintSize val = Zero;
		for (uint32_t i = 0; i < val.nBytes; i++)
			m_Result.push_back(val.m_pData[i]);
	}

	uint8_t Compiler::IsPtrPrefix(char ch)
	{
		switch (ch)
		{
		case 's':
		case 'd':
		case 'p':
			return true;
		}
		return false;
	}

	uint8_t* Compiler::ParseOperand(MyBlob& line, bool bW, int nLen, Type::Size nSizeX)
	{
		bool bCanInline = !bW && (nLen >= 0);

		if (!nLen)
		{
			if (!nSizeX)
			{
				// read operand size
				uint8_t* pInl = ParseOperand(line, false, Type::uintSize::nBytes, 0);
				if (pInl)
				{
					reinterpret_cast<Type::uintSize*>(pInl)->Export(nSizeX);
					if (!nSizeX)
						return nullptr; // skipped
				}
				else
					bCanInline = false;
			}

			nLen = nSizeX;
		}

		MyBlob x;
		line.ExtractToken(x, ',');

		if (!x.n)
			Fail("");

		uint8_t p1 = *x.p;
		uint8_t bIsInline = !IsPtrPrefix(p1);
		if (bCanInline)
			BwAdd(bIsInline);

		if (bIsInline)
		{
			if (!bCanInline)
				Fail("can't inline operand");

			size_t n0 = m_Result.size();
			bool bLabel = ParseSignedNumberOrLabel(x, nLen);
			if (bLabel)
				return nullptr;

			assert(!m_Result.empty());
			return &m_Result.front() + n0;
		}


		x.Move1();

		uint8_t p2 = *x.p;
		uint8_t bIndirect = IsPtrPrefix(p2);
		if (bIndirect)
		{
			x.Move1();
			std::swap(p1, p2);
		}

		bool bLabel = ParseSignedNumberOrLabel(x, sizeof(Type::Size));
		if (bLabel && ('d' != p1))
			Fail("label pointer must reference data");

		BwAddPtrType(p1);

		BwAdd(bIndirect);
		if (bIndirect)
			BwAddPtrType(p2);

		return nullptr;
	}

	void Compiler::ParseSignedRaw(MyBlob& x, uint32_t nBytes, uintBigFor<uint64_t>::Type& val2)
	{
		uint8_t neg = (x.n && ('-' == *x.p));
		if (neg)
			x.Move1();

		uint64_t val = ParseUnsignedRaw(x);

		if ((nBytes < sizeof(val)) && (val >> (nBytes << 3)))
			Fail("overflow");

		val2 = val;
		if (neg)
			val2.Negate();
	}

	uint64_t Compiler::ParseUnsignedRaw(MyBlob& x)
	{
		uint64_t val = 0;
		while (x.n)
		{
			uint8_t c = *x.p;
			c -= '0';
			if (c > 9)
				Fail("");

			val = val * 10 + c;
			x.Move1();
		}

		return val;
	}

	void Compiler::ParseSignedNumber(MyBlob& x, uint32_t nBytes)
	{
		uintBigFor<uint64_t>::Type val2;
		ParseSignedRaw(x, nBytes, val2);

		for (uint32_t i = 0; i < nBytes; i++)
			m_Result.push_back(val2.m_pData[val2.nBytes - nBytes + i]);

	}

	bool Compiler::ParseSignedNumberOrLabel(MyBlob& x, uint32_t nBytes)
	{
		if ((sizeof(Type::Size) == nBytes) && x.n && ('.' == *x.p))
		{
			ParseLabel(x);
			return true;
		}

		ParseSignedNumber(x, nBytes);
		return false;
	}

	void Compiler::ParseHex(MyBlob& x, uint32_t nBytes)
	{
		struct Dummy :public uintBigImpl {
			static uint32_t Do(uint8_t* pDst, const char* sz, uint32_t nTxtLen) {
				return _Scan(pDst, sz, nTxtLen);
			}
		};

		uint32_t nTxtLen = nBytes * 2;
		if (x.n != nTxtLen)
			Fail("hex size mismatch");

		if (!nBytes)
			return;

		size_t n0 = m_Result.size();
		m_Result.resize(n0 + nBytes);
		if (Dummy::Do(&m_Result.front() + n0, (const char*) x.p, nTxtLen) != nTxtLen)
			Fail("hex parse");
	}

	void Compiler::ParseLine(MyBlob& line)
	{
		MyBlob opcode;
		line.ExtractToken(opcode, ' ');
		if (!opcode.n)
			return;

		if ('.' == *opcode.p)
		{
			opcode.Move1();

			if (!opcode.n)
				Fail("empty label");

			Label& x = m_ScopesActive.back().m_Labels[opcode.as_Blob()];
			if (Label::s_Invalid != x.m_Pos)
				Fail("duplicated label");

			x.m_Pos = ToSize(m_Result.size());

			return;
		}

		if (opcode == "{")
		{
			ScopeOpen();
			return;
		}

		if (opcode == "}")
		{
			ScopeClose();
			return;
		}

		if (opcode == "const")
		{
			line.ExtractToken(opcode, ' ');

			if (!opcode.n)
				Fail("");

			char nTag = *opcode.p;
			opcode.Move1();

			switch (nTag)
			{
			case 'u':
				{
					uint64_t nBytes = ParseUnsignedRaw(opcode);
					if (nBytes > sizeof(nBytes))
						Fail("");

					ParseSignedNumber(line, static_cast<uint32_t>(nBytes));
				}
				break;

			case 'h':
				{
					uint64_t nBytes = ParseUnsignedRaw(opcode);
					ParseHex(line, static_cast<uint32_t>(nBytes));
				}
				break;

			default:
				Fail("invalid const type");
			}

			return;
		}

		BitWriter bw;

		constexpr int x = 0;
		constexpr int v = -1;
		constexpr bool r = false;
		constexpr bool w = true;

		Type::Size nSizeX = ParseSizeX(opcode);
		nSizeX;

#define THE_MACRO_ParamCompile(name, rw, len) ParseOperand(line, rw, len, nSizeX);
#define THE_MACRO(name) \
		if (opcode == #name) \
		{ \
			m_Result.push_back(static_cast<uint8_t>(Processor::OpCodesImpl::OpCode::n_##name)); \
 \
			constexpr bool bSizeX = BVMOp_##name(THE_MACRO_ParamIsX) false; \
			WriteSizeX(nSizeX, bSizeX); \
 \
			BVMOp_##name(THE_MACRO_ParamCompile) \
			return; \
		}

		BVM_OpCodes(THE_MACRO)
#undef THE_MACRO
#undef THE_MACRO_ParamCompile

		Fail("Invalid instruction");
	}

	Type::Size Compiler::ParseSizeX(MyBlob& opcode)
	{
		if (!opcode.n)
			return 0;

		uint8_t val = opcode.p[opcode.n - 1] - '0';
		switch (val)
		{
		case 1:
		case 2:
		case 4:
		case 8:
			break;

		default:
			return 0;
		}

		opcode.n--;
		return val;
	}

	void Compiler::WriteSizeX(Type::Size n, bool b)
	{
		if (!b)
		{
			if (n)
				Fail("size modifier isn't needed for this opcode");
			return;
		}

		BwAdd(n >= 0);
		if (n < 0)
			return;

		BwAdd(n >= 4);
		if (n >= 4)
			n >>= 2;
		BwAdd(n >= 2);
	}

	void Compiler::BwFlushStrict()
	{
		(&m_Result.front())[m_BitWriter.m_Pos] = m_BitWriter.m_Value;
		m_BitWriter.m_Bits = 0;
	}

	void Compiler::BwAddPtrType(uint8_t p)
	{
		uint8_t n = ('s' != p);
		BwAdd(n);

		if (n)
			BwAdd('d' == p);
	}

	void Compiler::BwAdd(uint8_t x)
	{
		switch (m_BitWriter.m_Bits)
		{
		case 8:
			BwFlushStrict();
			// no break;

		case 0:
			m_BitWriter.m_Value = 0;
			m_BitWriter.m_Pos = ToSize(m_Result.size());
			m_Result.push_back(0);
		}

		m_BitWriter.m_Value |= (x << m_BitWriter.m_Bits);
		m_BitWriter.m_Bits++;
	}

	void Compiler::ScopeOpen()
	{
		m_ScopesActive.Create_back();
	}

	void Compiler::ScopeClose()
	{
		if (m_ScopesActive.empty())
			Fail("no scope");

		Scope& s = m_ScopesActive.back();

		Scope* pPrev = nullptr;
		auto itScope = Scope::List::s_iterator_to(s);
		if (m_ScopesActive.begin() != itScope)
			pPrev = &(* --itScope);

		for (auto it = s.m_Labels.begin(); s.m_Labels.end() != it; )
		{
			Label& lbl = *it++;

			if (lbl.s_Invalid == lbl.m_Pos)
			{
				// unresolved, move it into outer scope
				if (!pPrev)
					Fail("unresolved label");

				auto itDup = pPrev->m_Labels.find(lbl);
				if (pPrev->m_Labels.end() != itDup)
				{
					for (; !lbl.m_Refs.empty(); lbl.m_Refs.pop_front())
						itDup->m_Refs.push_back(std::move(lbl.m_Refs.front()));

					s.m_Labels.Delete(lbl);
				}
				else
				{
					s.m_Labels.erase(Label::Map::s_iterator_to(lbl));
					pPrev->m_Labels.insert(lbl);
				}
			}
		}

		m_ScopesActive.erase(Scope::List::s_iterator_to(s));
		m_ScopesDone.push_back(s);
	}

	void Compiler::Start()
	{
		assert(m_ScopesActive.empty());
		ScopeOpen();
	}

	void Compiler::Finalyze()
	{
		ScopeClose();
		if (!m_ScopesActive.empty())
			Fail("unclosed scopes");

		Scope& s = m_ScopesDone.back();

#define BVM_PUBLIC_LABEL "method_"

		char szLabel[_countof(BVM_PUBLIC_LABEL) + 5] = BVM_PUBLIC_LABEL;
		// count public labels
		Type::Size nLabels = 0;
		for (; ; nLabels++)
		{
			Blob b;
			b.p = szLabel;
			b.n = static_cast<uint32_t>(_countof(BVM_PUBLIC_LABEL) - 1);
			b.n += sprintf(szLabel + _countof(BVM_PUBLIC_LABEL) - 1, "%u", nLabels);

			auto it = s.m_Labels.find(b, Label::Comparator());
			if (s.m_Labels.end() == it)
				break;
		}

		if (nLabels < Header::s_MethodsMin)
			Fail("too few methods");

		size_t nSizeHeader = sizeof(Header) + sizeof(Header::MethodEntry) * (nLabels - Header::s_MethodsMin);

		{
			ByteBuffer buf = std::move(m_Result);
			m_Result.resize(buf.size() + nSizeHeader);

			if (!buf.empty())
				memcpy(&m_Result.front() + nSizeHeader, &buf.front(), buf.size());
		}

		Header& hdr = reinterpret_cast<Header&>(m_Result.front());
		hdr.m_Version = Header::s_Version;
		hdr.m_NumMethods = nLabels;

		for (nLabels = 0; ; nLabels++)
		{
			Blob b;
			b.p = szLabel;
			b.n = static_cast<uint32_t>(_countof(BVM_PUBLIC_LABEL) - 1);
			b.n += sprintf(szLabel + _countof(BVM_PUBLIC_LABEL) - 1, "%u", nLabels);

			auto it = s.m_Labels.find(b, Label::Comparator());
			if (s.m_Labels.end() == it)
				break;

			hdr.m_pMethod[nLabels] = ToSize(nSizeHeader + it->m_Pos);
		}

		for (auto itS = m_ScopesDone.begin(); m_ScopesDone.end() != itS; itS++)
		{
			for (auto itL = itS->m_Labels.begin(); itS->m_Labels.end() != itL; itL++)
			{
				Label& x = *itL;
				assert(Label::s_Invalid != x.m_Pos);

				Type::uintSize addr = ToSize(nSizeHeader + x.m_Pos);

				for (auto it2 = x.m_Refs.begin(); x.m_Refs.end() != it2; it2++)
					*reinterpret_cast<Type::uintSize*>(&m_Result.front() + nSizeHeader + *it2) = addr;
			}
		}

	}


} // namespace bvm
} // namespace beam
