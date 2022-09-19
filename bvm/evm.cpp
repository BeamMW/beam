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
#include "evm.h"
#include "../core/keccak.h"
//#include <sstream>
//#include <iomanip>
//#include <string>

namespace beam {

void EvmProcessor::Fail()
{
	throw std::runtime_error("evm");
}

void EvmProcessor::Test(bool b)
{
	if (!b)
		Fail();
}

uint32_t EvmProcessor::WtoU32(const Word& w)
{
	uint32_t ret;
	Test(memis0(w.m_pData, w.nBytes - sizeof(ret)));

	w.ExportWord<sizeof(Word) / sizeof(ret) - 1>(ret);
	return ret;
}

uint64_t EvmProcessor::WtoU64(const Word& w)
{
	uint64_t ret;
	Test(memis0(w.m_pData, w.nBytes - sizeof(ret)));

	w.ExportWord<sizeof(Word) / sizeof(ret) - 1>(ret);
	return ret;
}

uint32_t EvmProcessor::Memory::get_Order(uint32_t n)
{
	uint32_t ret = 0;
	for (; n; ret++)
		n >>= 1;
	return ret;
}

uint8_t* EvmProcessor::Memory::get_Addr(const Word& wAddr, uint32_t nSize)
{
	if (!nSize)
		return nullptr;

	uint32_t n0 = WtoU32(wAddr);
	uint32_t n1 = n0 + nSize;
	Test(n1 >= n0);

	if (n1 > m_v.size())
	{
		Test(n1 <= m_Max);

		uint32_t nOrder = get_Order(n1);
		nOrder = std::max(nOrder, 10u); // 2^10 = 1024

		if ((n1 - 1) >> (nOrder - 1))
			nOrder++;

		m_v.resize((uint32_t)(1u << nOrder));
	}

	return &m_v.front() + n0;
}

void EvmProcessor::Reset()
{
	InitVars();
	m_Memory.m_v.clear();
	m_Stack.m_v.clear();
}

void EvmProcessor::InitVars()
{
	ZeroObject(m_Code);
	ZeroObject(m_RetVal);
	ZeroObject(m_Args);
	m_State = State::Running;
	m_Gas = 0;
}

#define EvmOpcodes_Binary(macro) \
	macro(0x01, add, 3) \
	macro(0x02, mul, 5) \
	macro(0x03, sub, 3) \
	macro(0x04, div, 5) \
	macro(0x05, sdiv, 5) \
	macro(0x0a, exp, 10) \
	macro(0x10, lt, 3) \
	macro(0x11, gt, 3) \
	macro(0x12, slt, 3) \
	macro(0x13, sgt, 3) \
	macro(0x14, eq, 3) \
	macro(0x16, And, 3) \
	macro(0x17, Or, 3) \
	macro(0x18, Xor, 3) \
	macro(0x1a, byte, 3) \
	macro(0x1b, shl, 3) \
	macro(0x1c, shr, 3) \
	macro(0x1d, sar, 3) \
	macro(0x20, sha3, 30) \

#define EvmOpcodes_Unary(macro) \
	macro(0x15, iszero, 3) \
	macro(0x19, Not, 3) \

#define EvmOpcodes_Custom(macro) \
	macro(0x00, stop, 0) \
	macro(0x33, caller, 2) \
	macro(0x34, callvalue, 2) \
	macro(0x35, calldataload, 3) \
	macro(0x36, calldatasize, 2) \
	macro(0x38, codesize, 2) \
	macro(0x39, codecopy, 3) \
	macro(0x50, pop, 2) \
	macro(0x51, mload, 3) \
	macro(0x52, mstore, 3) \
	macro(0x53, mstore8, 3) \
	macro(0x54, sload, 800) \
	macro(0x55, sstore, 20000) \
	macro(0x56, jump, 8) \
	macro(0x57, jumpi, 10) \
	macro(0x58, pc, 2) \
	macro(0x5a, gas, 2) \
	macro(0x5b, jumpdest, 1) \
	macro(0xf3, Return, 0) \
	macro(0xfd, revert, 0) \

#define EvmOpcodes_All(macro) \
	EvmOpcodes_Binary(macro) \
	EvmOpcodes_Unary(macro) \
	EvmOpcodes_Custom(macro) \

struct EvmProcessorPlus :public EvmProcessor
{
#define THE_MACRO(code, name, gas) void On_##name();
	EvmOpcodes_Custom(THE_MACRO)
#undef THE_MACRO

#define THE_MACRO(code, name, gas) \
	void OnBinary_##name(Word& a, Word& b); \
	void On_##name() \
	{ \
		auto& w1 = m_Stack.Pop(); \
		auto& w2 = m_Stack.get_At(0); \
		OnBinary_##name(w1, w2); \
	}

