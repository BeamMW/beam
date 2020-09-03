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



#pragma pack (push, 1)
	struct StackFrame
	{
		Type::uintSize m_Prev;
		Type::uintSize m_RetAddr;
	};

	struct Header
	{
		Type::uintSize m_NumMethods;

		static const uint32_t s_MethodsMin = 2; // c'tor and d'tor
		typedef Type::uintSize MethodEntry;

		MethodEntry m_pMethod[s_MethodsMin]; // var size
	};
#pragma pack (pop)



	class Processor
	{
		static const uint32_t s_StackSize = 0x10000; // 64K

		Blob m_Data;

		int m_Flags = 0;
		Type::Size m_Sp = 0;
		Type::Size m_Ip = 0;

		uint8_t m_pStack[s_StackSize];

		struct Ptr
			:public Blob // ptr + max accessible size
		{
			bool m_Writable;

			template <typename T>
			const T* RGet(Type::Size nCount = 1) const
			{
				Test(sizeof(T) * nCount <= n);
				return static_cast<const T*>(p);
			}

			template <typename T>
			T* WGet(Type::Size nCount = 1) const
			{
				Test(m_Writable);
				return Cast::NotConst(RGet<T>(nCount));
			}
		};

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


#define THE_MACRO_ParamDecl(name, type) const BVM_ParamType_##type& name,
#define THE_MACRO(id, name) void On_##name(BVMOp_##name(THE_MACRO_ParamDecl) Zero_);
		BVM_OpCodes(THE_MACRO)
#undef THE_MACRO
#undef THE_MACRO_ParamDecl

		void DoMov(const Ptr&, const uint8_t*, Type::Size nSize);
		void DoXor(const Ptr&, const uint8_t*, Type::Size nSize);
		void DoCmp(const uint8_t*, const uint8_t*, Type::Size nSize);
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

	public:

		virtual void AddSig(const ECC::Point&) {}
		virtual bool LoadVar(const uint8_t* pKey, Type::Size nKey, uint8_t* pVal, Type::Size nVal) { return false; }
		virtual bool SaveVar(const uint8_t* pKey, Type::Size nKey, const uint8_t* pVal, Type::Size nVal) { return false; }
		virtual bool DelVar(const uint8_t* pKey, Type::Size nKey) { return false; }

		bool m_Fin = false;
		Amount m_Charge = 0;

		void Setup(const Blob&, Type::Size ip);
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
			:public Blob
		{
			void StripBeg(char);

			void ExtractToken(MyBlob& res, char chSep);
			bool operator == (const char* sz) const
			{
				return
					!memcmp(p, sz, n) &&
					!sz[n];
			}
		};

		MyBlob m_Input;
		ByteBuffer m_Result; // without the header

		bool ParseOnce();
		void Finalyze();

	private:
		void ParseLine(MyBlob&);

		void ParseParam_Ptr(MyBlob&);
		void ParseParam_uintBig(MyBlob&, uint32_t nBytes);

		template <uint32_t nBytes>
		void ParseParam_uintBig_t(MyBlob& line)
		{
			ParseParam_uintBig(line, nBytes);
		}

		void ParseParam_PtrDirect(MyBlob&, uint8_t p);
	};


} // namespace bvm
} // namespace beam
