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
	m_Finished = false;
}


#define EvmOpcodes_All(macro) \
	macro(0x15, iszero) \
	macro(0x34, callvalue) \
	macro(0x39, codecopy) \
	macro(0x50, pop) \
	macro(0x52, mstore) \
	macro(0x56, jump) \
	macro(0x57, jumpi) \
	macro(0x5b, jumpdest) \
	macro(0x80, dup1) \
	macro(0xf3, retrn) \

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
			PushN(nCode - 0x5f);
			break;

		case 0x8: // 0x80 - 0x8f
			DupN(nCode - 0x7f);
			break;

		case 0x9: // 0x90 - 0x9f
			SwapN(nCode - 0x7f);
			break;

		default:
			Fail();
		}

	}
}

#define OnOpcode(name) void EvmProcessorPlus::On_##name()

OnOpcode(iszero)
{
	auto& w = m_Stack.get_At(0);
	uint8_t bZero = !!(w == Zero);
	w = bZero;
}

OnOpcode(callvalue)
{
	auto& w = m_Stack.Push();
	get_CallValue(w);
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

OnOpcode(mstore)
{
	const Word& w1 = m_Stack.Pop();
	const Word& w2 = m_Stack.Pop();

	auto* pDst = m_Memory.get_Addr(w1, sizeof(Word));
	memcpy(pDst, w2.m_pData, sizeof(Word));
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


void EvmProcessorPlus::PushN(uint32_t n)
{
	auto pSrc = m_Code.Consume(n);
	auto& wDst = m_Stack.Push();

	assert(n <= wDst.nBytes);
	memset0(wDst.m_pData, wDst.nBytes - n);
	memcpy(wDst.m_pData + wDst.nBytes - n, pSrc, n);
}

void EvmProcessorPlus::DupN(uint32_t n)
{
	auto& wDst = m_Stack.Push();
	wDst = m_Stack.get_At(n);
}

void EvmProcessorPlus::SwapN(uint32_t n)
{
	auto& w1 = m_Stack.Push();
	auto& w2 = m_Stack.get_At(n);

	std::swap(w1, w2);
}

OnOpcode(dup1) {
	DupN(1);
}

OnOpcode(retrn)
{
	auto& w1 = m_Stack.Pop();
	auto& w2 = m_Stack.Pop();

	m_RetVal.n = WtoU32(w2);
	m_RetVal.p = m_Memory.get_Addr(w1, m_RetVal.n);

	m_Finished = true;
}

} // namespace beam