	EvmOpcodes_Binary(THE_MACRO)
#undef THE_MACRO

#define THE_MACRO(code, name, gas) \
	void OnUnary_##name(Word& a); \
	void On_##name() \
	{ \
		auto& w = m_Stack.get_At(0); \
		OnUnary_##name(w); \
	}

	EvmOpcodes_Unary(THE_MACRO)
#undef THE_MACRO

	struct Opcode {
#define THE_MACRO(code, name, gas) static const uint8_t name = code;
		EvmOpcodes_All(THE_MACRO)
#undef THE_MACRO
	};

	void RunOnce();

	void LogOpCode(const char* sz);
	void DrainGas(uint32_t);

	void Jump(const Word&);
	void PushN(uint32_t n);
	void DupN(uint32_t n);
	void SwapN(uint32_t n);
	void SaveRetval();

	template <bool bPadLeft>
	static void AssignPartial(Word&, const uint8_t* p, uint32_t n);

	static bool IsNeg(const Word& w) {
		return !!(0x80 & *w.m_pData);
	}

	template <bool bSigned>
	static int CmpW(const Word& w1, const Word& w2)
	{
		if constexpr (bSigned)
		{
			bool b1 = IsNeg(w1);
			if (IsNeg(w2) != b1)
				return b1 ? -1 : 1;
		}

		return w1.cmp(w2);
	}

