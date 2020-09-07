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
#include <sstream>

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

	void Processor::FarCalls::Stack::Clear()
	{
		while (!empty())
			Pop();
	}

	void Processor::FarCalls::Stack::Pop()
	{
		auto* p = &back();
		pop_back();
		delete p;
	}

	void Processor::CallFar(const ContractID& cid, Type::Size iMethod)
	{
		Test(m_FarCalls.m_Stack.size() < Limits::FarCallDepth);

		m_FarCalls.m_Stack.push_back(*new FarCalls::Frame);
		auto& x = m_FarCalls.m_Stack.back();

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
		uint8_t bData = FetchBit(br); // data or stack

		Type::Size n;
		addr.Export(n);

		if (bData)
			SetPtrData(out, n);
		else
			SetPtrStack(out, n);

		if (m_pDbg)
			*m_pDbg << (bData ? 'd' : 's') << static_cast<int>(out.p - (bData ? m_Data.p : (m_pStack + m_Sp)));
	}

	void Processor::LogDeref()
	{
		if (m_pDbg)
			*m_pDbg << "=>";
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

	template <> void Processor::TestStackPtr<true>(Type::Size n) {
		Test(n <= Limits::StackSize);
	}

	template <> void Processor::TestStackPtr<false>(Type::Size) {
	}

	void Processor::SetPtrStack(Ptr& out, Type::Size n)
	{
		n += m_Sp; // wraparound is ok, negative stack offsets are allowed

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

	void Processor::FetchParam(BitReader& br, Ptr& out)
	{
		FetchPtr(br, out);

		if (FetchBit(br))
		{
			LogDeref();
			// dereference
			const auto* p = out.RGet<uintBigFor<Type::Size>::Type>();
			FetchPtr(br, out, *p);
		}
	}

	void Processor::FetchBuffer(BitReader& br, uint8_t* pBuf, Type::Size nSize)
	{
		Ptr ptr;
		if (FetchBit(br)) // ptr or data
		{
			// dereference
			FetchPtr(br, ptr);
			Test(ptr.n >= nSize);

			LogDeref();
		}
		else
			ptr.p = Cast::NotConst(FetchInstruction(nSize));

		memcpy(pBuf, ptr.p, nSize);

		if (m_pDbg)
		{
			struct Dummy :public uintBigImpl {
				static void Do(const uint8_t* pDst, uint32_t nDst, std::ostream& os) {
					_Print(pDst, nDst, os);
				}
			};

			Dummy::Do(pBuf, nSize, *m_pDbg);
		}
	}

	void Processor::RunOnce()
	{
		if (m_pDbg)
			*m_pDbg << "ip=" << Type::uintSize(m_Ip) << ", ";

		uint8_t nOpCode = *FetchInstruction(1);
		BitReader br;

		switch (nOpCode)
		{
#define THE_MACRO_ParamPass(name, type) par##name,
#define THE_MACRO_ParamRead(name, type) BVM_ParamType_##type par##name; LogVarName(#name); FetchParam(br, par##name); LogVarEnd();

#define THE_MACRO(name) \
		case static_cast<uint8_t>(OpCode::n_##name): \
			{ \
				if (m_pDbg) \
					*m_pDbg << #name " "; \
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

#define THE_MACRO_ParamDecl(name, type) const BVM_ParamType_##type& name,
#define BVM_METHOD(method) void Processor::On_##method(BVMOp_##method(THE_MACRO_ParamDecl) Zero_)

	BVM_METHOD(mov)
	{
		Type::Size nSize;
		size.Export(nSize);
		DoMov(dst, src.RGet<uint8_t>(nSize), nSize);
	}

	BVM_METHOD(mov1) { DoMov(dst, src.m_pData, src.nBytes); }
	BVM_METHOD(mov2) { DoMov(dst, src.m_pData, src.nBytes); }
	BVM_METHOD(mov4) { DoMov(dst, src.m_pData, src.nBytes); }
	BVM_METHOD(mov8) { DoMov(dst, src.m_pData, src.nBytes); }

	void Processor::DoMov(const Ptr& dst, const uint8_t* pSrc, Type::Size nSize)
	{
		memmove(dst.WGet<uint8_t>(nSize), pSrc, nSize);
	}

	BVM_METHOD(xor)
	{
		Type::Size nSize;
		size.Export(nSize);
		DoXor(dst, src.RGet<uint8_t>(nSize), nSize);
	}

	BVM_METHOD(xor1) { DoXor(dst, src.m_pData, src.nBytes); }
	BVM_METHOD(xor2) { DoXor(dst, src.m_pData, src.nBytes); }
	BVM_METHOD(xor4) { DoXor(dst, src.m_pData, src.nBytes); }
	BVM_METHOD(xor8) { DoXor(dst, src.m_pData, src.nBytes); }

	void Processor::DoXor(const Ptr& dst, const uint8_t* pSrc, Type::Size nSize)
	{
		auto* pDst = dst.WGet<uint8_t>(nSize);

		for (uint32_t i = 0; i < nSize; i++)
			pDst[i] ^= pSrc[i];
	}

	BVM_METHOD(cmp)
	{
		Type::Size nSize;
		size.Export(nSize);

		DoCmp(p1.RGet<uint8_t>(nSize), p2.RGet<uint8_t>(nSize), nSize);
	}

	BVM_METHOD(cmp1) { DoCmp(a.m_pData, b.m_pData, b.nBytes); }
	BVM_METHOD(cmp2) { DoCmp(a.m_pData, b.m_pData, b.nBytes); }
	BVM_METHOD(cmp4) { DoCmp(a.m_pData, b.m_pData, b.nBytes); }
	BVM_METHOD(cmp8) { DoCmp(a.m_pData, b.m_pData, b.nBytes); }

	void Processor::DoCmp(const uint8_t* p1, const uint8_t* p2, Type::Size nSize)
	{
		int n = memcmp(p1, p2, nSize);
		m_Flags = (n < 0) ? -1 : (n > 0);
	}

	BVM_METHOD(add)
	{
		Type::Size nSize;
		size.Export(nSize);
		DoAdd(dst, src.RGet<uint8_t>(nSize), nSize);
	}

	BVM_METHOD(add1) { DoAdd(dst, src.m_pData, src.nBytes); }
	BVM_METHOD(add2) { DoAdd(dst, src.m_pData, src.nBytes); }
	BVM_METHOD(add4) { DoAdd(dst, src.m_pData, src.nBytes); }
	BVM_METHOD(add8) { DoAdd(dst, src.m_pData, src.nBytes); }

	void Processor::DoAdd(const Ptr& dst, const uint8_t* pSrc, Type::Size nSize)
	{
		struct Dummy :public uintBigImpl {
			static uint8_t Do(const Ptr& dst, const uint8_t* pSrc, Type::Size nSize) {
				return _Inc(dst.WGet<uint8_t>(nSize), nSize, pSrc);
			}
		};

		m_Flags = Dummy::Do(dst, pSrc, nSize);
	}

	BVM_METHOD(jmp) {
		DoJmp(addr);
	}
	BVM_METHOD(jz) {
		if (!m_Flags)
			DoJmp(addr);
	}
	BVM_METHOD(jnz) {
		if (m_Flags)
			DoJmp(addr);
	}
	BVM_METHOD(jg) {
		if (m_Flags > 0)
			DoJmp(addr);
	}
	BVM_METHOD(jb) {
		if (m_Flags < 0)
			DoJmp(addr);
	}
	BVM_METHOD(jgz) {
		if (m_Flags >= 0)
			DoJmp(addr);
	}
	BVM_METHOD(jbz) {
		if (m_Flags <= 0)
			DoJmp(addr);
	}

	void Processor::DoJmp(const Type::uintSize& addr)
	{
		addr.Export(m_Ip);
		Test(m_Ip < m_Data.n);
	}

	void Processor::PushFrame(const Type::uintSize& frame)
	{
		Type::Size nFrame;
		frame.Export(nFrame);

		Ptr ptr;
		SetPtrStack(ptr, nFrame);
		auto* pFrame = ptr.WGet<StackFrame>();

		pFrame->m_Prev = frame;
		pFrame->m_RetAddr = m_Ip;

		m_Sp += nFrame + sizeof(StackFrame);
		LogStackPtr();
	}

	BVM_METHOD(call)
	{
		PushFrame(frame);
		DoJmp(addr);
		m_FarCalls.m_Stack.back().m_LocalDepth++;
	}

	BVM_METHOD(call_far)
	{
		PushFrame(frame);

		const auto* pID = trgContract.RGet<ContractID>();
		Type::Size nM;
		iMethod.Export(nM);

		CallFar(*pID, nM);
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
			m_FarCalls.m_Stack.Pop();
			if (m_FarCalls.m_Stack.empty())
				return; // finished

			m_Data = m_FarCalls.m_Stack.back().m_Data;
		}

		DoJmp(pFrame->m_RetAddr);
	}

	void Processor::SetVarKey(VarKey& vk)
	{
		memcpy(vk.m_p, m_FarCalls.m_Stack.back().m_Cid.m_pData, ContractID::nBytes);
		vk.m_Size = static_cast<Type::Size>(ContractID::nBytes);
	}

	void Processor::SetVarKey(VarKey& vk, uint8_t nTag, const Blob& blob)
	{
		SetVarKey(vk);
		vk.m_p[vk.m_Size++] = nTag;

		assert(vk.m_Size + blob.n <= _countof(vk.m_p));
		memcpy(vk.m_p + vk.m_Size, blob.p, blob.n);
		vk.m_Size += blob.n;
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
		SetVarKey(vk, key, nKey);

		auto* pSizeDst = pnDst.WGet<Type::uintSize>();

		Type::Size nDst_;
		pSizeDst->Export(nDst_);
		Test(nDst_ <= Limits::VarSize);

		LoadVar(vk, dst.WGet<uint8_t>(nDst_), nDst_);

		*pSizeDst = nDst_;
	}

	BVM_METHOD(save_var)
	{
		VarKey vk;
		SetVarKey(vk, key, nKey);

		Type::Size nDst_;
		nDst.Export(nDst_);
		Test(nDst_ <= Limits::VarSize);

		bool b = SaveVar(vk, dst.RGet<uint8_t>(nDst_), nDst_);
		m_Flags = !!b;
	}

	BVM_METHOD(add_sig)
	{
		if (m_pSigMsg)
			AddSigInternal(*pPubKey.RGet<ECC::Point>());
	}

#undef BVM_METHOD
#undef THE_MACRO_ParamDecl

	ECC::Point::Native& Processor::AddSigInternal(const ECC::Point& pk)
	{
		auto& ret = m_vPks.emplace_back();
		Test(ret.ImportNnz(pk));
		return ret;
	}

	void Processor::CheckSigs(const ECC::Point& pt, const ECC::Signature& sig)
	{
		if (!m_pSigMsg)
			return;

		auto& comm = AddSigInternal(pt);

		// TODO: account for kernel balance change (funds consumed/emitted)
		comm;

		ECC::SignatureBase::Config cfg = ECC::Context::get().m_Sig.m_CfgG1; // copy
		cfg.m_nKeys = static_cast<uint32_t>(m_vPks.size());

		Test(Cast::Down<ECC::SignatureBase>(sig).IsValid(cfg, *m_pSigMsg, &sig.m_k, &m_vPks.front()));
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

	void Compiler::ParseParam_Ptr(MyBlob& line)
	{
		MyBlob x;
		line.ExtractToken(x, ',');

		if (x.n < 2)
			Fail("");

		uint8_t p1 = *x.p;
		switch (p1)
		{
		case 's':
		case 'd':
			break;
		default:
			Fail("");
		}

		x.Move1();

		uint8_t p2 = *x.p;
		uint8_t bIndirect = 0;
		switch (p2)
		{
		case 's':
		case 'd':
			x.Move1();
			bIndirect = 1;
			std::swap(p1, p2);
		}

		ParseParam_PtrDirect(x, p1);

		BwAdd(bIndirect);
		if (bIndirect)
			BwAdd('d' == p2);
	}

	void Compiler::ParseParam_PtrDirect(MyBlob& x, uint8_t p)
	{
		ParseSignedNumber(x, sizeof(Type::Size));
		BwAdd('d' == p);
	}

	void Compiler::ParseSignedNumber(MyBlob& x, uint32_t nBytes)
	{
		uint8_t neg = (x.n && ('-' == *x.p));
		if (neg)
			x.Move1();

		uint64_t val = 0;
		assert(nBytes <= sizeof(val));
		while (x.n)
		{
			uint8_t c = *x.p;
			c -= '0';
			if (c > 9)
				Fail("");

			val = val * 10 + c;

			x.Move1();
		}

		if ((nBytes < sizeof(val)) && (val >> (nBytes << 3)))
			Fail("overflow");

		uintBigFor<uint64_t>::Type val2 = val;
		if (neg)
			val2.Negate();

		for (uint32_t i = 0; i < nBytes; i++)
			m_Result.push_back(val2.m_pData[val2.nBytes - nBytes + i]);

	}

	void Compiler::ParseParam_uintBig(MyBlob& line, uint32_t nBytes)
	{
		MyBlob x;
		line.ExtractToken(x, ',');

		if (!x.n)
			Fail("");

		uint8_t p1 = *x.p;
		uint8_t bIndirect = 0;
		switch (p1)
		{
		case 's':
		case 'd':
			x.Move1();
			bIndirect = 1;
		}

		BwAdd(bIndirect);

		if (bIndirect)
			ParseParam_PtrDirect(x, p1);
		else
		{
			if (!x.n)
				Fail("");

			if ((Type::uintSize::nBytes == nBytes) && ('.' == p1))
			{
				// must be a label
				x.Move1();
				if (!x.n)
					Fail("");

				Label& lbl = m_mapLabels[x.as_Blob()];
				lbl.m_Refs.push_back(ToSize(m_Result.size()));

				Type::uintSize val = Zero;
				for (uint32_t i = 0; i < val.nBytes; i++)
					m_Result.push_back(val.m_pData[i]);

			}
			else
				ParseSignedNumber(x, nBytes);
		}
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

			Label& x = m_mapLabels[opcode.as_Blob()];
			if (Label::s_Invalid != x.m_Pos)
				Fail("duplicated label");

			x.m_Pos = ToSize(m_Result.size());

			return;
		}

		BitWriter bw;

#define MY_TOKENIZE2(a, b) a##b
#define MY_TOKENIZE1(a, b) MY_TOKENIZE2(a, b)
#define THE_MACRO_ParamCompile(name, type) MY_TOKENIZE1(ParseParam_, BVM_ParamType_##type)(line);
#define THE_MACRO(name) \
		if (opcode == #name) \
		{ \
			m_Result.push_back(static_cast<uint8_t>(OpCode::n_##name)); \
			BVMOp_##name(THE_MACRO_ParamCompile) \
			return; \
		}

		BVM_OpCodes(THE_MACRO)
#undef THE_MACRO
#undef THE_MACRO_ParamCompile

		Fail("Invalid instruction");
	}

	void Compiler::BwFlushStrict()
	{
		(&m_Result.front())[m_BitWriter.m_Pos] = m_BitWriter.m_Value;
		m_BitWriter.m_Bits = 0;
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

	void Compiler::Finalyze()
	{
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

			auto it = m_mapLabels.find(b);
			if (m_mapLabels.end() == it)
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

			auto it = m_mapLabels.find(b);
			if (m_mapLabels.end() == it)
				break;

			hdr.m_pMethod[nLabels] = ToSize(nSizeHeader + it->second.m_Pos);
		}

		for (auto it = m_mapLabels.begin(); m_mapLabels.end() != it; it++)
		{
			Label& x = it->second;
			if (Label::s_Invalid == x.m_Pos)
				Fail("undefined label");

			Type::uintSize addr = ToSize(nSizeHeader + x.m_Pos);

			for (auto it2 = x.m_Refs.begin(); x.m_Refs.end() != it2; it2++)
				*reinterpret_cast<Type::uintSize*>(&m_Result.front() + nSizeHeader + *it2) = addr;
		}
	}


	/////////////////////////////////////////////
	// VariableMem

	void VariableMem::Set::Clear()
	{
		while (!empty())
			Delete(*begin());
	}

	void VariableMem::Set::Delete(Entry& x)
	{
		erase(s_iterator_to(x));
		delete& x;
	}

	VariableMem::Entry* VariableMem::Set::Find(const Blob& key)
	{
		Entry x;
		x.m_KeyIdx = key;

		iterator it = find(x);
		return (end() == it) ? nullptr : &*it;
	}

	VariableMem::Entry* VariableMem::Set::Create(const Blob& key)
	{
		auto pGuard = std::make_unique<Entry>();
		key.Export(pGuard->m_Key);
		pGuard->m_KeyIdx = pGuard->m_Key;

		auto* pE = pGuard.release();
		insert(*pE);
		return pE;
	}

} // namespace bvm
} // namespace beam
