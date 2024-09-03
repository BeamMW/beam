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

namespace beam {

	namespace Shaders {

		typedef ECC::Point PubKey;
		typedef ECC::Point Secp_point_data;
		typedef ECC::Point::Storage Secp_point_dataEx;
		typedef ECC::Scalar Secp_scalar_data;
		typedef beam::Asset::ID AssetID;
		typedef ECC::uintBig ShaderID;
		typedef ECC::uintBig HashValue;
		typedef beam::uintBig_t<64> HashValue512;
		using beam::ContractID;
		using beam::Amount;
		using beam::Height;
		using beam::Timestamp;
		using beam::HeightPos;

		template<bool bToShader, typename T>
		inline void ConvertOrd(T& x)
		{
			if constexpr (bToShader)
				x = beam::ByteOrder::to_le(x);
			else
				x = beam::ByteOrder::from_le(x);
		}

#define HOST_BUILD
#include "Shaders/Eth.h" // Rlp encoding
	}

namespace Evm {

void Processor::Fail()
{
	throw std::runtime_error("evm");
}

void Processor::Test(bool b)
{
	if (!b)
		Fail();
}

uint32_t Processor::WtoU32(const Word& w)
{
	uint32_t ret;
	Test(memis0(w.m_pData, w.nBytes - sizeof(ret)));

	w.ExportWord<sizeof(Word) / sizeof(ret) - 1>(ret);
	return ret;
}

uint64_t Processor::WtoU64(const Word& w)
{
	uint64_t ret;
	Test(memis0(w.m_pData, w.nBytes - sizeof(ret)));

	w.ExportWord<sizeof(Word) / sizeof(ret) - 1>(ret);
	return ret;
}

void AddressForContract(Address& res, const Address& from, uint64_t nonce)
{
	KeccakProcessor<Word::nBits> hp;

	// Rlp encode
	Shaders::Eth::Rlp::Node pC[] = {
		Shaders::Eth::Rlp::Node(from),
		Shaders::Eth::Rlp::Node(nonce)
	};

	Shaders::Eth::Rlp::Node(pC).Write(hp);

	Word wRes;
	hp >> wRes;
	res = Address::W2A(wRes);
}

void Processor::Method::SetSelector(const Blob& b)
{
	Word w;
	HashOf(w, b);
	assert(w.nBytes >= m_Selector.nBytes);
	memcpy(m_Selector.m_pData, w.m_pData, m_Selector.nBytes);
}

void Processor::Method::SetSelector(const char* szSignature)
{
	Blob blob;
	blob.p = szSignature;
	blob.n = (uint32_t) strlen(szSignature);
	SetSelector(blob);
}

void Processor::Reset()
{
	InitVars();
	m_lstFrames.Clear();
	m_Top.m_lstUndo.Clear();
}

void Processor::InitVars()
{
	m_Top.m_pAccount = nullptr;
	m_Top.m_Gas = 0;
}


/*

	// 0x30 range - closure state.
	GASPRICE       OpCode = 0x3a, gas=2
	EXTCODEHASH    OpCode = 0x3f, gas=700??

	// 0x40 range - block operations.
	BASEFEE     OpCode = 0x48

	// 0x50 range - 'storage' and execution.
	MSIZE    OpCode = 0x59, gas=2

	// 0xf0 range - closures.
	CREATE       OpCode = 0xf0, gas=32000
	CALLCODE     OpCode = 0xf2
	DELEGATECALL OpCode = 0xf4
	CREATE2      OpCode = 0xf5

	INVALID      OpCode = 0xfe
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
	macro(0x31, balance, 700) \
	macro(0x32, origin, 2) \
	macro(0x33, caller, 2) \
	macro(0x34, callvalue, 2) \
	macro(0x35, calldataload, 3) \
	macro(0x36, calldatasize, 2) \
	macro(0x37, calldatacopy, 0) \
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
	macro(0x47, selfbalance, 3) \
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
	macro(0xff, selfdestruct, 0) \

#define EvmOpcodes_All(macro) \
	EvmOpcodes_Binary(macro) \
	EvmOpcodes_Unary(macro) \
	EvmOpcodes_Custom(macro) \

struct Processor::Context :public Processor::Frame
{
#define THE_MACRO(code, name, gas) void On_##name(Processor&);
	EvmOpcodes_Custom(THE_MACRO)
#undef THE_MACRO

#define THE_MACRO(code, name, gas) \
	void OnBinary_##name(Word& a, Word& b); \
	void On_##name(Processor&) \
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
	void On_##name(Processor&) \
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

	void RunOnce(Processor&);

	void UndoChanges(Processor& p);

	void LogOpCode(const char* sz);
	void LogOpCode(const char* sz, uint32_t n);
	void LogOperand(const Word&);

	BaseFrame& get_Prev(Processor& p)
	{
		assert(!p.m_lstFrames.empty());
		if (p.m_lstFrames.size() == 1)
			return p.m_Top;

		auto it = Frame::List::s_iterator_to(*this);
		--it;
		return *it;
	}

	const Address& get_Caller(Processor& p)
	{
		return get_Prev(p).m_pAccount->m_Key;
	}

	uint8_t* get_Memory(const Word& wAddr, uint32_t nSize);
	static uint64_t get_MemoryCost(uint32_t nSize);

	void Jump(const Word&);
	void PushN(uint32_t n);
	void DupN(uint32_t n);
	void SwapN(uint32_t n);
	void LogN(uint32_t n);
	void OnFrameDone(Processor&, bool bSuccess, bool bHaveRetval);
	void OnCall(Processor&, bool bStatic);

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

	Height get_BlockArg(Processor& p, BlockHeader& bh)
	{
		Height h = WtoU64(m_Stack.Pop());
		Test(p.get_BlockHeader(bh, h));
		return h;
	}

	Height get_BlockLast(Processor& p, BlockHeader& bh)
	{
		Height h = p.get_Height();
		Test(p.get_BlockHeader(bh, h));
		return h;
	}
};

void Processor::Context::LogOpCode(const char* sz)
{
	printf("\t%08x, %s\n", m_Code.m_Ip - 1, sz);
}

void Processor::Context::LogOpCode(const char* sz, uint32_t n)
{
	printf("\t%08x, %s%u\n", m_Code.m_Ip - 1, sz, n);
}

void Processor::Context::LogOperand(const Word& x)
{
	char sz[Word::nTxtLen + 1];
	x.Print(sz);
	printf("\t\t%s\n", sz);
}

void Processor::BaseFrame::DrainGas(uint64_t n)
{
	if (m_Gas < n)
	{
		m_Gas = 0;
		throw Context::NoGasException();
	}

	m_Gas -= n;
}

uint8_t* Processor::Context::get_Memory(const Word& wAddr, uint32_t nSize)
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

uint64_t Processor::Context::get_MemoryCost(uint32_t nSize)
{
	// cost := a * 3 + floor(a^2 / 512)
	uint64_t a = nSize / Word::nBytes;
	if (nSize % Word::nBytes)
		a++;

	return (a * 3) + (a * a / 512);
}

struct Processor::UndoOp::TouchAccount
	:public Base
{
	Account& m_Account;
	TouchAccount(Account& acc) :m_Account(acc) {}
	~TouchAccount() override {}
	void Undo(Processor& p) override
	{
		p.m_Accounts.Delete(m_Account);
	}
};

struct Processor::UndoOp::TouchSlot
	:public Base
{
	Account& m_Account;
	Account::Slot& m_Slot;
	TouchSlot(Account& acc, Account::Slot& slot)
		:m_Account(acc)
		,m_Slot(slot)
	{}
	~TouchSlot() override {}
	void Undo(Processor& p) override
	{
		m_Account.m_Slots.Delete(m_Slot);
	}
};

template <typename T>
struct Processor::UndoOp::VarSet_T
	:public Base
{
	Account::Variable<T>& m_Trg;
	T m_Val0;

	VarSet_T(Account::Variable<T>& trg)
		:m_Trg(trg)
	{
		m_Val0 = trg.m_Value;
		trg.m_Modified++;
	}

	~VarSet_T() override {}

	void Undo(Processor&) override
	{
		assert(m_Trg.m_Modified);
		m_Trg.m_Modified--;

		m_Trg.m_Value = m_Val0;
	}
};

struct Processor::UndoOp::SetCode
	:public VarSet_T<Blob>
{
	typedef VarSet_T<Blob> Base;

	ByteBuffer m_Buf;
	using Base::Base;
	~SetCode() override {}

};

template <typename T>
void Processor::BaseFrame::UpdateVariable(Account::Variable<T>& trg, const T& x)
{
	auto* pOp = new UndoOp::VarSet_T<T>(trg);
	m_lstUndo.push_back(*pOp);
	trg.m_Value = x;
}

void Processor::BaseFrame::UpdateCode(Account& acc, const Blob& code)
{
	auto* pOp = new UndoOp::SetCode(acc.m_Code);
	m_lstUndo.push_back(*pOp);

	code.Export(pOp->m_Buf);
	acc.m_Code.m_Value = pOp->m_Buf;
}

void Processor::BaseFrame::UpdateBalance(Account& acc, const Word& val1)
{
	if (acc.m_Balance.m_Value != val1)
		UpdateVariable(acc.m_Balance, val1);
}

Processor::Account& Processor::TouchAccount(BaseFrame& f, const Address& addr, bool& wasWarm)
{
	auto it = m_Accounts.find(addr, Account::Comparator());
	if (m_Accounts.end() != it)
	{
		wasWarm = true;
		return *it;
	}

	wasWarm = false;
	auto& ret = LoadAccount(addr);

	auto* pOp = new UndoOp::TouchAccount(ret);
	f.m_lstUndo.push_back(*pOp);

	return ret;
}

void Processor::BaseFrame::EnsureAccountCreated(Account& acc)
{
	if (!acc.m_Exists.m_Value)
		UpdateVariable(acc.m_Exists, true);
}

Processor::Account::Slot& Processor::TouchSlot(BaseFrame& f, Account& acc, const Word& key, bool& wasWarm)
{
	auto it = acc.m_Slots.find(key, Account::Slot::Comparator());
	if (acc.m_Slots.end() != it)
	{
		wasWarm = true;
		return *it;
	}

	wasWarm = false;
	auto& ret = LoadSlot(acc, key);

	auto* pOp = new UndoOp::TouchSlot(acc, ret);
	f.m_lstUndo.push_back(*pOp);

	return ret;
}

void Processor::RunOnce()
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
			printf("\tno-gas exc\n");

			assert(!m_lstFrames.empty());
			auto& f = Cast::Up<Context>(m_lstFrames.back());
			f.OnFrameDone(*this, false, false); // assume during revert no gas is drained
		}
	}
	catch (const std::exception&)
	{
		printf("\tcrash exc\n");

		while (!m_lstFrames.empty())
		{
			auto& f = Cast::Up<Context>(m_lstFrames.back());
			f.UndoChanges(*this);
			m_lstFrames.Delete(f);
		}

		// Don't touch m_Top undo list. It only contains balance update w.r.t. gas

		m_RetVal.m_Success = false;
		ZeroObject(m_RetVal.m_Blob);
	}

}

void Processor::Context::RunOnce(Processor& p)
{
	uint8_t nCode;
	if (m_Code.m_Ip < m_Code.m_n)
	{
		nCode = m_Code.m_p[m_Code.m_Ip];
		m_Code.m_Ip++;
	}
	else
		nCode = 0; // stop

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

#define OnOpcode(name) void Processor::Context::On_##name(Processor& p)
#define OnOpcodeBinary(name) void Processor::Context::OnBinary_##name(Word& a, Word& b)
#define OnOpcodeUnary(name) void Processor::Context::OnUnary_##name(Word& a)

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
		// static_gas = 10
		// dynamic_gas = 50 * exponent_byte_size
		uint32_t nBytes = uintBigImpl::_GetOrderBytes(b.m_pData, b.nBytes);
		DrainGas(10u + 50u * nBytes);
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

void Processor::HashOf(Word& w, const Blob& b)
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

	// static_gas = 30
	// dynamic_gas = 6 * minimum_word_size + memory_expansion_cost
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
	m_Stack.Push() = m_pAccount->m_Key.ToWord();
}

OnOpcode(balance)
{
	auto& w1 = m_Stack.get_At(0);

	bool wasWarm;
	auto& acc = p.TouchAccount(*this, Address::W2A(w1), wasWarm);

	w1 = acc.m_Balance.m_Value;
}

OnOpcode(origin)
{
	p.m_Top.m_pAccount->m_Key.ToWord(m_Stack.Push());
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

OnOpcode(calldatacopy)
{
	auto& w1 = m_Stack.Pop();
	auto& w2 = m_Stack.Pop();
	auto& w3 = m_Stack.Pop();

	auto nAddrSrc = WtoU32(w2);
	auto nSize = WtoU32(w3);

	// 2 + 3 * (number of words copied, rounded up)
	// 2 is paid for the operation plus 3 for each word copied(rounded up).
	DrainGas(2 + 3u * Context::NumWordsRoundUp(nSize));

	Test(nSize <= m_Args.m_Buf.n - nAddrSrc); // overflow-resistant
	const auto* pSrc = reinterpret_cast<const uint8_t*>(m_Args.m_Buf.p) + nAddrSrc;

	auto* pDst = get_Memory(w1, nSize);
	memcpy(pDst, pSrc, nSize);
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
	auto& w1 = m_Stack.get_At(0);

	bool wasWarm;
	auto& acc = p.TouchAccount(*this, Address::W2A(w1), wasWarm);

	w1 = acc.m_Code.m_Value.n;
}

OnOpcode(extcodecopy)
{
	bool wasWarm;
	auto& acc = p.TouchAccount(*this, Address::W2A(m_Stack.Pop()), wasWarm);


	auto& wOffsetDst = m_Stack.Pop();
	auto nOffsetSrc = WtoU32(m_Stack.Pop());
	auto nSize = WtoU32(m_Stack.Pop());

	// 700 + 3 * (number of words copied, rounded up)
	// 700 is paid for the operation plus 3 for each word copied(rounded up).
	DrainGas(700 + 3u * Context::NumWordsRoundUp(nSize));

	auto nEndSrc = nOffsetSrc + nSize;
	Test(nEndSrc >= nOffsetSrc);
	Test(acc.m_Code.m_Value.n >= nEndSrc);

	auto* pDst = get_Memory(wOffsetDst, nSize);
	memcpy(pDst, (const uint8_t*) acc.m_Code.m_Value.p + nOffsetSrc, nSize);
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

	bool wasWarm;
	auto& slot = p.TouchSlot(*this, *m_pAccount, w, wasWarm);

	w = slot.m_Data.m_Value;

	LogOperand(w);
}

OnOpcode(sstore)
{
	const Word& w1 = m_Stack.Pop();
	const Word& w2 = m_Stack.Pop();

	bool wasWarm;
	auto& slot = p.TouchSlot(*this, *m_pAccount, w1, wasWarm);

	if (slot.m_Data.m_Value != w2)
		UpdateVariable(slot.m_Data, w2);


	//// ((value != 0) && (storage_location == 0)) ? 20000 : 5000
	//// 20000 is paid when storage value is set to non - zero from zero. 5000 is paid when the storage value's zeroness remains unchanged or is set to zero.								
	//{
	//	bool b0 = (wPrev == Zero);
	//	bool b1 = (w2 == Zero);
	//	if (b0 == b1)
	//		DrainGas(5000);
	//	else
	//	{
	//		if (b0)
	//			DrainGas(20000);
	//		else
	//			m_Gas += 15000; // refund gas
	//	}
	//}

	LogOperand(w1);
	LogOperand(w2);
}

void Processor::Context::Jump(const Word& w)
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

OnOpcode(selfbalance)
{
	m_Stack.Push() = m_pAccount->m_Balance.m_Value;
}

void Processor::get_ChainID(Word& w)
{
	w = Zero;
}

template <bool bPadLeft>
void Processor::Context::AssignPartial(Word& w, const uint8_t* p, uint32_t n)
{
	assert(n <= w.nBytes);
	auto* pDst = w.m_pData;
	auto* pPad = pDst;

	uint32_t nPad = w.nBytes - n;
	if constexpr (bPadLeft)
		pDst += nPad; // padding on left, data on right
	else
		pPad += n;

	memset0(pPad, nPad);
	memcpy(pDst, p, n);
}

void Processor::Context::PushN(uint32_t n)
{
	auto pSrc = m_Code.Consume(n);
	auto& wDst = m_Stack.Push();
	AssignPartial<true>(wDst, pSrc, n);
}

void Processor::Context::DupN(uint32_t n)
{
	auto& wDst = m_Stack.Push();
	wDst = m_Stack.get_At(n);
}

void Processor::Context::SwapN(uint32_t n)
{
	auto& w1 = m_Stack.get_At(0);
	auto& w2 = m_Stack.get_At(n);

	std::swap(w1, w2);
}

void Processor::Context::LogN(uint32_t n)
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

void Processor::Context::UndoChanges(Processor& p)
{
	while (!m_lstUndo.empty())
	{
		auto& op = m_lstUndo.back();
		op.Undo(p);
		m_lstUndo.Delete(op);
	}
}

void Processor::Context::OnFrameDone(Processor& p, bool bSuccess, bool bHaveRetval)
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
	Account* pMyAccount = m_pAccount;

	Context* pPrev = p.m_lstFrames.empty() ? nullptr : &Cast::Up<Context>(p.m_lstFrames.back());
	BaseFrame& fPrev = pPrev ? *pPrev : p.m_Top;
	fPrev.m_Gas += m_Gas;

	if (bSuccess)
		fPrev.m_lstUndo.splice(fPrev.m_lstUndo.end(), m_lstUndo);
	else
		UndoChanges(p);


	switch (m_Type)
	{
	case Type::CreateContract:

		if (bSuccess)
		{
			fPrev.UpdateCode(*pMyAccount, p.m_RetVal.m_Blob);
			ZeroObject(p.m_RetVal.m_Blob);
		}

		if (pPrev)
		{
			auto& wRes = pPrev->m_Stack.Push();
			if (bSuccess)
				pMyAccount->m_Key.ToWord(wRes);
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
			auto* pRes = pPrev->get_Memory(wResAddr, nResSize); // may rasise no-gas exc

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
	Args args;
	args.m_CallValue = m_Stack.Pop();
	auto& wOffset = m_Stack.Pop();

	args.m_Buf.n = WtoU32(m_Stack.Pop());
	args.m_Buf.p = get_Memory(wOffset, args.m_Buf.n);

	auto& wSalt = m_Stack.Pop();

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

	p.CallInternal(aNew, args, m_Gas, true);
}

//void Processor::BaseFrame::PushUndoAccountDelete(IAccount* pAccount)
//{
//	auto* pOp = new UndoOp::AccountDelete;
//	m_lstUndo.push_back(*pOp);
//	pOp->m_pAccount = pAccount;
//}

void Processor::CallInternal(const Address& addr, const Args& args, uint64_t gas, bool isDeploy)
{
	BaseFrame& fPrev = m_lstFrames.empty() ? m_Top : m_lstFrames.back();

	bool wasWarm;
	auto& acc = TouchAccount(fPrev, addr, wasWarm);

	auto* pF = new Frame;
	m_lstFrames.push_back(*pF);
	pF->m_pAccount = &acc;

	auto& f = Cast::Up<Context>(*pF);

	bool bCreated = !acc.m_Exists.m_Value;
	if (bCreated)
	{
		f.EnsureAccountCreated(acc);
		ZeroObject(f.m_Code);
	}
	else
	{
		f.m_Code = acc.m_Code.m_Value;
		f.m_Code.m_Ip = 0;
	}

	ZeroObject(f.m_Args);
	f.m_Gas = 0;

	if (isDeploy)
	{
		f.m_Type = Frame::Type::CreateContract;

		if (!bCreated)
		{
			f.OnFrameDone(*this, false, false);
			return; // addr collision
		}
	}
	else
		f.m_Type = Frame::Type::CallRetStatus;

	if (args.m_CallValue != Zero)
	{
		if (fPrev.m_pAccount->m_Balance.m_Value < args.m_CallValue)
		{
			f.OnFrameDone(*this, false, false);
			return; // insufficient funds
		}

		Word valFrom1 = args.m_CallValue;
		valFrom1.Negate();
		valFrom1 += fPrev.m_pAccount->m_Balance.m_Value;

		Word valTo1 = acc.m_Balance.m_Value;
		valTo1 += args.m_CallValue;

		if (valTo1 < acc.m_Balance.m_Value)
		{
			f.OnFrameDone(*this, false, false);
			return; // overflow
		}

		f.UpdateBalance(*fPrev.m_pAccount, valFrom1);
		f.UpdateBalance(acc, valTo1);
	}

	if (isDeploy)
	{
		f.m_Code = args.m_Buf;
		f.m_Args.m_CallValue = args.m_CallValue;
	}
	else
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

void Processor::Call(const Address& addr, const Args& args, bool isDeploy)
{
	assert(m_lstFrames.empty());
	CallInternal(addr, args, m_Top.m_Gas, isDeploy);
}

void Processor::Context::OnCall(Processor& p, bool bStatic)
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

	p.CallInternal(Address::W2A(wAddr), args, nGas, false);
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

OnOpcode(selfdestruct)
{
	// 5000 + ((create_new_account) ? 25000 : 0)
	// 5000 for the operation plus 25000 if a new account is also created.A refund of 24000 gas is also added to the refund counter for self - destructing the account.
	uint64_t nGas = 5000;

	const auto& aTrg = Address::W2A(m_Stack.Pop()); // funds recipient

	if (m_pAccount->m_Balance.m_Value != Zero)
	{
		Test(aTrg != m_pAccount->m_Key);

		bool wasWarm;
		auto& acc = p.TouchAccount(*this, aTrg, wasWarm);
		EnsureAccountCreated(acc);

		Word w1 = acc.m_Balance.m_Value;
		w1 += m_pAccount->m_Balance.m_Value;
		Test(w1 > acc.m_Balance.m_Value);

		UpdateBalance(acc, w1);

		w1 = Zero;
		UpdateBalance(*m_pAccount, w1);
	}

	UpdateCode(*m_pAccount, Blob(nullptr, 0));
	UpdateVariable(m_pAccount->m_Exists, false);

	DrainGas(nGas);
	OnFrameDone(p, true, false);
}

} // namespace Evm
} // namespace beam
