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


void EvmProcessor::Address::FromPubKey(const ECC::Point::Storage& pk)
{
	KeccakProcessor<Word::nBits> hp;

	//static const uint8_t s_pPrefix[] = {
	//	0x30,0x59,0x30,0x13,0x06,0x07,0x2A,0x86,0x48,0xCE,0x3D,0x02,0x01,0x06,0x08,0x2A,0x86,0x48,0xCE,0x3D,0x03,0x01,0x07,0x03,0x42,0x00,0x04
	//};

	//hp.Write(s_pPrefix, sizeof(s_pPrefix));

	hp.Write(pk.m_X);
	hp.Write(pk.m_Y);

	Word w;
	hp.Read(w.m_pData);

	Cast::Down<uintBig_t<20> >(*this) = w; // takes the last portion
}

bool EvmProcessor::Address::FromPubKey(const ECC::Point& pk)
{
	ECC::Point::Native pt_n;
	ECC::Point::Storage pt_s;
	if (!pt_n.ImportNnz(pk, &pt_s))
		return false;

	FromPubKey(pt_s);
	return true;
}

bool EvmProcessor::Address::FromPubKey(const PeerID& pid)
{
	ECC::Point pt;
	pt.m_X = Cast::Down<ECC::uintBig>(pid);
	pt.m_Y = 0;
	return FromPubKey(pt);
}

void EvmProcessor::Method::SetSelector(const Blob& b)
{
	Word w;
	HashOf(w, b);
	assert(w.nBytes >= m_Selector.nBytes);
	memcpy(m_Selector.m_pData, w.m_pData, m_Selector.nBytes);
}

void EvmProcessor::Method::SetSelector(const char* szSignature)
{
	Blob blob;
	blob.p = szSignature;
	blob.n = (uint32_t) strlen(szSignature);
	SetSelector(blob);
}

void EvmProcessor::Reset()
{
	InitVars();
	m_lstFrames.Clear();
	m_Top.m_lstUndo.Clear();
}

void EvmProcessor::InitVars()
{
	m_Top.m_Gas = 0;
	m_Top.m_pAccount = nullptr;
}


/*

	// 0x30 range - closure state.
	BALANCE        OpCode = 0x31, gas=400
	ORIGIN         OpCode = 0x32, gas=2
	CALLDATACOPY   OpCode = 0x37, gas= 2 + 3 * (number of words copied, rounded up)	2 is paid for the operation plus 3 for each word copied (rounded up).			
	GASPRICE       OpCode = 0x3a, gas=2
	EXTCODEHASH    OpCode = 0x3f, gas=700??

	// 0x40 range - block operations.
	SELFBALANCE OpCode = 0x47
	BASEFEE     OpCode = 0x48

	// 0x50 range - 'storage' and execution.
	MSIZE    OpCode = 0x59, gas=2

	// 0xf0 range - closures.
	CREATE       OpCode = 0xf0, gas=32000
	CALLCODE     OpCode = 0xf2
	DELEGATECALL OpCode = 0xf4
	CREATE2      OpCode = 0xf5

	INVALID      OpCode = 0xfe
	SELFDESTRUCT OpCode = 0xff
*/


#define EvmOpcodes_Binary(macro) \
	macro(0x01, add, 3) \
	macro(0x02, mul, 5) \
	macro(0x03, sub, 3) \
	macro(0x04, div, 5) \
	macro(0x05, sdiv, 5) \
	macro(0x06, mod, 5) \
	macro(0x07, smod, 5) \
	macro(0x0a, exp, 0) \
	macro(0x0b, signextend, 5) \
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
	macro(0x20, sha3, 0) \

#define EvmOpcodes_Unary(macro) \
	macro(0x15, iszero, 3) \
	macro(0x19, Not, 3) \

