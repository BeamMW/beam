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

	void Processor::InitStack(const Buf& args, uint8_t nFill /* = 0 */)
	{
		Test(args.n <= Limits::StackSize - sizeof(StackFrame));
		memcpy(m_pStack, args.p, args.n);
		memset(m_pStack + args.n, nFill, Limits::StackSize - args.n);

		memset0(m_pStack + args.n, sizeof(StackFrame));
		m_Sp = static_cast<Type::Size>(Limits::DataSize + args.n + sizeof(StackFrame));
		LogStackPtr();

		m_Ip = 0;
	}

	void Processor::LogStackPtr()
	{
		if (m_pDbg)
			*m_pDbg << "sp=" << Type::uintSize(m_Sp) << std::endl;
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

		const auto* pHdr = ptr.Get<Header>();

		Type::Size n;
		pHdr->m_Version.Export(n);
		Test(Header::s_Version == n);

		pHdr->m_NumMethods.Export(n);
		Test(iMethod < n);

		Test(ptr.Move(sizeof(Header) - sizeof(Header::MethodEntry) * Header::s_MethodsMin + sizeof(Header::MethodEntry) * iMethod));

		DoJmp(*ptr.Get<Header::MethodEntry>());
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

	bool Processor::FetchPtr(Ptr& out, Type::Size nAddr)
	{
		bool bData = (nAddr < Limits::DataSize);
		if (bData)
		{
			Test(nAddr <= m_Data.n);

			out.p = m_Data.p + nAddr;
			out.n = m_Data.n - nAddr;
		}
		else
			SetPtrStack(out, nAddr);

		return bData;
	}

	void Processor::LogOpCode(const char* szName)
	{
		if (m_pDbg)
			*m_pDbg << szName << ' ';
	}

	void Processor::LogOpResults(bool bHasW)
	{
		if (m_pDbg && bHasW)
			*m_pDbg << " res: ";
	}

	void Processor::LogVarResult(const char* szName, const Ptr& ptr)
	{
		LogVarName(szName);
		LogValue(ptr);
		LogVarEnd();
	}

	void Processor::LogVarName(const char* szName)
	{
		if (m_pDbg)
			*m_pDbg << szName << "=";
	}

	void Processor::LogVarEnd()
	{
		if (m_pDbg)
			*m_pDbg << ", ";
	}

	void Processor::LogValue(const Ptr& x)
	{
		if (m_pDbg)
		{
			if (x.n)
				*m_pDbg << uintBigImpl::_Str(x.p, x.n); // dynamically allocates str of needed length
			else
				*m_pDbg << '-';
		}
	}

	Type::Size Processor::get_StackOffset(Type::Size n)
	{
		n -= Limits::DataSize;
		Test(n <= Limits::StackSize);
		return n;
	}

	void Processor::SetPtrStack(Ptr& out, Type::Size n)
	{
		n = get_StackOffset(n);
		out.p = m_pStack + n;
		out.n = Limits::StackSize - n;
	}

	Type::Size Processor::FetchSizeX(BitReader& br, bool bSizeX)
	{
		Type::Size ret = 0;
		if (bSizeX)
		{
			if (FetchBit(br))
			{
				// 2-bit code
				uint8_t n = FetchBit(br);
				n = (n << 1) | FetchBit(br);

				ret = ((Type::Size) 1) << n;
				if (m_pDbg)
					*m_pDbg << ret;
			}
			else
			{
				if (m_pDbg)
					*m_pDbg << '-';

				FetchSize(br, ret);
			}
		}

		if (m_pDbg)
			*m_pDbg << ' ';

		return ret;
	}

	int Processor::FetchSize(BitReader& br, Type::Size& ret)
	{
		Operand out;
		int nInd = FetchOperand(br, out, false, Type::uintSize::nBytes, 0);

		out.Get<Type::uintSize>()->Export(ret);
		return nInd;
	}

	template <int nSize>
	struct Processor::ParamType
		:public uintBig_t<nSize>
	{
		typedef uintBig_t<nSize> Type;
		static Type& get(Ptr& x)
		{
			assert(x.n == nBytes);
			return *reinterpret_cast<Type*>(x.p);
		}
	};

	template <>
	struct Processor::ParamType<0>
		:public Processor::Ptr
	{
		typedef Processor::Ptr Type;

		static Type& get(Ptr& x) { return x; }
	};

	template <>
	struct Processor::ParamType<-1>
		:public Processor::ParamType<0>
	{
	};

	template <>
	struct Processor::ParamType<-2>
		:public Processor::ParamType<0>
	{
	};

#define BVMOpParamConst_w
#define BVMOpParamConst_r const

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

		static constexpr int x = 0;
		static constexpr int v = -1;
		static constexpr int flexible = -2;

#define THE_MACRO_ParamDecl(name, rw, len) BVMOpParamConst_##rw typename ParamType<len>::Type& name##_,
#define THE_MACRO(name) void On_##name(BVMOp_##name(THE_MACRO_ParamDecl) Zero_);

		BVM_OpCodes(THE_MACRO)

#undef THE_MACRO

		void RunOnce();
	};


	int Processor::FetchOperand(BitReader& br, Operand& out, bool bW, int nSize, Type::Size nSizeX)
	{
		bool bCanInline = !bW && (OpCodesImpl::v != nSize);

		switch (nSize)
		{
		case OpCodesImpl::flexible:
			if (m_pDbg)
				*m_pDbg << '(';

			nSize = FetchSize(br, nSizeX);
			// reuse variable nSize. It's actually the indirection count while reading the size

			if (m_pDbg)
				*m_pDbg << ") ";

			if (nSize)
				bCanInline = false; // size was written indirectly, means the operand size was not known at compile time
			else
			{
				if (!nSizeX)
				{
					ZeroObject(out); // skipped
					return 0;
				}
			}

			// no break;

		case OpCodesImpl::x:
			nSize = nSizeX;
		}

		if (bCanInline && FetchBit(br))
		{
			// inline
			out.p = Cast::NotConst(FetchInstruction(static_cast<Type::Size>(nSize)));
			out.n = nSize;

			if ((Type::uintSize::nBytes == static_cast<Type::Size>(nSize)) && FetchBit(br))
			{
				// taking address of a local variable
				out.m_Aux = *out.Get<Type::uintSize>();

				if (m_pDbg)
					*m_pDbg << "sp+" << out.m_Aux << "=";

				out.m_Aux += Type::uintSize(m_Sp);

				out.p = out.m_Aux.m_pData;
				out.n = out.m_Aux.nBytes;
			}

			LogValue(out);

			return 0;
		}

		int nInd = 1; // num of indirections
		bool bReadOnly = true;

		Type::Size nAddr = FetchBit(br) ? m_Sp : 0; // 1st indirection may account for sp, it's a typical access of a local variable

		for (; ; nInd++)
		{
			const auto* pOffs = reinterpret_cast<const Type::uintSize*>(FetchInstruction(Type::uintSize::nBytes));

			if (m_pDbg)
				*m_pDbg << uintBigFrom(nAddr) << '+' << *pOffs << '=';

			Type::Size nOffs;
			pOffs->Export(nOffs);
			nAddr += nOffs;

			if (m_pDbg)
				*m_pDbg << uintBigFrom(nAddr) << "=>";

			bReadOnly = FetchPtr(out, nAddr);

			if (!FetchBit(br))
				break; // no more indirections

			out.Get<Type::uintSize>()->Export(nAddr); // dereference operation
		}

		if (bW)
			Test(!bReadOnly);

		if (nSize >= 0)
		{
			Test(out.n >= static_cast<uint32_t>(nSize));
			out.n = static_cast<uint32_t>(nSize);

			LogValue(out);
		}

		return nInd;
	}

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

		constexpr bool r = false;
		constexpr bool w = true;

		switch (nOpCode)
		{
#define THE_MACRO_ParamPass(name, rw, len) ParamType<len>::get(par##name),
#define THE_MACRO_ParamIsX(name, rw, len) (len == 0) |
#define THE_MACRO_ParamIsPrintResult(name, rw, len) (rw && (v != len)) |
#define THE_MACRO_ParamRead(name, rw, len) \
				LogVarName(#name); \
				Operand par##name; \
				FetchOperand(br, par##name, rw, len, nSizeX); \
				LogVarEnd();

#define THE_MACRO_ParamPrintResult(name, rw, len) if constexpr (rw && (v != len)) LogVarResult(#name, par##name);

#define THE_MACRO(name) \
		case static_cast<uint8_t>(OpCode::n_##name): \
			{ \
				LogOpCode(#name); \
 \
				constexpr bool bSizeX = BVMOp_##name(THE_MACRO_ParamIsX) false; \
				Type::Size nSizeX = FetchSizeX(br, bSizeX); \
				nSizeX; \
 \
				BVMOp_##name(THE_MACRO_ParamRead) \
				On_##name(BVMOp_##name(THE_MACRO_ParamPass) Zero); \
 \
				constexpr bool bLogRes = BVMOp_##name(THE_MACRO_ParamIsPrintResult) false; \
				LogOpResults(bLogRes); \
				BVMOp_##name(THE_MACRO_ParamPrintResult) \
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
		assert(pDst_.n == pSrc_.n);
		memmove(pDst_.p, pSrc_.p, pSrc_.n);
	}

	BVM_METHOD(xor)
	{
		assert(pDst_.n == pSrc_.n);
		memxor(pDst_.p, pSrc_.p, pSrc_.n);
	}

	BVM_METHOD(cmp)
	{
		assert(p1_.n == p2_.n);

		int n = memcmp(p1_.p, p2_.p, p2_.n);
		m_Flags = (n < 0) ? -1 : (n > 0);
	}

	BVM_METHOD(add)
	{
		m_Flags = uintBigImpl::_Inc(pDst_.p, pDst_.n, pSrc_.p, pSrc_.n);
	}

	BVM_METHOD(inv)
	{
		uintBigImpl::_Inv(pDst_.p, pDst_.n);
	}

	BVM_METHOD(inc)
	{
		uintBigImpl::_Inc(pDst_.p, pDst_.n);
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
		assert(pDst_.n == pSrc_.n);

		for (uint32_t i = 0; i < pSrc_.n; i++)
			pDst_.p[i] |= pSrc_.p[i];
	}

	BVM_METHOD(and)
	{
		assert(pDst_.n == pSrc_.n);

		for (uint32_t i = 0; i < pSrc_.n; i++)
			pDst_.p[i] &= pSrc_.p[i];
	}

	BVM_METHOD(mul)
	{
		uintBigImpl::_Mul(pDst_.p, pDst_.n, pSrc1_.p, pSrc1_.n, pSrc2_.p, pSrc2_.n);
	}

	BVM_METHOD(div)
	{
		ByteBuffer buf;
		buf.resize(pSrc1_.n * 2);

		uint8_t* pMul = buf.empty() ? nullptr : &buf.front();

		uintBigImpl::_Div(pDst_.p, pDst_.n, pSrc1_.p, pSrc1_.n, pSrc2_.p, pSrc2_.n, pMul, pMul + pSrc1_.n);
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
		auto* pFrame = ptr.Get<StackFrame>();

		pFrame->m_Prev = nFrame_;
		pFrame->m_RetAddr = m_Ip;

		m_Sp += sizeof(StackFrame);
		get_StackOffset(m_Sp); // just test
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

		Type::Size iMethod;
		iMethod_.Export(iMethod);

		CallFar(cid_, iMethod);
	}

	BVM_METHOD(fail) {
		Exc::Throw();
	}

	BVM_METHOD(ret)
	{
		m_Sp -= sizeof(StackFrame);
		Ptr ptr;
		SetPtrStack(ptr, m_Sp);

		auto* pFrame = ptr.Get<StackFrame>();

		Type::Size nFrame;
		pFrame->m_Prev.Export(nFrame);

		m_Sp -= nFrame;
		get_StackOffset(m_Sp); // just test
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

	void Processor::SetVarKey(VarKey& vk, const Ptr& key)
	{
		Test(key.n <= Limits::VarKeySize);

		SetVarKey(vk, VarKey::Tag::Internal, Blob(key.p, key.n));
	}

	BVM_METHOD(load_var)
	{
		VarKey vk;
		SetVarKey(vk, pKey_);

		Type::Size nVal = static_cast<Type::Size>(pVal_.n);
		LoadVar(vk, pVal_.p, nVal);

		nActualSize_ = nVal;
	}

	BVM_METHOD(save_var)
	{
		VarKey vk;
		SetVarKey(vk, pKey_);

		Test(pVal_.n <= Limits::VarSize);

		bool b = SaveVar(vk, pVal_.p, static_cast<Type::Size>(pVal_.n));
		m_Flags = !!b;
	}

	BVM_METHOD(add_sig)
	{
		if (m_pSigValidate)
			AddSigInternal(Cast::Reinterpret<ECC::Point>(pk_));
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
		HandleRef(cid_, true);
	}

	BVM_METHOD(ref_release)
	{
		HandleRef(cid_, false);
	}

	BVM_METHOD(asset_create)
	{
		Test(pMetaData_.n && (pMetaData_.n <= Asset::Info::s_MetadataMaxSize));

		Asset::Metadata md;
		Blob(pMetaData_.p, pMetaData_.n).Export(md.m_Value);
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

		uint8_t* p = pArray_.Get<uint8_t>(ac.m_nSize);

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

		AmountBig::Type val0;
		Load_T(vk, val0);

		if (bLock)
		{
			val0 += val;
			Test(val0 >= val); // overflow test
		}
		else
		{
			Test(val0 >= val); // overflow test

			val0.Negate();
			val0 += val;
			val0.Negate();
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

	void Processor::HandleRef(const ContractID& cid, bool bAdd)
	{
		if (bAdd)
			m_Flags = 1;

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
		case 'p':
			return true;
		}
		return false;
	}

	Compiler::Variable* Compiler::LookupVar(MyBlob& name)
	{
		for (auto it = m_ScopesActive.rbegin(); m_ScopesActive.rend() != it; it++)
		{
			auto& var = it->m_mapVars[name.as_Blob()];
			if (var.IsValid())
				return &var;
		}

		return nullptr;
	}

	uint32_t Compiler::RemoveDerefs(MyBlob& x)
	{
		uint32_t i = 0;
		for (; x.n && ('*' == *x.p); i++)
			x.Move1();
		return i;
	}

	bool Compiler::ParseVariableAccess(MyBlob x, VarAccess& res)
	{
		MyBlob name0;
		x.ExtractToken(name0, '.');
		uint32_t nDeref = RemoveDerefs(name0);

		Variable* pVar = LookupVar(name0);
		if (!pVar)
			return false;

		Type::Size nPos = 0;

		while (true)
		{
			nPos += pVar->m_Pos;
			bool bPtr = (nDeref < pVar->m_nPtrs);

			if (nDeref > pVar->m_nPtrs)
				Fail("too many derefs");

			while (nDeref--)
			{
				res.m_Indirections.push_back(nPos);
				nPos = 0;
			}

			// ...

			x.ExtractToken(name0, '.');
			if (!name0.n)
			{
				res.m_Indirections.push_back(nPos);
				res.m_Size = bPtr ? Type::uintSize::nBytes : pVar->m_Size;
				break;
			}

			nDeref = RemoveDerefs(name0);

			auto* pType = pVar->m_pType;
			if (!pType)
				Fail("not a struct");

			auto it = pType->m_mapFields.find(name0.as_Blob());
			if (pType->m_mapFields.end() == it)
				Fail("field not found");

			pVar = &(it->second);
		}

		return true;
	}

	uint8_t* Compiler::ParseOperand(MyBlob& line, bool bW, int nLen, Type::Size nSizeX)
	{
		bool bCanInline = !bW && (Processor::OpCodesImpl::v != nLen);

		switch (nLen)
		{
		case Processor::OpCodesImpl::flexible:

			if (ParseSize(line, nSizeX))
			{
				if (!nSizeX)
					return nullptr; // skip
			}
			else
				bCanInline = false;
			// no break;

		case Processor::OpCodesImpl::x:
			nLen = nSizeX;
		}

		MyBlob x;
		line.ExtractToken(x, ',');

		if (!x.n)
			Fail("operand expected");

		uint8_t nMod = *x.p;
		bool bMod = false;
		switch (nMod)
		{
		case '@': // sizeof
		case '&': // addr of a local var
			x.Move1();
			bMod = true;
		}

		VarAccess va;
		if (ParseVariableAccess(x, va))
		{
			assert(!va.m_Indirections.empty());

			if (nLen > 0)
			{
				Type::Size nSizeMod = bMod ? Type::uintSize::nBytes : va.m_Size;
				if (static_cast<Type::Size>(nLen) != nSizeMod)
					Fail("operand size mismatch");
			}

			if (bCanInline)
				BwAdd(!!bMod); // bIsInline

			if (bMod)
			{
				if (!bCanInline)
					Fail("can't inline operand");

				Type::uintSize val;
				uint8_t bSizeOf = ('@' == nMod);
				if (bSizeOf)
					val = va.m_Size;
				else
				{
					if (1 != va.m_Indirections.size())
						Fail("can't get address");
					val = va.m_Indirections.front();
				}

				size_t n0 = m_Result.size();
				WriteFlexible(val, Type::uintSize::nBytes);

				BwAdd(!bSizeOf); // should add sp?

				return bSizeOf ? &m_Result.front() + n0 : nullptr;
			}
			else
			{
				BwAdd(1); // should add sp?
				for (size_t i = 0; ; )
				{
					Type::uintSize val = va.m_Indirections[i];
					WriteFlexible(val, Type::uintSize::nBytes);

					uint8_t nMore = (++i < va.m_Indirections.size());
					BwAdd(nMore);
					if (!nMore)
						break;
				}

			}

			return nullptr;
		}
		else
		{
			if (bMod)
				Fail("modifiers allowed on variables only");
		}

		if (!bCanInline || !nLen)
			Fail("can't inline operand");


		BwAdd(1); // inline

		size_t n0 = m_Result.size();
		bool bLabel = ParseSignedNumberOrLabel(x, nLen);

		if ((Type::uintSize::nBytes == static_cast<Type::Size>(nLen)))
			BwAdd(0); // not taking addr of local var

		if (bLabel)
			return nullptr;

		assert(!m_Result.empty());
		return &m_Result.front() + n0;
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
		WriteFlexible(val2, nBytes);
	}

	void Compiler::WriteFlexible(const uint8_t* pSrc, uint32_t nSizeSrc, uint32_t nSizeDst)
	{
		for (; nSizeDst > nSizeSrc; nSizeDst--)
			m_Result.push_back(0);

		pSrc += (nSizeSrc - nSizeDst);

		for (uint32_t i = 0; i < nSizeDst; i++)
			m_Result.push_back(pSrc[i]);
	}

	bool Compiler::ParseSignedNumberOrLabel(MyBlob& x, uint32_t nBytes)
	{
		if (x.n)
		{
			if ('.' == *x.p)
			{
				if (sizeof(Type::Size) != nBytes)
					Fail("Label size mismatch");

				ParseLabel(x);
				return true;
			}

			if (x == "local_size")
			{
				Scope& s = m_ScopesActive.back();
				Type::uintSize val = s.m_nSizeLocal;
				WriteFlexible(val, nBytes);
				return true;
			}
		}


		ParseSignedNumber(x, nBytes);
		return false;
	}

	void Compiler::ParseHex(MyBlob& x, uint32_t nBytes)
	{
		uint32_t nTxtLen = nBytes * 2;
		if (x.n != nTxtLen)
			Fail("hex size mismatch");

		if (!nBytes)
			return;

		size_t n0 = m_Result.size();
		m_Result.resize(n0 + nBytes);
		if (uintBigImpl::_Scan(&m_Result.front() + n0, (const char*) x.p, nTxtLen) != nTxtLen)
			Fail("hex parse");
	}

	Compiler::Struct* Compiler::FindType(const MyBlob& x)
	{
		for (auto it = m_ScopesActive.rbegin(); m_ScopesActive.rend() != it; it++)
		{
			Scope& s = *it;
			auto itS = s.m_mapStructs.find(x.as_Blob(), Struct::Comparator());
			if (s.m_mapStructs.end() != itS)
				return &(*itS);
		}

		return nullptr;
	}

	Compiler::Struct* Compiler::ParseVariableType(MyBlob& line, Type::Size& res, char& nTag)
	{
		MyBlob var;
		line.ExtractToken(var, ' ');

		if (!var.n)
			Fail("type expected");

		// lookup struct type first
		Struct* pType = FindType(var);
		if (pType)
		{
			res = pType->m_Size;
			nTag = 0;
			return pType;
		}

		nTag = *var.p;
		var.Move1();

		switch (nTag)
		{
		case 'u':
		case 'h':
		case 'b':
			break;

		default:
			Fail("invalid const type");
		}

		res = static_cast<Type::Size>(ParseUnsignedRaw(var));
		//if (!res)
		//	Fail("type size can't be zero");
		return nullptr;
	}

	Compiler::Struct* Compiler::ParseVariableDeclarationRaw(MyBlob& line, MyBlob& name, Type::Size& nVarSize, uint32_t& nPtrs)
	{
		char nTag;
		Struct* pType = ParseVariableType(line, nVarSize, nTag);

		line.ExtractToken(name, ' ');
		if (!name.n)
			Fail("variable name expected");

		nPtrs = RemoveDerefs(name);

		return pType;
	}

	void Compiler::ParseVariableDeclaration(MyBlob& line, bool bArg)
	{
		MyBlob name;
		Type::Size nVarSize;
		uint32_t nPtrs;
		Struct* pType = ParseVariableDeclarationRaw(line, name, nVarSize, nPtrs);

		auto& s = m_ScopesActive.back();
		auto& x = s.m_mapVars[name.as_Blob()];
		if (x.IsValid())
			Fail("variable name duplicated");

		x.m_Size = nVarSize;
		x.m_nPtrs = nPtrs;
		x.m_pType = pType;

		if (nPtrs)
			nVarSize = Type::uintSize::nBytes;

		if (bArg)
		{
			s.m_nSizeArgs += nVarSize;
			x.m_Pos = static_cast<Type::Size>(-s.m_nSizeArgs);
		}
		else
		{
			x.m_Pos = s.m_nSizeLocal;
			s.m_nSizeLocal += nVarSize;
		}
	}


	void Compiler::ParseLine(MyBlob& line)
	{
		MyBlob opcode;
		line.ExtractToken(opcode, ' ');
		if (!opcode.n)
			return;

		Scope& s = m_ScopesActive.back();

		if (m_InsideStructDef)
		{
			assert(1 == s.m_mapStructs.size());
			auto& st = *s.m_mapStructs.begin();

			if (opcode == "}")
			{
				if (st.m_mapFields.empty())
					Fail("empty struct");

				Scope* pPrev = m_ScopesActive.get_Prev(s);
				assert(pPrev);

				s.m_mapStructs.erase(Struct::Map::s_iterator_to(st));
				pPrev->m_mapStructs.insert(st);

				ScopeClose();
				m_InsideStructDef = false;
			}
			else
			{
				line.n += static_cast<uint32_t>(line.p - opcode.p);
				line.p = opcode.p;

				Type::Size nSize;
				uint32_t nPtrs;
				Struct* pType = ParseVariableDeclarationRaw(line, opcode, nSize, nPtrs);

				auto& field = st.m_mapFields[opcode.as_Blob()];
				if (field.IsValid())
					Fail("duplicate field name");

				field.m_Pos = st.m_Size;
				field.m_Size = nSize;
				field.m_nPtrs = nPtrs;
				field.m_pType = pType;
				st.m_Size += nPtrs ? Type::uintSize::nBytes : nSize;
			}

			return;
		}

		if (opcode == "struct")
		{
			line.ExtractToken(opcode, ' ');
			if (!opcode.n)
				Fail("struct name expected");

			if (!line.n || ('{' != *line.p))
				Fail("struct open '{' expected");

			auto it = s.m_mapStructs.find(opcode.as_Blob(), Struct::Comparator());
			if (s.m_mapStructs.end() != it)
				Fail("duplicate struct name");

			ScopeOpen();
			Scope& s2 = m_ScopesActive.back();
			s2.m_mapStructs.Create(opcode.as_Blob());

			m_InsideStructDef = true;

			return;
		}

		if ('.' == *opcode.p)
		{
			opcode.Move1();

			if (!opcode.n)
				Fail("empty label");

			Label& x = s.m_Labels[opcode.as_Blob()];
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
			Type::Size nVarSize;
			char nTag;
			ParseVariableType(line, nVarSize, nTag);

			switch (nTag)
			{
			case 'u':
				if (nVarSize > sizeof(uint64_t))
					Fail("const too big");
				ParseSignedNumber(line, nVarSize);
				break;

			case 'h':
			case 0: // struct, treat as hex
				ParseHex(line, nVarSize);
				break;

			default:
				Fail("invalid const type");
			}

			return;
		}

		if (opcode == "arg")
		{
			ParseVariableDeclaration(line, true);
			return;
		}

		if (opcode == "var")
		{
			ParseVariableDeclaration(line, false);
			return;
		}

		BitWriter bw;

		constexpr int x = Processor::OpCodesImpl::x;
		constexpr int v = Processor::OpCodesImpl::v;
		constexpr int flexible = Processor::OpCodesImpl::flexible;
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
			WriteSizeX(line, nSizeX, bSizeX); \
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

	void Compiler::WriteSizeX(MyBlob& line, Type::Size& n, bool b)
	{
		if (!b)
		{
			if (n)
				Fail("size modifier isn't needed for this opcode");
			return;
		}

		BwAdd(n > 0);
		if (n)
		{
			Type::Size val = n;
			BwAdd(val >= 4);
			if (val >= 4)
				val >>= 2;
			BwAdd(val >= 2);
		}
		else
			ParseSize(line, n);
	}

	bool Compiler::ParseSize(MyBlob& line, Type::Size& ret)
	{
		uint8_t* pInl = ParseOperand(line, false, Type::uintSize::nBytes, 0);
		if (!pInl)
			return false;

		reinterpret_cast<Type::uintSize*>(pInl)->Export(ret);
		return true;
	}

	void Compiler::BwFlushStrict()
	{
		(&m_Result.front())[m_BitWriter.m_Pos] = m_BitWriter.m_Value;
		m_BitWriter.m_Bits = 0;
	}

	void Compiler::BwAddPtrType(uint8_t p)
	{
		uint8_t n = ('s' == p);
		BwAdd(n);
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
		Scope* pPrev = m_ScopesActive.empty() ? nullptr : &m_ScopesActive.back();
		m_ScopesActive.Create_back();

		if (pPrev)
		{
			Scope& s = m_ScopesActive.back();
			s.m_nSizeArgs = pPrev->m_nSizeArgs;
			s.m_nSizeLocal = pPrev->m_nSizeLocal;
		}
	}

	Compiler::Scope* Compiler::Scope::List::get_Prev(Scope& s)
	{
		auto it = s_iterator_to(s);
		if (begin() == it)
			return nullptr;
		return &(*--it);
	}


	void Compiler::ScopeClose()
	{
		if (m_ScopesActive.empty())
			Fail("no scope");

		Scope& s = m_ScopesActive.back();
		Scope* pPrev = m_ScopesActive.get_Prev(s);

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
