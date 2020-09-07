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

#pragma once
#include "block_crypt.h"
#include "bvm_opcodes.h"
#include <boost/intrusive/list.hpp>
#include <boost/intrusive/set.hpp>

#define BVM_ParamType_p Ptr
#define BVM_ParamType_f1 uintBig_t<1>
#define BVM_ParamType_f2 uintBig_t<2>
#define BVM_ParamType_f4 uintBig_t<4>
#define BVM_ParamType_f8 uintBig_t<8>

namespace beam {
namespace bvm {

	struct Type
	{
		typedef uint16_t Size;
		typedef int16_t PtrDiff;

		typedef uintBigFor<Size>::Type uintSize; // big-endian
	};

	struct Limits
	{
		static const uint32_t FarCallDepth = 32;
		static const uint32_t StackSize = 0x8000; // 32K
		static const uint32_t VarKeySize = 256;
		static const uint32_t VarSize = 0x2000; // 8K
	};

	enum class OpCode : uint8_t
	{
#define THE_MACRO(name) n_##name,
		BVM_OpCodes(THE_MACRO)
#undef THE_MACRO

		count
	};

#pragma pack (push, 1)
	struct StackFrame
	{
		Type::uintSize m_Prev;
		Type::uintSize m_RetAddr;
	};

	struct Header
	{
		static const Type::Size s_Version = 1;

		Type::uintSize m_Version;
		Type::uintSize m_NumMethods;

		static const uint32_t s_MethodsMin = 2; // c'tor and d'tor
		typedef Type::uintSize MethodEntry;

		MethodEntry m_pMethod[s_MethodsMin]; // var size
	};
#pragma pack (pop)

	typedef ECC::uintBig ContractID;
	// Contract unique identifier 
	void get_Cid(ContractID&, const Blob& data, const Blob& args);

	struct Buf
	{
		Buf() {}
		Buf(const ByteBuffer& bb) {
			n = static_cast<uint32_t>(bb.size());
			p = n ? &Cast::NotConst(bb.front()) : nullptr;
		}
		Buf(Zero_) {
			p = nullptr;
			n = 0;
		}
		Buf(void* p_, uint32_t n_)
		{
			p = reinterpret_cast<uint8_t*>(p_);
			n = n_;
		}

		uint8_t* p;
		uint32_t n;

		void MoveRaw(uint32_t x) {
			p += x;
			n -= x;
		}

		void Move1() {
			MoveRaw(1);
		}

		bool Move(uint32_t x) {
			if (x > n)
				return false;
			MoveRaw(x);
			return true;
		}

		template <typename T>
		T* get_As(Type::Size nCount = 1) const
		{
			if (sizeof(T) * nCount > n)
				return nullptr;
			return reinterpret_cast<T*>(p);
		}
	};

	class Processor
	{
	protected:

		struct Exc {
			static void Throw() {
				Exc exc;
				throw exc;
			}
		};

		static void Test(bool b) {
			if (!b)
				Exc::Throw();
		}

	private:

		Buf m_Data;

		int m_Flags = 0;
		Type::Size m_Sp;
		Type::Size m_Ip;

		uint8_t m_pStack[Limits::StackSize];

		struct Ptr
			:public Buf
		{
			bool m_Writable;

			template <typename T>
			const T* RGet(Type::Size nCount = 1) const
			{
				auto* ret = get_As<T>(nCount);
				Test(ret != nullptr);
				return ret;
			}

			template <typename T>
			T* WGet(Type::Size nCount = 1) const
			{
				Test(m_Writable);
				return Cast::NotConst(RGet<T>(nCount));
			}
		};

#define THE_MACRO_ParamDecl(name, type) const BVM_ParamType_##type& name,
#define THE_MACRO(name) void On_##name(BVMOp_##name(THE_MACRO_ParamDecl) Zero_);
		BVM_OpCodes(THE_MACRO)
#undef THE_MACRO
#undef THE_MACRO_ParamDecl

		void DoMov(const Ptr&, const uint8_t*, Type::Size nSize);
		void DoXor(const Ptr&, const uint8_t*, Type::Size nSize);
		void DoCmp(const uint8_t*, const uint8_t*, Type::Size nSize);
		void DoAdd(const Ptr&, const uint8_t*, Type::Size nSize);
		void DoJmp(const Type::uintSize&);

		const uint8_t* FetchInstruction(Type::Size n);

		struct BitReader {
			uint8_t m_Value = 0;
			uint8_t m_Bits = 0;
		};

		uint8_t FetchBit(BitReader& br);