#define EvmOpcodes_Custom(macro) \
	macro(0x00, stop, 0) \
	macro(0x08, addmod, 8) \
	macro(0x09, mulmod, 8) \
	macro(0x30, address, 2) \
	macro(0x33, caller, 2) \
	macro(0x34, callvalue, 2) \
	macro(0x35, calldataload, 3) \
	macro(0x36, calldatasize, 2) \
	macro(0x38, codesize, 2) \
	macro(0x39, codecopy, 0) \
	macro(0x3b, extcodesize, 700) \
	macro(0x3c, extcodecopy, 0) \
	macro(0x3d, returndatasize, 2) \
	macro(0x3e, returndatacopy, 3) \
	macro(0x40, blockhash, 20) \
	macro(0x41, coinbase, 2) \
	macro(0x42, timestamp, 2) \
	macro(0x43, number, 2) \
	macro(0x44, difficulty, 2) \
	macro(0x45, gaslimit, 2) \
	macro(0x46, chainid, 2) \
	macro(0x50, pop, 2) \
	macro(0x51, mload, 3) \
	macro(0x52, mstore, 3) \
	macro(0x53, mstore8, 3) \
	macro(0x54, sload, 800) \
	macro(0x55, sstore, 0) \
	macro(0x56, jump, 8) \
	macro(0x57, jumpi, 10) \
	macro(0x58, pc, 2) \
	macro(0x5a, gas, 2) \
	macro(0x5b, jumpdest, 1) \
	macro(0x5f, push0, 3) \
	macro(0xf1, call, 0) \
	macro(0xf3, Return, 0) \
	macro(0xf5, Create2, 0) \
	macro(0xfa, staticcall, 0) \
	macro(0xfd, revert, 0) \

#define EvmOpcodes_All(macro) \
	EvmOpcodes_Binary(macro) \
	EvmOpcodes_Unary(macro) \
	EvmOpcodes_Custom(macro) \

struct EvmProcessor::Context :public EvmProcessor::Frame
{
#define THE_MACRO(code, name, gas) void On_##name(EvmProcessor&);
	EvmOpcodes_Custom(THE_MACRO)
#undef THE_MACRO

#define THE_MACRO(code, name, gas) \
	void OnBinary_##name(Word& a, Word& b); \
	void On_##name(EvmProcessor&) \
	{ \
		auto& w1 = m_Stack.Pop(); \
		auto& w2 = m_Stack.get_At(0); \
		LogOperand(w1); \
		LogOperand(w2); \
		OnBinary_##name(w1, w2); \
		LogOperand(w2); \
	}

	EvmOpcodes_Binary(THE_MACRO)
#undef THE_MACRO

#define THE_MACRO(code, name, gas) \
	void OnUnary_##name(Word& a); \
	void On_##name(EvmProcessor&) \
	{ \
		auto& w = m_Stack.get_At(0); \
		LogOperand(w); \
		OnUnary_##name(w); \
		LogOperand(w); \
	}

	EvmOpcodes_Unary(THE_MACRO)
#undef THE_MACRO

	struct Opcode {
#define THE_MACRO(code, name, gas) static const uint8_t name = code;
		EvmOpcodes_All(THE_MACRO)
#undef THE_MACRO
	};

	void RunOnce(EvmProcessor&);

	void LogOpCode(const char* sz);
	void LogOpCode(const char* sz, uint32_t n);
	void LogOperand(const Word&);

	BaseFrame& get_Prev(EvmProcessor& p)
	{
		assert(!p.m_lstFrames.empty());
		if (p.m_lstFrames.size() == 1)
			return p.m_Top;

		auto it = Frame::List::s_iterator_to(*this);
		--it;
		return *it;
	}

	const Address& get_Caller(EvmProcessor& p)
	{
		return get_Prev(p).m_pAccount->get_Address();
	}

	uint8_t* get_Memory(const Word& wAddr, uint32_t nSize);
	static uint64_t get_MemoryCost(uint32_t nSize);

	void Jump(const Word&);
	void PushN(uint32_t n);
	void DupN(uint32_t n);
	void SwapN(uint32_t n);
	void LogN(uint32_t n);
	void OnFrameDone(EvmProcessor&, bool bSuccess, bool bHaveRetval);
	void OnCall(EvmProcessor&, bool bStatic);

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

	static uint32_t NumWordsRoundUp(uint32_t n)
	{
		return (n + sizeof(Word) - 1) / sizeof(Word);
	}

	struct NoGasException
		:public std::exception
	{
	};

	Height get_BlockArg(EvmProcessor& p, BlockHeader& bh)
	{
		Height h = WtoU64(m_Stack.Pop());
		Test(p.get_BlockHeader(bh, h));
		return h;
	}

