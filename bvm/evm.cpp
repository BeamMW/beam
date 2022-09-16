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
}


#define EvmOpcodes_All(macro) \
	macro(0x00, stop) \
	macro(0x01, add) \
	macro(0x02, mul) \
	macro(0x03, sub) \
	macro(0x04, div) \
	macro(0x05, sdiv) \
	macro(0x10, lt) \
	macro(0x11, gt) \
	macro(0x12, slt) \
	macro(0x13, sgt) \
	macro(0x14, eq) \
	macro(0x15, iszero) \
	macro(0x16, And) \
	macro(0x17, Or) \
	macro(0x18, Xor) \
	macro(0x19, Not) \
	macro(0x1a, byte) \
	macro(0x1b, shl) \
	macro(0x1c, shr) \
	macro(0x34, callvalue) \
	macro(0x35, calldataload) \
	macro(0x36, calldatasize) \
	macro(0x39, codecopy) \
	macro(0x50, pop) \
	macro(0x51, mload) \
	macro(0x52, mstore) \
	macro(0x54, sload) \
	macro(0x55, sstore) \
	macro(0x56, jump) \
	macro(0x57, jumpi) \
	macro(0x5b, jumpdest) \
	macro(0xf3, Return) \
	macro(0xfd, revert) \

struct EvmProcessorPlus :public EvmProcessor
{
#define THE_MACRO(code, name) void On_##name();
	EvmOpcodes_All(THE_MACRO)
#undef THE_MACRO

	struct Opcode {
#define THE_MACRO(code, name) static const uint8_t name = code;
		EvmOpcodes_All(THE_MACRO)
#undef THE_MACRO
	};

	void RunOnce();

	void LogOpCode(const char* sz);

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
};

void EvmProcessorPlus::LogOpCode(const char* sz)
{
	printf("\t%08x, %s\n", m_Code.m_Ip - 1, sz);
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
#define THE_MACRO(code, name) case code: LogOpCode(#name); On_##name(); break;
		EvmOpcodes_All(THE_MACRO)
#undef THE_MACRO

	default:

		switch (nCode >> 4)
		{
		case 0x6:
		case 0x7: // 0x60 - 0x7f
			LogOpCode("push");
			PushN(nCode - 0x5f);
			break;

		case 0x8: // 0x80 - 0x8f
			LogOpCode("dup");
			DupN(nCode - 0x7f);
			break;

		case 0x9: // 0x90 - 0x9f
			LogOpCode("swap");
			SwapN(nCode - 0x8f);
			break;

		default:
			Fail();
		}

	}
}

#define OnOpcode(name) void EvmProcessorPlus::On_##name()

OnOpcode(stop)
{
	m_State = State::Done;
}

OnOpcode(add)
{
	auto& w1 = m_Stack.Pop();
	auto& w2 = m_Stack.get_At(0);

	w2 += w1;
}

OnOpcode(mul)
{
	auto& w1 = m_Stack.Pop();
	auto& w2 = m_Stack.get_At(0);

	w2 = w1 * w2;
}

OnOpcode(sub)
{
	auto& w1 = m_Stack.Pop();
	auto& w2 = m_Stack.get_At(0);

	// w2 = w1 - w2;
	w2.Negate();
	w2 += w1;

}

OnOpcode(div)
{
	auto& w1 = m_Stack.Pop();
	auto& w2 = m_Stack.get_At(0);

	// w2 = w1 / w2;
	auto x = w2;
	Test(x != Zero);

	w2.SetDiv(w1, x);

}

OnOpcode(sdiv)
{
	auto& w1 = m_Stack.Pop();
	auto& w2 = m_Stack.get_At(0);

	// w2 = w1 / w2;
	auto x = w2;
	Test(x != Zero);

	bool bNeg = IsNeg(w1);
	if (bNeg)
		w1.Negate();

	if (IsNeg(x))
	{
		bNeg = !bNeg;
		x.Negate();
	}

	w2.SetDiv(w1, x);
	if (bNeg)
		w2.Negate();

}

OnOpcode(lt)
{
	auto& w1 = m_Stack.Pop();
	auto& w2 = m_Stack.get_At(0);

	uint8_t b = CmpW<false>(w1, w2) < 0;
	w2 = b;
}

OnOpcode(gt)
{
	auto& w1 = m_Stack.Pop();
	auto& w2 = m_Stack.get_At(0);

	uint8_t b = CmpW<false>(w1, w2) > 0;
	w2 = b;
}

OnOpcode(slt)
{
	auto& w1 = m_Stack.Pop();
	auto& w2 = m_Stack.get_At(0);

	uint8_t b = CmpW<true>(w1, w2) < 0;
	w2 = b;
}

OnOpcode(sgt)
{
	auto& w1 = m_Stack.Pop();
	auto& w2 = m_Stack.get_At(0);

	uint8_t b = CmpW<true>(w1, w2) > 0;
	w2 = b;
}

OnOpcode(eq)
{
	auto& w1 = m_Stack.Pop();
	auto& w2 = m_Stack.get_At(0);

	uint8_t b = !!(w1 == w2);
	w2 = b;
}

OnOpcode(shl)
{
	auto& w1 = m_Stack.Pop();
	auto& w2 = m_Stack.get_At(0);

	auto x = w2;
	x.ShiftLeft(WtoU32(w1), w2);
}

OnOpcode(shr)
{
	auto& w1 = m_Stack.Pop();
	auto& w2 = m_Stack.get_At(0);

	auto x = w2;
	x.ShiftRight(WtoU32(w1), w2);
}

OnOpcode(iszero)
{
	auto& w = m_Stack.get_At(0);
	uint8_t bZero = !!(w == Zero);
	w = bZero;
}

OnOpcode(And)
{
	auto& w1 = m_Stack.Pop();
	auto& w2 = m_Stack.get_At(0);

	for (uint32_t i = 0; i < Word::nBytes; i++)
		w2.m_pData[i] &= w1.m_pData[i];
}

OnOpcode(Or)
{
	auto& w1 = m_Stack.Pop();
	auto& w2 = m_Stack.get_At(0);

	for (uint32_t i = 0; i < Word::nBytes; i++)
		w2.m_pData[i] |= w1.m_pData[i];
}

OnOpcode(Xor)
{
	auto& w1 = m_Stack.Pop();
	auto& w2 = m_Stack.get_At(0);

	for (uint32_t i = 0; i < Word::nBytes; i++)
		w2.m_pData[i] ^= w1.m_pData[i];
}

OnOpcode(Not)
{
	auto& w = m_Stack.get_At(0);
	w.Inv();
}

OnOpcode(byte)
{
	auto& w1 = m_Stack.Pop();
	auto& w2 = m_Stack.get_At(0);

	auto nByte = WtoU32(w1);
	Test(nByte < w2.nBytes);
	w2 = w2.m_pData[nByte];
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