		void FetchParam(BitReader& br, Ptr& out);
		void FetchBuffer(BitReader& br, uint8_t* pBuf, Type::Size nSize);

		template <uint32_t nBytes>
		void FetchParam(BitReader& br, uintBig_t<nBytes>& out)
		{
			FetchBuffer(br, out.m_pData, out.nBytes);
		}

		void FetchPtr(BitReader& br, Ptr& out);
		void FetchPtr(BitReader& br, Ptr& out, const Type::uintSize&);
		void SetPtrStack(Ptr& out, Type::Size n);
		void SetPtrData(Ptr& out, Type::Size n);

		void LogStackPtr();
		void LogDeref();
		void LogVarName(const char* szName);
		void LogVarEnd();
		void PushFrame(const Type::uintSize& frame);

		template <bool>
		void TestStackPtr(Type::Size);

	protected:

		struct VarKey
		{
			uint8_t m_p[ContractID::nBytes + 1 + Limits::VarKeySize];
			Type::Size m_Size;
		};

		void SetVarKey(VarKey&);
		void SetVarKey(VarKey&, const Ptr& key, const Type::uintSize& nKey);

		struct FarCalls
		{
			struct Frame
				:public boost::intrusive::list_base_hook<>
			{
				ContractID m_Cid;
				ByteBuffer m_Data;
				Type::Size m_LocalDepth;
			};

			struct Stack
				:public boost::intrusive::list<Frame>
			{
				~Stack() { Clear(); }
				void Clear();
				void Pop();

			} m_Stack;

		} m_FarCalls;

		virtual void AddSig(const ECC::Point&) {}
		virtual void LoadVar(const VarKey&, uint8_t* pVal, Type::Size& nValInOut) {}
		virtual void LoadVar(const VarKey&, ByteBuffer&) {}
		virtual bool SaveVar(const VarKey&, const uint8_t* pVal, Type::Size nVal) { return false; }

	public:

		bool IsDone() const { return m_FarCalls.m_Stack.empty(); }
		Amount m_Charge = 0;

		std::ostringstream* m_pDbg = nullptr;

		void InitStack(const Buf& args); // initial arguments
		void CallFar(const ContractID&, Type::Size iMethod);

		void RunOnce();
	};


	class Compiler
	{
		struct Label
		{
			static const Type::Size s_Invalid = static_cast<Type::Size>(-1);
			Type::Size m_Pos = s_Invalid;
			std::list<Type::Size> m_Refs;
		};

		typedef std::map<Blob, Label> LabelMap;
		LabelMap m_mapLabels;

		static void Fail(const char* sz)
		{
			throw std::runtime_error(sz);
		}

		static Type::Size ToSize(size_t n);

		struct BitWriter
		{
			uint8_t m_Value;
			uint8_t m_Bits = 0;
			Type::Size m_Pos;
		} m_BitWriter;

		void BwFlushStrict();
		void BwAdd(uint8_t);

	public:

		struct MyBlob
			:public Buf
		{
			static bool IsWhitespace(char);

			void ExtractToken(Buf& res, char chSep);
			bool operator == (const char* sz) const
			{
				return
					!memcmp(p, sz, n) &&
					!sz[n];
			}

			const Blob& as_Blob() const
			{
				static_assert(sizeof(*this) == sizeof(Blob));
				return *reinterpret_cast<const Blob*>(this);
			}
		};

		uint32_t m_iLine = 0;
		MyBlob m_Input;
		ByteBuffer m_Result; // without the header

		bool ParseOnce();
		void Finalyze();

	private:
		void ParseLine(MyBlob&);

		void ParseParam_Ptr(MyBlob&);
		void ParseParam_uintBig(MyBlob&, uint32_t nBytes);

		void ParseSignedNumber(MyBlob&, uint32_t nBytes);

		template <uint32_t nBytes>
		void ParseParam_uintBig_t(MyBlob& line)
		{
			ParseParam_uintBig(line, nBytes);
		}

		void ParseParam_PtrDirect(MyBlob&, uint8_t p);
	};


	struct VariableMem
	{
		struct Entry
			:public boost::intrusive::set_base_hook<>
		{
			Blob m_KeyIdx;
			ByteBuffer m_Key;
			ByteBuffer m_Data;

			bool operator < (const Entry& x) const {
				return Blob(m_KeyIdx) < Blob(x.m_KeyIdx);
			}
		};

		class Set
			:public boost::intrusive::multiset<Entry>
		{
		public:

			~Set() { Clear(); }

			void Clear();
			void Delete(Entry&);
			Entry* Find(const Blob& key);
			Entry* Create(const Blob& key);
		};
	};

} // namespace bvm
} // namespace beam