	Height get_BlockLast(EvmProcessor& p, BlockHeader& bh)
	{
		Height h = p.get_Height();
		Test(p.get_BlockHeader(bh, h));
		return h;
	}
};

void EvmProcessor::Context::LogOpCode(const char* sz)
{
	printf("\t%08x, %s\n", m_Code.m_Ip - 1, sz);
}

void EvmProcessor::Context::LogOpCode(const char* sz, uint32_t n)
{
	printf("\t%08x, %s%u\n", m_Code.m_Ip - 1, sz, n);
}

void EvmProcessor::Context::LogOperand(const Word& x)
{
	char sz[Word::nTxtLen + 1];
	x.Print(sz);
	printf("\t\t%s\n", sz);
}

void EvmProcessor::BaseFrame::DrainGas(uint64_t n)
{
	if (m_Gas < n)
	{
		m_Gas = 0;
		throw Context::NoGasException();
	}

	m_Gas -= n;
}

uint8_t* EvmProcessor::Context::get_Memory(const Word& wAddr, uint32_t nSize)
{
	if (!nSize)
		return nullptr;

	uint32_t n0 = WtoU32(wAddr);
	uint32_t n1 = n0 + nSize;
	Test(n1 >= n0);

	uint32_t nSize0 = static_cast<uint32_t>(m_Memory.m_v.size());
	if (n1 > nSize0)
	{
		DrainGas(get_MemoryCost(n1) - get_MemoryCost(nSize0));
		m_Memory.m_v.resize(n1);
	}

	return &m_Memory.m_v.front() + n0;
}

uint64_t EvmProcessor::Context::get_MemoryCost(uint32_t nSize)
{
	// cost := a * 3 + floor(a^2 / 512)
	uint64_t a = nSize / Word::nBytes;
	if (nSize % Word::nBytes)
		a++;

	return (a * 3) + (a * a / 512);
}

struct EvmProcessor::UndoOp::Guard
	:public Base
{
	~Guard() override
	{
		m_pAccount->Release();
	}
	void Undo() override
	{
	}
};

void EvmProcessor::BaseFrame::InitAccount(IAccount::Guard& g)
{
	assert(!m_pAccount && g.m_p);

	auto* pOp = new UndoOp::Guard;
	m_lstUndo.push_back(*pOp);
	pOp->m_pAccount = g.m_p;

	m_pAccount = g.m_p;
	g.m_p = nullptr;
}

struct EvmProcessor::UndoOp::BalanceChange
	:public Base
{
	Word m_Val;
	~BalanceChange() override {}
	void Undo() override
	{
		m_pAccount->set_Balance(m_Val);
	}
};


void EvmProcessor::UpdateBalance(IAccount* pAccount, const Word& val0, const Word& val1)
{
	if (val0 == val1)
		return;

	BaseFrame& f = m_lstFrames.empty() ? m_Top : m_lstFrames.back();

	auto* pOp = new UndoOp::BalanceChange;
	f.m_lstUndo.push_back(*pOp);
	pOp->m_pAccount = pAccount;
	pOp->m_Val = val0;

	pAccount->set_Balance(val1);
}

struct EvmProcessor::UndoOp::AccountDelete
	:public Base
{
	~AccountDelete() override {}
	void Undo() override
	{
		m_pAccount->Delete();
	}
};

EvmProcessor::Frame& EvmProcessor::PushFrame(IAccount::Guard& g, bool bCreated)
{
	auto* pF = new Frame;
	m_lstFrames.push_back(*pF);
	pF->InitAccount(g);

	if (bCreated)
	{
		ZeroObject(pF->m_Code);

		auto* pOp = new UndoOp::AccountDelete;
		pF->m_lstUndo.push_back(*pOp);
		pOp->m_pAccount = pF->m_pAccount;
	}
	else
	{
		Blob code;
		pF->m_pAccount->GetCode(code);
		pF->m_Code = code;
		pF->m_Code.m_Ip = 0;
	}

	ZeroObject(pF->m_Args);
	pF->m_Gas = 0;

	return *pF;
}

EvmProcessor::Frame* EvmProcessor::PushFrameContractCreate(const Address& aContract, const Blob& code)
{
	IAccount::Guard g;
	bool bCreated = GetAccount(aContract, true, g);
	if (!bCreated)
		return nullptr; // addr already exists?

	auto& f = PushFrame(g, true);
	f.m_Type = Frame::Type::CreateContract;

	f.m_Code = code;

	return &f;
}


