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
#include "../utility/containers.h"

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
	void get_AssetOwner(PeerID&, const ContractID&, const Asset::Metadata&);

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

		template <int nSize>
		struct ParamType;

		void DoJmp(const Type::uintSize&);

		const uint8_t* FetchInstruction(Type::Size n);

		struct BitReader {
			uint8_t m_Value = 0;
			uint8_t m_Bits = 0;
		};

		uint8_t FetchBit(BitReader& br);

		int FetchOperand(BitReader& br, Ptr& out, bool bW, int nSize, Type::Size nSizeX); // returns num of indirections

		int FetchSize(BitReader& br, Type::Size&);
		Type::Size FetchSizeX(BitReader& br, bool bSizeX);

		void FetchPtr(BitReader& br, Ptr& out);
		void FetchPtr(BitReader& br, Ptr& out, const Type::uintSize&);
		void SetPtrStack(Ptr& out, Type::Size n);
		void SetPtrData(Ptr& out, Type::Size n);

		void LogStackPtr();
		void LogDeref();
		void LogOpCode(const char*);
		void LogVarName(const char* szName);
		void LogValue(const Ptr&);
		void LogVarEnd();
		void PushFrame(const Type::uintSize& nFrame_);

		template <bool>
		void TestStackPtr(Type::Size);

		void HandleAmount(const uintBigFor<Amount>::Type&, const uintBigFor<Asset::ID>::Type&, bool bLock);
		void HandleAmountInner(const uintBigFor<Amount>::Type&, const uintBigFor<Asset::ID>::Type&, bool bLock);
		void HandleAmountOuter(const uintBigFor<Amount>::Type&, const uintBigFor<Asset::ID>::Type&, bool bLock);

	protected:

		struct ArrayContext
		{
			Type::Size m_nCount;
			Type::Size m_nElementWidth;
			Type::Size m_nKeyPos;
			Type::Size m_nKeyWidth;
			Type::Size m_nSize;

			void Realize();

			void MergeSort(uint8_t* p) const;
			void MergeOnce(uint8_t* pDst, const uint8_t* pSrc, Type::Size p0, Type::Size n0, Type::Size p1, Type::Size n1) const;
		};

		struct VarKey
		{
			struct Tag
			{
				static const uint8_t Internal = 0;
				static const uint8_t LockedAmount = 1;
				static const uint8_t Refs = 2;
				static const uint8_t OwnedAsset = 3;
			};

			uint8_t m_p[ContractID::nBytes + 1 + Limits::VarKeySize];
			uint32_t m_Size;

			void Set(const ContractID&);
			void Append(uint8_t nTag, const Blob&);
		};

		void SetVarKey(VarKey&);
		void SetVarKey(VarKey&, uint8_t nTag, const Blob&);
		void SetVarKey(VarKey&, const Ptr& key);

		struct FarCalls
		{
			struct Frame
				:public boost::intrusive::list_base_hook<>
			{
				ContractID m_Cid;
				ByteBuffer m_Data;
				Type::Size m_LocalDepth;
			};

			intrusive::list_autoclear<Frame> m_Stack;

		} m_FarCalls;

		virtual void LoadVar(const VarKey&, uint8_t* pVal, Type::Size& nValInOut) {}
		virtual void LoadVar(const VarKey&, ByteBuffer&) {}
		virtual bool SaveVar(const VarKey&, const uint8_t* pVal, Type::Size nVal) { return false; }

		virtual Asset::ID AssetCreate(const Asset::Metadata&, const PeerID&) { return 0; }
		virtual bool AssetEmit(Asset::ID, const PeerID&, AmountSigned) { return false; }
		virtual bool AssetDestroy(Asset::ID, const PeerID&) { return false; }

		bool LoadFixedOrZero(const VarKey&, uint8_t* pVal, Type::Size);
		bool SaveNnz(const VarKey&, const uint8_t* pVal, Type::Size);

		template <uint32_t nBytes>
		bool Load_T(const VarKey& vk, uintBig_t<nBytes>& x) {
			return LoadFixedOrZero(vk, x.m_pData, static_cast<Type::Size>(x.nBytes));
		}

		template <uint32_t nBytes>
		bool Save_T(const VarKey& vk, const uintBig_t<nBytes>& x) {
			return SaveNnz(vk, x.m_pData, static_cast<Type::Size>(x.nBytes));
		}
		std::vector<ECC::Point::Native> m_vPks;
		ECC::Point::Native& AddSigInternal(const ECC::Point&);

		ECC::Point::Native m_FundsIO;

		struct AssetVar {
			VarKey m_vk;
			PeerID m_Owner;
		};

		Asset::ID get_AssetStrict(AssetVar&, const uintBigFor<Asset::ID>::Type&);
		void SetAssetKey(AssetVar&, const uintBigFor<Asset::ID>::Type&);

	private:
		void HandleRef(const ContractID&, bool bAdd);
		bool HandleRefRaw(const VarKey&, bool bAdd);

	public:

		struct OpCodesImpl;

		bool IsDone() const { return m_FarCalls.m_Stack.empty(); }
		Amount m_Charge = 0;

		ECC::Hash::Processor* m_pSigValidate = nullptr; // assign it to allow sig validation
		void CheckSigs(const ECC::Point& comm, const ECC::Signature&);

		std::ostringstream* m_pDbg = nullptr;

		void InitStack(const Buf& args); // initial arguments
		void CallFar(const ContractID&, Type::Size iMethod);

		void RunOnce();
	};


	class Compiler
	{
		struct Label
			:public intrusive::set_base_hook<Blob>
		{
			static const Type::Size s_Invalid = static_cast<Type::Size>(-1);
			Type::Size m_Pos = s_Invalid;
			std::list<Type::Size> m_Refs;

			struct Map
				:public intrusive::multiset_autoclear<Label>
			{
				Label& operator [] (const Blob& key) {
					auto it = find(key, Label::Comparator());
					if (end() != it)
						return *it;

					return *Create(key);
				}
			};
		};

		struct Scope
			:public boost::intrusive::list_base_hook<>
		{
			Label::Map m_Labels;

			typedef intrusive::list_autoclear<Scope> List;

			struct Variable
			{
				Type::Size m_Size;
				Type::Size m_Pos = Label::s_Invalid;

				bool IsValid() const { return Label::s_Invalid != m_Pos; }

				typedef std::map<Blob, Variable> Map;
			};

			Variable::Map m_mapVars;

			Type::Size m_nSizeArgs = sizeof(StackFrame);
			Type::Size m_nSizeLocal = 0;
		};


		Scope::List m_ScopesActive;
		Scope::List m_ScopesDone;

		void ScopeOpen();
		void ScopeClose();

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
		void BwAddPtrType(uint8_t);

		static uint8_t IsPtrPrefix(char);

	public:

		struct MyBlob
			:public Buf
		{
			static bool IsWhitespace(char);

			void ExtractToken(Buf& res, char chSep);
			bool operator == (const char* sz) const;

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

		void Start();
		void Finalyze();

	private:
		void ParseLine(MyBlob&);

		Type::Size ParseSizeX(MyBlob&);
		bool ParseSize(MyBlob&, Type::Size&);
		void WriteSizeX(MyBlob&, Type::Size&, bool);
		uint8_t* ParseOperand(MyBlob&, bool bW, int nLen, Type::Size nSizeX);

		void ParseSignedNumber(MyBlob&, uint32_t nBytes);
		bool ParseSignedNumberOrLabel(MyBlob&, uint32_t nBytes);
		void ParseHex(MyBlob&, uint32_t nBytes);
		void ParseLabel(MyBlob&);
		char ParseVariableType(MyBlob& line, Type::Size&);
		void ParseVariableDeclaration(MyBlob& line, bool bArg);
		void ParseVariableUse(MyBlob&, uint32_t nBytes, bool bPosOrSize);

		uint64_t ParseUnsignedRaw(MyBlob&);
		void ParseSignedRaw(MyBlob&, uint32_t nBytes, uintBigFor<uint64_t>::Type&);
	};

} // namespace bvm
} // namespace beam