	static void SetBool(Word& w, bool b)
	{
		uint8_t val = !!b;
		w = val;
	}
};

void EvmProcessorPlus::LogOpCode(const char* sz)
{
	printf("\t%08x, %s\n", m_Code.m_Ip - 1, sz);
}

void EvmProcessorPlus::DrainGas(uint32_t n)
{
	Test(m_Gas >= n);
	m_Gas -= n;
}

void EvmProcessor::RunOnce()
{
	Cast::Up<EvmProcessorPlus>(*this).RunOnce();
}

void EvmProcessorPlus::RunOnce()
{
	assert(State::Running == m_State);

	auto nCode = m_Code.Read1();

	switch (nCode)
	{
#define THE_MACRO(code, name, gas) \
	case code: \
		LogOpCode(#name); \
		DrainGas(gas); \
		On_##name(); \
		break;

		EvmOpcodes_All(THE_MACRO)
#undef THE_MACRO

	default:

		switch (nCode >> 4)
		{
		case 0x6:
		case 0x7: // 0x60 - 0x7f
			LogOpCode("push");
			DrainGas(3);
			PushN(nCode - 0x5f);
			break;

		case 0x8: // 0x80 - 0x8f
			LogOpCode("dup");
			DrainGas(3);
			DupN(nCode - 0x7f);
			break;

		case 0x9: // 0x90 - 0x9f
			LogOpCode("swap");
			DrainGas(3);
			SwapN(nCode - 0x8f);
			break;

		default:
			Fail();
		}

	}
}

#define OnOpcode(name) void EvmProcessorPlus::On_##name()
#define OnOpcodeBinary(name) void EvmProcessorPlus::OnBinary_##name(Word& a, Word& b)
#define OnOpcodeUnary(name) void EvmProcessorPlus::OnUnary_##name(Word& a)

OnOpcode(stop)
{
	m_State = State::Done;
}

OnOpcodeBinary(add)
{
	b += a;
}

OnOpcodeBinary(mul)
{
	b = a * b;
}

OnOpcodeBinary(sub)
{
	// b = a - b;
	b.Negate();
	b += a;
}

OnOpcodeBinary(div)
{
	// b = a / b;
	auto x = b;
	Test(x != Zero);

	b.SetDiv(a, x);
}

OnOpcodeBinary(sdiv)
{
	// b = a / b;
	auto x = b;
	Test(x != Zero);

	bool bNeg = IsNeg(a);
	if (bNeg)
		a.Negate();

	if (IsNeg(x))
	{
		bNeg = !bNeg;
		x.Negate();
	}

	b.SetDiv(a, x);
	if (bNeg)
		b.Negate();
}

OnOpcodeBinary(exp)
{
	// b = a ^ b
	auto n = b;
	b = 1u;

	// double-and-add
	for (uint32_t nBit = n.nBits - 1; ; )
	{
		if (1u & (n.m_pData[nBit >> 3] >> (7 & nBit)))
			b = b * a;

		if (!nBit--)
			break;

		a = a * a;
	}
}

OnOpcodeBinary(lt)
{
	SetBool(b, CmpW<false>(a, b) < 0);
}

OnOpcodeBinary(gt)
{
	SetBool(b, CmpW<false>(a, b) > 0);
}

OnOpcodeBinary(slt)
{
	SetBool(b, CmpW<true>(a, b) < 0);
}

OnOpcodeBinary(sgt)
{
	SetBool(b, CmpW<true>(a, b) < 0);
}

OnOpcodeBinary(eq)
{
	SetBool(b, a == b);
}

OnOpcodeBinary(shl)
{
	auto x = b;
	auto n = WtoU32(a);
	Test(n < x.nBits);
	x.ShiftLeft(n, b);
}

OnOpcodeBinary(shr)
{
	auto x = b;
	auto n = WtoU32(a);
	Test(n < x.nBits);
	x.ShiftRight(n, b);
}

OnOpcodeBinary(sar)
{
	auto x = b;
	auto n = WtoU32(a);
	Test(n < x.nBits);

	bool bNeg = IsNeg(x);

	x.ShiftRight(n, b);

	if (bNeg)
	{
		uint32_t nBytes = n >> 3;
		memset(b.m_pData, 0xff, nBytes);

		n &= 7;
		if (n)
		{
			uint8_t val = 0xff << (8 - n);
			b.m_pData[nBytes] |= val;
		}
	}
}

OnOpcodeBinary(sha3)
{
	auto nSize = WtoU32(b);
	auto pSrc = m_Memory.get_Addr(a, nSize);

	KeccakProcessor<256> hp;
	hp.Write(pSrc, nSize);
	hp.Read(b.m_pData);
}

OnOpcodeUnary(iszero)
{
	SetBool(a, a == Zero);
}

OnOpcodeBinary(And)
{
	for (uint32_t i = 0; i < Word::nBytes; i++)
		b.m_pData[i] &= a.m_pData[i];
}

OnOpcodeBinary(Or)
{
	for (uint32_t i = 0; i < Word::nBytes; i++)
		b.m_pData[i] |= a.m_pData[i];
}

OnOpcodeBinary(Xor)
{
	for (uint32_t i = 0; i < Word::nBytes; i++)
		b.m_pData[i] ^= a.m_pData[i];
}

OnOpcodeUnary(Not)
{
	a.Inv();
}

OnOpcodeBinary(byte)
{
	auto nByte = WtoU32(a);
	Test(nByte < b.nBytes);
	b = b.m_pData[nByte];
}

OnOpcode(caller)
{
	m_Stack.Push() = m_Args.m_Caller;
}

OnOpcode(callvalue)
{
	m_Stack.Push() = m_Args.m_CallValue;
}

OnOpcode(calldataload)
{
	auto& w1 = m_Stack.get_At(0);
	auto nOffs = WtoU32(w1);

	Test(nOffs <= m_Args.m_Buf.n);
	uint32_t nSize = std::min(32u, m_Args.m_Buf.n - nOffs);

	AssignPartial<false>(w1, ((const uint8_t*) m_Args.m_Buf.p) + nOffs, nSize);
}

OnOpcode(calldatasize)
{
	m_Stack.Push() = m_Args.m_Buf.n;
}

OnOpcode(codesize)
{
	m_Stack.Push() = m_Code.m_n;
}

OnOpcode(codecopy)
{
	auto& w1 = m_Stack.Pop();
	auto& w2 = m_Stack.Pop();
	auto& w3 = m_Stack.Pop();

	auto nAddrSrc = WtoU32(w2);
	auto nSize = WtoU32(w3);

	m_Code.TestAccess(nAddrSrc, nSize); // access code pointer
	const auto* pSrc = m_Code.m_p + nAddrSrc;

	auto* pDst = m_Memory.get_Addr(w1, nSize);

	memcpy(pDst, pSrc, nSize);
}

OnOpcode(pop)
{
	m_Stack.Pop();
}

OnOpcode(mload)
{
	Word& w = m_Stack.get_At(0);

	auto* pSrc = m_Memory.get_Addr(w, sizeof(Word));
	memcpy(w.m_pData, pSrc, sizeof(Word));
}

OnOpcode(mstore)
{
	const Word& w1 = m_Stack.Pop();
	const Word& w2 = m_Stack.Pop();

	auto* pDst = m_Memory.get_Addr(w1, sizeof(Word));
	memcpy(pDst, w2.m_pData, sizeof(Word));
}

OnOpcode(mstore8)
{
	const Word& w1 = m_Stack.Pop();
	const Word& w2 = m_Stack.Pop();

	auto* pDst = m_Memory.get_Addr(w1, 1);
	*pDst = w2.m_pData[w2.nBytes - 1];
}

OnOpcode(sload)
{
	Word& w = m_Stack.get_At(0);

	if (!SLoad(WtoU64(w), w))
		w = Zero;
}

OnOpcode(sstore)
{
	const Word& w1 = m_Stack.Pop();
	const Word& w2 = m_Stack.Pop();

	SStore(WtoU64(w1), w2);
}

void EvmProcessorPlus::Jump(const Word& w)
{
	auto nAddr = WtoU32(w);
	m_Code.m_Ip = nAddr;
	Test(m_Code.m_Ip < m_Code.m_n);
	Test(Opcode::jumpdest == m_Code.m_p[m_Code.m_Ip]);

}

OnOpcode(jump)
{
	const Word& w1 = m_Stack.Pop();
	Jump(w1);
}

OnOpcode(jumpi)
{
	const Word& w1 = m_Stack.Pop();
	const Word& w2 = m_Stack.Pop();

	if (w2 != Zero)
		Jump(w1);
}

OnOpcode(pc)
{
	m_Stack.Push() = m_Code.m_Ip;
}

OnOpcode(gas)
{
	m_Stack.Push() = m_Gas;
}

OnOpcode(jumpdest)
{
	// do nothing
}


template <bool bPadLeft>
void EvmProcessorPlus::AssignPartial(Word& w, const uint8_t* p, uint32_t n)
{
	assert(n <= w.nBytes);
	auto* pDst = w.m_pData;
	auto* pPad = pDst;

	uint32_t nPad = w.nBytes - n;
	(bPadLeft ? pDst : pPad) += nPad;

	memset0(pPad, nPad);
	memcpy(pDst, p, n);
}

void EvmProcessorPlus::PushN(uint32_t n)
{
	auto pSrc = m_Code.Consume(n);
	auto& wDst = m_Stack.Push();
	AssignPartial<true>(wDst, pSrc, n);
}

void EvmProcessorPlus::DupN(uint32_t n)
{
	auto& wDst = m_Stack.Push();
	wDst = m_Stack.get_At(n);
}

void EvmProcessorPlus::SwapN(uint32_t n)
{
	auto& w1 = m_Stack.get_At(0);
	auto& w2 = m_Stack.get_At(n);

	std::swap(w1, w2);
}

void EvmProcessorPlus::SaveRetval()
{
	auto& w1 = m_Stack.Pop();
	auto& w2 = m_Stack.Pop();

	m_RetVal.n = WtoU32(w2);
	m_RetVal.p = m_Memory.get_Addr(w1, m_RetVal.n);
}

OnOpcode(Return)
{
	SaveRetval();
	m_State = State::Done;
}

OnOpcode(revert)
{
	SaveRetval();
	m_State = State::Failed;
}

} // namespace beam