void EvmProcessor::RunOnce()
{
	static_assert(sizeof(Context) == sizeof(Frame), "");

	try
	{
		try
		{
			assert(!m_lstFrames.empty());
			auto& f = Cast::Up<Context>(m_lstFrames.back());
			f.RunOnce(*this);
		}
		catch (const Context::NoGasException&)
		{
			assert(!m_lstFrames.empty());
			auto& f = Cast::Up<Context>(m_lstFrames.back());
			f.OnFrameDone(*this, false, false); // assume during revert no gas is drained
		}
	}
	catch (const std::exception&)
	{
		while (!m_lstFrames.empty())
		{
			auto& f = Cast::Up<Context>(m_lstFrames.back());
			f.UndoChanges();
			m_lstFrames.Delete(f);
		}

		m_RetVal.m_Success = false;
		ZeroObject(m_RetVal.m_Blob);
	}

}

void EvmProcessor::Context::RunOnce(EvmProcessor& p)
{
	auto nCode = m_Code.Read1();

	switch (nCode)
	{
#define THE_MACRO(code, name, gas) \
	case code: \
		LogOpCode(#name); \
		if constexpr (gas > 0) DrainGas(gas); \
		On_##name(p); \
		break;

		EvmOpcodes_All(THE_MACRO)
#undef THE_MACRO

	default:

		switch (nCode >> 4)
		{
		case 0x6:
		case 0x7: // 0x60 - 0x7f
		{
			auto n = nCode - 0x5f;
			LogOpCode("push", n);
			DrainGas(3);
			PushN(n);
		}
		break;

		case 0x8: // 0x80 - 0x8f
		{
			auto n = nCode - 0x7f;
			LogOpCode("dup", n);
			DrainGas(3);
			DupN(n);
		}
		break;

		case 0x9: // 0x90 - 0x9f
		{
			auto n = nCode - 0x8f;
			LogOpCode("swap", n);
			DrainGas(3);
			SwapN(n);
		}
		break;

		case 0xa: // 0xa0 - 0xaf
			{
				uint8_t nLog = nCode - 0xa0;
				Test(nLog <= 4);

				LogOpCode("log", nLog);
				LogN(nLog);
			}
			break;

		default:
			Fail();
		}

	}
}

#define OnOpcode(name) void EvmProcessor::Context::On_##name(EvmProcessor& p)
#define OnOpcodeBinary(name) void EvmProcessor::Context::OnBinary_##name(Word& a, Word& b)
#define OnOpcodeUnary(name) void EvmProcessor::Context::OnUnary_##name(Word& a)

OnOpcode(stop)
{
	OnFrameDone(p, true, false);
}

OnOpcode(addmod)
{
	auto& a = m_Stack.Pop();
	auto& b = m_Stack.Pop();
	auto& c = m_Stack.get_At(0);

	LogOperand(a);
	LogOperand(b);
	LogOperand(c);

	// c = (a + b) % c
	if (c != Zero)
	{
		auto num = a.ToNumber();
		num += b.ToNumber();

		Word::Number quot;
		quot.SetDivResid(num, c.ToNumber());

		c.FromNumber(num);
	}

	LogOperand(c);
}

OnOpcode(mulmod)
{
	auto& a = m_Stack.Pop();
	auto& b = m_Stack.Pop();
	auto& c = m_Stack.get_At(0);

	LogOperand(a);
	LogOperand(b);
	LogOperand(c);

	// c = (a * b) % c
	if (c != Zero)
	{
		Word::Number num;
		num.get_Slice().SetMul(a.ToNumber().get_ConstSlice(), b.ToNumber().get_ConstSlice());

		Word::Number quot;
		quot.SetDivResid(num, c.ToNumber());

		c.FromNumber(num);
	}

	LogOperand(c);
}

OnOpcodeBinary(add)
{
	b += a;
}

OnOpcodeBinary(mul)
{
	Word::Number b_;
	b_.get_Slice().SetMul(a.ToNumber().get_ConstSlice(), b.ToNumber().get_ConstSlice());
	b.FromNumber(b_);
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
	if (b == Zero)
		return;

	Word::Number quot;
	quot.SetDiv(a.ToNumber(), b.ToNumber());
	b.FromNumber(quot);
}

OnOpcodeBinary(sdiv)
{
	// b = a / b;
	if (b == Zero)
		return;

	auto resid = a.ToNumber();
	auto div = b.ToNumber();

	bool bNeg = IsNeg(a);
	if (bNeg)
		resid = -resid;

	if (IsNeg(b))
	{
		bNeg = !bNeg;
		div = -div;
	}

	Word::Number quot;
	quot.SetDivResid(resid, div);

	if (bNeg)
		quot = -quot;

	b.FromNumber(quot);
}

OnOpcodeBinary(mod)
{
	// b = a % b;
	if (b == Zero)
		return;

	auto resid = a.ToNumber();
	Word::Number quot;
	quot.SetDivResid(resid, b.ToNumber());

	b.FromNumber(resid);
}

OnOpcodeBinary(smod)
{
	if (b == Zero)
		return;

	auto resid = a.ToNumber();
	bool bNeg = IsNeg(a);
	if (bNeg)
		resid = -resid;

	auto div = b.ToNumber();
	if (IsNeg(b))
		div = -div;

	Word::Number quot;
	quot.SetDivResid(resid, b.ToNumber());

	if (bNeg)
		resid = -resid;
	b.FromNumber(resid);
}

OnOpcodeBinary(exp)
{
	{
		// (exp == 0) ? 10 : (10 + 10 * (1 + log256(exp)))
		// If exponent is 0, gas used is 10. If exponent is greater than 0, gas used is 10 plus 10 times a factor related to how large the log of the exponent is.

		uint32_t nBytes = uintBigImpl::_GetOrderBytes(b.m_pData, b.nBytes);
		DrainGas((nBytes + 1) * 10);
	}

	// b = a ^ b
	Word::Number res;
	res.Power(a.ToNumber(), b.ToNumber());
	b.FromNumber(res);
}

OnOpcodeBinary(signextend)
{
	if (memis0(a.m_pData, a.nBytes - sizeof(uint32_t)))
	{
		uint32_t ret;
		a.ExportWord<sizeof(Word) / sizeof(ret) - 1>(ret);

		if ((ret < b.nBytes) && (0x80 & b.m_pData[ret]))
			memset(b.m_pData, 0xff, b.nBytes - ret - 1);
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

void EvmProcessor::HashOf(Word& w, const Blob& b)
{
	KeccakProcessor<Word::nBits> hp;
	hp.Write(b.p, b.n);
	hp.Read(w.m_pData);
}

OnOpcodeBinary(sha3)
{
	Blob blob;
	blob.n = WtoU32(b);
	blob.p = get_Memory(a, blob.n);

	// 30 + 6 * (size of input in words)
	// 30 is the paid for the operation plus 6 paid for each word(rounded up) for the input data.
	DrainGas(30 + 6u * Context::NumWordsRoundUp(blob.n));

	HashOf(b, blob);
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

OnOpcode(address)
{
	m_Stack.Push() = m_pAccount->get_Address().ToWord();
}

OnOpcode(caller)
{
	m_Stack.Push() = get_Caller(p).ToWord();
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

	// 2 + 3 * (number of words copied, rounded up)
	// 2 is paid for the operation plus 3 for each word copied(rounded up).
	DrainGas(2 + 3u * Context::NumWordsRoundUp(nSize));

	m_Code.TestAccess(nAddrSrc, nSize); // access code pointer
	const auto* pSrc = m_Code.m_p + nAddrSrc;

	auto* pDst = get_Memory(w1, nSize);

	memcpy(pDst, pSrc, nSize);
}

OnOpcode(extcodesize)
{
	IAccount::Guard g;
	p.GetAccount(Address::W2A(m_Stack.Pop()), false, g);

	auto& wRes = m_Stack.Push();
	if (g.m_p)
	{
		Blob b;
		g.m_p->GetCode(b);
		wRes = b.n;
	}
	else
		wRes = Zero;
}

OnOpcode(extcodecopy)
{
	IAccount::Guard g;
	p.GetAccount(Address::W2A(m_Stack.Pop()), false, g);
	Test(g.m_p != nullptr);

	auto& wOffsetDst = m_Stack.Pop();
	auto nOffsetSrc = WtoU32(m_Stack.Pop());
	auto nSize = WtoU32(m_Stack.Pop());

	// 700 + 3 * (number of words copied, rounded up)
	// 700 is paid for the operation plus 3 for each word copied(rounded up).
	DrainGas(700 + 3u * Context::NumWordsRoundUp(nSize));

	Blob code;
	g.m_p->GetCode(code);

	auto nEndSrc = nOffsetSrc + nSize;
	Test(nEndSrc >= nOffsetSrc);
	Test(code.n >= nEndSrc);

	auto* pDst = get_Memory(wOffsetDst, nSize);
	memcpy(pDst, (const uint8_t*) code.p + nOffsetSrc, nSize);
}

OnOpcode(returndatasize)
{
	m_Stack.Push() = p.m_RetVal.m_Blob.n;
}

OnOpcode(returndatacopy)
{
	auto& wOffsetDst = m_Stack.Pop();
	auto nOffsetSrc = WtoU32(m_Stack.Pop());
	auto nSize = WtoU32(m_Stack.Pop());

	auto nEndSrc = nOffsetSrc + nSize;
	Test(nEndSrc >= nOffsetSrc);
	Test(p.m_RetVal.m_Blob.n >= nEndSrc);

	auto* pDst = get_Memory(wOffsetDst, nSize);
	memcpy(pDst, (const uint8_t*) p.m_RetVal.m_Blob.p + nOffsetSrc, nSize);
}

OnOpcode(blockhash)
{
	BlockHeader bh;
	get_BlockArg(p, bh);
	m_Stack.Push() = bh.m_Hash;
}

OnOpcode(coinbase)
{
	BlockHeader bh;
	get_BlockLast(p, bh);
	bh.m_Coinbase.ToWord(m_Stack.Push());
}

OnOpcode(timestamp)
{
	BlockHeader bh;
	get_BlockLast(p, bh);
	m_Stack.Push() = bh.m_Timestamp;
}

OnOpcode(number)
{
	m_Stack.Push() = p.get_Height();
}

OnOpcode(difficulty)
{
	BlockHeader bh;
	get_BlockLast(p, bh);
	m_Stack.Push() = bh.m_Difficulty;
}

OnOpcode(gaslimit)
{
	BlockHeader bh;
	get_BlockLast(p, bh);
	m_Stack.Push() = bh.m_GasLimit;
}

OnOpcode(pop)
{
	m_Stack.Pop();
}

OnOpcode(mload)
{
	Word& w = m_Stack.get_At(0);

	auto* pSrc = get_Memory(w, sizeof(Word));
	memcpy(w.m_pData, pSrc, sizeof(Word));
}

OnOpcode(mstore)
{
	const Word& w1 = m_Stack.Pop();
	const Word& w2 = m_Stack.Pop();

	auto* pDst = get_Memory(w1, sizeof(Word));
	memcpy(pDst, w2.m_pData, sizeof(Word));
}

OnOpcode(mstore8)
{
	const Word& w1 = m_Stack.Pop();
	const Word& w2 = m_Stack.Pop();

	auto* pDst = get_Memory(w1, 1);
	*pDst = w2.m_pData[w2.nBytes - 1];
}

OnOpcode(sload)
{
	Word& w = m_Stack.get_At(0);
	LogOperand(w);

	if (!m_pAccount->SLoad(w, w))
		w = Zero;

	LogOperand(w);
}

struct EvmProcessor::UndoOp::VarSet
	:public Base
{
	Word m_Key;
	Word m_Val;

	~VarSet() override {}
	void Undo() override
	{
		Word wPrev;
		m_pAccount->SStore(m_Key, m_Val, wPrev);
	}
};

OnOpcode(sstore)
{
	const Word& w1 = m_Stack.Pop();
	const Word& w2 = m_Stack.Pop();

	Word wPrev;
	m_pAccount->SStore(w1, w2, wPrev);

	// ((value != 0) && (storage_location == 0)) ? 20000 : 5000
	// 20000 is paid when storage value is set to non - zero from zero. 5000 is paid when the storage value's zeroness remains unchanged or is set to zero.								
	{
		bool b0 = (wPrev == Zero);
		bool b1 = (w2 == Zero);
		if (b0 == b1)
			DrainGas(5000);
		else
		{
			if (b0)
				DrainGas(20000);
			else
				m_Gas += 15000; // refund gas
		}
	}

	auto* pOp = new UndoOp::VarSet;
	m_lstUndo.push_back(*pOp);
	pOp->m_pAccount = m_pAccount;
	pOp->m_Key = w1;
	pOp->m_Val = wPrev;

	LogOperand(w1);
	LogOperand(w2);
}

void EvmProcessor::Context::Jump(const Word& w)
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

OnOpcode(push0)
{
	m_Stack.Push() = Zero;
}

OnOpcode(chainid)
{
	p.get_ChainID(m_Stack.Push());
}

void EvmProcessor::get_ChainID(Word& w)
{
	w = Zero;
}

template <bool bPadLeft>
void EvmProcessor::Context::AssignPartial(Word& w, const uint8_t* p, uint32_t n)
{
	assert(n <= w.nBytes);
	auto* pDst = w.m_pData;
	auto* pPad = pDst;

	uint32_t nPad = w.nBytes - n;
	(bPadLeft ? pDst : pPad) += nPad;

	memset0(pPad, nPad);
	memcpy(pDst, p, n);
}

void EvmProcessor::Context::PushN(uint32_t n)
{
	auto pSrc = m_Code.Consume(n);
	auto& wDst = m_Stack.Push();
	AssignPartial<true>(wDst, pSrc, n);
}

void EvmProcessor::Context::DupN(uint32_t n)
{
	auto& wDst = m_Stack.Push();
	wDst = m_Stack.get_At(n);
}

void EvmProcessor::Context::SwapN(uint32_t n)
{
	auto& w1 = m_Stack.get_At(0);
	auto& w2 = m_Stack.get_At(n);

	std::swap(w1, w2);
}

void EvmProcessor::Context::LogN(uint32_t n)
{
	auto& wAddr = m_Stack.Pop();
	auto nSize = WtoU32(m_Stack.Pop());
	auto* pSrc = get_Memory(wAddr, nSize);

	// 375 + 8 * (number of bytes in log data) + 375
	// 375 is paid for operation plus 8 for each byte in data to be logged plus 375 for the X topics to be logged.
	DrainGas(375u * (n + 1) + 8u * nSize);

	while (n--)
		m_Stack.Pop(); // topic

	pSrc;

}

void EvmProcessor::BaseFrame::UndoChanges()
{
	while (!m_lstUndo.empty())
	{
		auto& op = m_lstUndo.back();
		op.Undo();
		m_lstUndo.Delete(op);
	}

	m_pAccount = nullptr; // for more safety, since we're no longer holding the guard
}

void EvmProcessor::Context::OnFrameDone(EvmProcessor& p, bool bSuccess, bool bHaveRetval)
{
	p.m_RetVal.m_Success = bSuccess;

	if (bHaveRetval)
	{
		auto& w1 = m_Stack.Pop();
		auto& w2 = m_Stack.Pop();

		p.m_RetVal.m_Blob.n = WtoU32(w2);
		p.m_RetVal.m_Blob.p = get_Memory(w1, p.m_RetVal.m_Blob.n);

		p.m_RetVal.m_Memory.m_v.swap(m_Memory.m_v);
	}
	else
		ZeroObject(p.m_RetVal.m_Blob);

	std::unique_ptr<Frame> pGuard(this);
	p.m_lstFrames.pop_back(); // if exc raises now - it'll be handled according to the prev frame

	Context* pPrev = p.m_lstFrames.empty() ? nullptr : &Cast::Up<Context>(p.m_lstFrames.back());
	BaseFrame& fPrev = pPrev ? *pPrev : p.m_Top;
	fPrev.m_Gas += m_Gas;

	if (bSuccess)
		fPrev.m_lstUndo.splice(fPrev.m_lstUndo.end(), m_lstUndo);
	else
		UndoChanges();


	switch (m_Type)
	{
	case Type::CreateContract:

		if (bSuccess)
		{
			m_pAccount->SetCode(p.m_RetVal.m_Blob);
			ZeroObject(p.m_RetVal.m_Blob);
		}

		if (pPrev)
		{
			auto& wRes = pPrev->m_Stack.Push();
			if (bSuccess)
				m_pAccount->get_Address().ToWord(wRes);
			else
				wRes = Zero;
		}
		break;

	case Type::CallRetStatus:
		if (pPrev)
		{
			// copy retval and status

			auto& wResAddr = pPrev->m_Stack.Pop();
			auto nResSize = WtoU32(pPrev->m_Stack.Pop());
			auto* pRes = pPrev->get_Memory(wResAddr, nResSize);

			std::setmin(nResSize, p.m_RetVal.m_Blob.n);
			memcpy(pRes, p.m_RetVal.m_Blob.p, nResSize);

			pPrev->m_Stack.Push() = p.m_RetVal.m_Success ? 1u : 0u;

		}
		break;

	default:
		break; // do nothing
	}
}

OnOpcode(Return)
{
	OnFrameDone(p, true, true);
}

OnOpcode(Create2)
{
	Blob code;

	auto& wValue = m_Stack.Pop();
	auto& wOffset = m_Stack.Pop();
	code.n = WtoU32(m_Stack.Pop());
	auto& wSalt = m_Stack.Pop();

	code.p = get_Memory(wOffset, code.n);

	// new_address = hash(0xFF, sender, salt, bytecode)
	Address aNew;
	{
		KeccakProcessor<Word::nBits> hp;

		uint8_t n = 0xff;
		hp.Write(&n, sizeof(n));

		Word wRes;
		get_Caller(p).ToWord(wRes);

		hp << wRes;
		hp << wSalt;

		hp >> wRes;
		aNew = Address::W2A(wRes);
	}

	auto* pF = p.PushFrameContractCreate(aNew, code);
	if (pF)
	{
		pF->m_Args.m_CallValue = wValue;
		std::swap(pF->m_Gas, m_Gas);
	}
	else
		m_Stack.Push() = Zero; // failure
}

void EvmProcessor::Call(const Address& addr, const Args& args, uint64_t gas)
{
	BaseFrame& fPrev = m_lstFrames.empty() ? m_Top : m_lstFrames.back();

	IAccount::Guard g;
	bool bCreated = GetAccount(addr, true, g);

	auto& f = Cast::Up<Context>(PushFrame(g, bCreated));

	f.m_Type = Frame::Type::CallRetStatus;

	if (args.m_CallValue != Zero)
	{
		Word valFrom0, valFrom1, valTo0, valTo1;
		fPrev.m_pAccount->get_Balance(valFrom0);
		if (valFrom0 < args.m_CallValue)
		{
			f.OnFrameDone(*this, false, false);
			return; // insufficient funds
		}

		valFrom1 = args.m_CallValue;
		valFrom1.Negate();
		valFrom1 += valFrom0;

		f.m_pAccount->get_Balance(valTo0);
		valTo1 = valTo0;
		valTo1 += args.m_CallValue;

		if (valTo1 < valTo0)
		{
			f.OnFrameDone(*this, false, false);
			return; // overflow
		}

		UpdateBalance(fPrev.m_pAccount, valFrom0, valFrom1);
		UpdateBalance(f.m_pAccount, valTo0, valTo1);
	}

	if (f.m_Code.m_n)
	{
		f.m_Args = args;

		// adjust gas
		if (gas >= fPrev.m_Gas)
		{
			f.m_Gas = fPrev.m_Gas;
			fPrev.m_Gas = 0;
		}
		else
		{
			f.m_Gas = gas;
			fPrev.m_Gas -= gas;
		}
	}
	else
		f.OnFrameDone(*this, true, false); // EOA, we're done
}

void EvmProcessor::Context::OnCall(EvmProcessor& p, bool bStatic)
{
	auto nGas = WtoU64(m_Stack.Pop());
	auto& wAddr = m_Stack.Pop();

	Args args;
	if (bStatic)
		args.m_CallValue = Zero;
	else
		args.m_CallValue = m_Stack.Pop();

	auto& wArgsAddr = m_Stack.Pop();
	args.m_Buf.n = WtoU32(m_Stack.Pop());
	args.m_Buf.p = get_Memory(wArgsAddr, args.m_Buf.n);

	DrainGas(40);

	p.Call(Address::W2A(wAddr), args, nGas);
}

OnOpcode(call)
{
	OnCall(p, false);
}

OnOpcode(staticcall)
{
	OnCall(p, true);
}

OnOpcode(revert)
{
	OnFrameDone(p, false, true);
}

} // namespace beam
