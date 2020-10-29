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

#define HOST_BUILD

#include "../../core/block_rw.h"
#include "../../utility/test_helpers.h"
#include "../../utility/blobmap.h"
#include "../bvm2.h"
#include "../bvm2_impl.h"

#include <sstream>

namespace Shaders {

#ifdef _MSC_VER
#	pragma warning (disable : 4200 4702) // unreachable code
#endif // _MSC_VER

#include "../Shaders/vault.h"
#include "../Shaders/oracle.h"
#include "../Shaders/dummy.h"
#include "../Shaders/StableCoin.h"

#define export

#include "../Shaders/common.h"
#include "../Shaders/Math.h"
#include "../Shaders/MergeSort.h"

	namespace Env {


		beam::bvm2::Processor* g_pEnv = nullptr;

#define PAR_DECL(type, name) type name
#define PAR_PASS(type, name) name
#define MACRO_COMMA ,

#define THE_MACRO(id, ret, name) ret name(BVMOp_##name(PAR_DECL, MACRO_COMMA)) { return Cast::Up<beam::bvm2::ProcessorPlusEnv>(g_pEnv)->OnHost_##name(BVMOp_##name(PAR_PASS, MACRO_COMMA)); }
		BVMOpsAll_Common(THE_MACRO)
#undef THE_MACRO

#define THE_MACRO(id, ret, name) ret name(BVMOp_##name(PAR_DECL, MACRO_COMMA)) { return Cast::Up<beam::bvm2::ProcessorPlusEnv_Contract>(g_pEnv)->OnHost_##name(BVMOp_##name(PAR_PASS, MACRO_COMMA)); }
		BVMOpsAll_Contract(THE_MACRO)
#undef THE_MACRO

#define THE_MACRO(id, ret, name) ret name(BVMOp_##name(PAR_DECL, MACRO_COMMA)) { return Cast::Up<beam::bvm2::ProcessorPlusEnv_Manager>(g_pEnv)->OnHost_##name(BVMOp_##name(PAR_PASS, MACRO_COMMA)); }
		BVMOpsAll_Manager(THE_MACRO)
#undef THE_MACRO

#undef MACRO_COMMA
#undef PAR_PASS
#undef PAR_DECL

		void CallFarN(const ContractID& cid, uint32_t iMethod, void* pArgs, uint32_t nArgs);

		template <typename T>
		void CallFar_T(const ContractID& cid, T& args)
		{
			args.template Convert<true>();
			CallFarN(cid, args.s_iMethod, &args, sizeof(args));
			args.template Convert<false>();
		}

	} // namespace Env

	namespace Vault {
#include "../Shaders/vault.cpp"
	}
	namespace Oracle {
#include "../Shaders/oracle.cpp"
	}
	namespace StableCoin {
#include "../Shaders/StableCoin.cpp"
	}

#ifdef _MSC_VER
#	pragma warning (default : 4200 4702)
#endif // _MSC_VER
}

namespace ECC {

	void SetRandom(uintBig& x)
	{
		GenRandom(x);
	}

	void SetRandom(Scalar::Native& x)
	{
		Scalar s;
		while (true)
		{
			SetRandom(s.m_Value);
			if (!x.Import(s))
				break;
		}
	}

	void SetRandom(Key::IKdf::Ptr& pRes)
	{
		uintBig seed;
		SetRandom(seed);
		HKdf::Create(pRes, seed);
	}

}

int g_TestsFailed = 0;

void TestFailed(const char* szExpr, uint32_t nLine)
{
	printf("Test failed! Line=%u, Expression: %s\n", nLine, szExpr);
	g_TestsFailed++;
	fflush(stdout);
}

#define verify_test(x) \
	do { \
		if (!(x)) \
			TestFailed(#x, __LINE__); \
	} while (false)

#define fail_test(msg) TestFailed(msg, __LINE__)

namespace beam {
namespace bvm2 {

	void TestMergeSort()
	{
		std::vector<uint64_t> buf, buf2;

		for (uint32_t nCount = 0; nCount < 500; nCount++)
		{
			buf.resize(nCount + 2);
			buf2.resize(nCount + 2);
			uint64_t* p = &buf.front();

			for (uint32_t n = 0; n < 10; n++)
			{
				p[0] = 1;
				p[nCount + 1] = 2;
				buf2[0] = 3;
				buf2[nCount + 1] = 4;
				ECC::GenRandom(p + 1, sizeof(uint64_t) * nCount);

				uint64_t* pRes = Shaders::MergeSort<uint64_t>::Do(p + 1, &buf2.front() + 1, nCount);

				verify_test(1 == p[0]);
				verify_test(2 == p[nCount + 1]);
				verify_test(3 == buf2[0]);
				verify_test(4 == buf2[nCount + 1]);

				for (uint32_t i = 0; i + 1 < nCount; i++)
					verify_test(pRes[i] <= pRes[i + 1]);

			}

		}
	}

	struct MyProcessor
		:public ProcessorContract
	{
		BlobMap::Set m_Vars;

		struct Action
			:public boost::intrusive::list_base_hook<>
		{
			virtual ~Action() = default;
			virtual void Undo(MyProcessor&) = 0;

			typedef intrusive::list_autoclear<Action> List;
		};

		Action::List m_lstUndo;

		void UndoChanges(size_t nTrg = 0)
		{
			while (m_lstUndo.size() > nTrg)
			{
				auto& x = m_lstUndo.back();
				x.Undo(*this);
				m_lstUndo.Delete(x);
			}
		}


		virtual void LoadVar(const VarKey& vk, uint8_t* pVal, uint32_t& nValInOut) override
		{
			auto* pE = m_Vars.Find(Blob(vk.m_p, vk.m_Size));
			if (pE && !pE->m_Data.empty())
			{
				auto n0 = static_cast<uint32_t>(pE->m_Data.size());
				memcpy(pVal, &pE->m_Data.front(), std::min(n0, nValInOut));
				nValInOut = n0;
			}
			else
				nValInOut = 0;
		}

		virtual void LoadVar(const VarKey& vk, ByteBuffer& res) override
		{
			auto* pE = m_Vars.Find(Blob(vk.m_p, vk.m_Size));
			if (pE)
				res = pE->m_Data;
			else
				res.clear();
		}

		virtual bool SaveVar(const VarKey& vk, const uint8_t* pVal, uint32_t nVal) override
		{
			return SaveVar(Blob(vk.m_p, vk.m_Size), pVal, nVal);
		}

		struct Action_Var
			:public Action
		{
			ByteBuffer m_Key;
			ByteBuffer m_Value;

			virtual void Undo(MyProcessor& p) override
			{
				p.SaveVar2(m_Key, m_Value.empty() ? nullptr : &m_Value.front(), static_cast<uint32_t>(m_Value.size()), nullptr);
			}
		};

		bool SaveVar(const Blob& key, const uint8_t* pVal, uint32_t nVal)
		{
			auto pUndo = std::make_unique<Action_Var>();
			bool bRet = SaveVar2(key, pVal, nVal, pUndo.get());

			m_lstUndo.push_back(*pUndo.release());
			return bRet;
		}

		bool SaveVar2(const Blob& key, const uint8_t* pVal, uint32_t nVal, Action_Var* pAction)
		{
			auto* pE = m_Vars.Find(key);
			bool bNew = !pE;

			if (pAction)
			{
				key.Export(pAction->m_Key);
				if (pE)
					pAction->m_Value.swap(pE->m_Data);
			}

			if (nVal)
			{
				if (!pE)
					pE = m_Vars.Create(key);

				Blob(pVal, nVal).Export(pE->m_Data);
			}
			else
			{
				if (pE)
					m_Vars.Delete(*pE);
			}

			return !bNew;
		}

		struct AssetData {
			Amount m_Amount;
			PeerID m_Pid;
		};
		typedef std::map<Asset::ID, AssetData> AssetMap;
		AssetMap m_Assets;

		virtual Asset::ID AssetCreate(const Asset::Metadata& md, const PeerID& pid) override
		{
			Asset::ID aid = AssetCreate2(md, pid);
			if (aid)
			{
				struct MyAction
					:public Action
				{
					Asset::ID m_Aid;

					virtual void Undo(MyProcessor& p) override {
						auto it = p.m_Assets.find(m_Aid);
						verify_test(p.m_Assets.end() != it);
						p.m_Assets.erase(it);
					}
				};

				auto pUndo = std::make_unique<MyAction>();
				pUndo->m_Aid = aid;
				m_lstUndo.push_back(*pUndo.release());
			}

			return aid;
		}

		Asset::ID AssetCreate2(const Asset::Metadata&, const PeerID& pid)
		{
			Asset::ID ret = 1;
			while (m_Assets.find(ret) != m_Assets.end())
				ret++;

			auto& val = m_Assets[ret];
			val.m_Amount = 0;
			val.m_Pid = pid;
			
			return ret;
		}

		virtual bool AssetEmit(Asset::ID aid, const PeerID& pid, AmountSigned val) override
		{
			bool bRet = AssetEmit2(aid, pid, val);
			if (bRet)
			{
				struct MyAction
					:public Action
				{
					Asset::ID m_Aid;
					AmountSigned m_Val;

					virtual void Undo(MyProcessor& p) override {
						auto it = p.m_Assets.find(m_Aid);
						verify_test(p.m_Assets.end() != it);
						it->second.m_Amount -= m_Val;;
					}
				};

				auto pUndo = std::make_unique<MyAction>();
				pUndo->m_Aid = aid;
				pUndo->m_Val = val;
				m_lstUndo.push_back(*pUndo.release());
			}

			return bRet;
		}

		bool AssetEmit2(Asset::ID aid, const PeerID& pid, AmountSigned val)
		{
			auto it = m_Assets.find(aid);
			if (m_Assets.end() == it)
				return false;

			auto& x = it->second;
			if (x.m_Pid != pid)
				return false;

			x.m_Amount += val; // don't care about overflow
			return true;
		}

		virtual bool AssetDestroy(Asset::ID aid, const PeerID& pid) override
		{
			bool bRet = AssetDestroy2(aid, pid);
			if (bRet)
			{
				struct MyAction
					:public Action
				{
					Asset::ID m_Aid;
					PeerID m_Pid;

					virtual void Undo(MyProcessor& p) override {
						auto& x = p.m_Assets[m_Aid];
						x.m_Amount = 0;
						x.m_Pid = m_Pid;
					}
				};

				auto pUndo = std::make_unique<MyAction>();
				pUndo->m_Aid = aid;
				pUndo->m_Pid = pid;
				m_lstUndo.push_back(*pUndo.release());
			}

			return bRet;
		}

		bool AssetDestroy2(Asset::ID aid, const PeerID& pid)
		{
			auto it = m_Assets.find(aid);
			if (m_Assets.end() == it)
				return false;

			auto& x = it->second;
			if (x.m_Pid != pid)
				return false;

			if (x.m_Amount)
				return false;

			m_Assets.erase(it);
			return true;
		}

		MyProcessor()
		{
			m_Dbg.m_Stack = true;
			m_Dbg.m_Instructions = true;
			m_Dbg.m_ExtCall = true;
		}

		uint32_t m_Cycles;

		void CallFarN(const ContractID& cid, uint32_t iMethod, void* pArgs, uint32_t nArgs)
		{
			m_Stack.AliasAlloc(nArgs);
			memcpy(m_Stack.get_AliasPtr(), pArgs, nArgs);

			size_t nFrames = m_FarCalls.m_Stack.size();

			Wasm::Word nSp = m_Stack.get_AlasSp();
			CallFar(cid, iMethod, nSp);

			bool bWasm = false;
			for (; m_FarCalls.m_Stack.size() > nFrames; m_Cycles++)
			{
				bWasm = true;
				RunOnce();

				if (m_Dbg.m_pOut)
				{
					std::cout << m_Dbg.m_pOut->str();
					m_Dbg.m_pOut->str("");
				}
			}

			if (bWasm) {
				verify_test(nSp == m_Stack.get_AlasSp()); // stack must be restored
			}
			else {
				// in 'host' mode the stack will not be restored automatically, if ther was a call to StackAlloc
				verify_test(nSp >= m_Stack.get_AlasSp());
				m_Stack.set_AlasSp(nSp);
			}

			memcpy(pArgs, m_Stack.get_AliasPtr(), nArgs);
			m_Stack.AliasFree(nArgs);
		}

		void RunMany(const ContractID& cid, uint32_t iMethod, const Blob& args)
		{
			std::ostringstream os;
			m_Dbg.m_pOut = &os;

			os << "BVM Method: " << cid << ":" << iMethod << std::endl;

			InitStack(0xcd);

			Shaders::Env::g_pEnv = this;
			m_Cycles = 0;

			CallFarN(cid, iMethod, Cast::NotConst(args.p), args.n);

			os << "Done in " << m_Cycles << " cycles" << std::endl << std::endl;
			std::cout << os.str();
		}

		bool RunGuarded(const ContractID& cid, uint32_t iMethod, const Blob& args, const Blob* pCode)
		{
			bool ret = true;
			size_t nChanges = m_lstUndo.size();

			if (!iMethod)
			{
				// c'tor
				assert(pCode);
				get_Cid(Cast::NotConst(cid), *pCode, args); // c'tor is empty
				SaveVar(cid, reinterpret_cast<const uint8_t*>(pCode->p), pCode->n);
			}

			try
			{
				RunMany(cid, iMethod, args);

				if (1 == iMethod) // d'tor
					SaveVar(cid, nullptr, 0);

			}
			catch (const std::exception& e) {
				std::cout << "*** Shader Execution failed. Undoing changes" << std::endl;
				std::cout << e.what() << std::endl;

				UndoChanges(nChanges);
				m_FarCalls.m_Stack.Clear();

				ret = false;
			}

			return ret;
		}

		template <typename T>
		struct Converter
			:public Blob
		{
			Converter(T& arg)
			{
				arg.template Convert<true>();
				p = &arg;
				n = static_cast<uint32_t>(sizeof(arg));
			}

			~Converter()
			{
				T& arg = Cast::NotConst(*reinterpret_cast<const T*>(p));
				arg.template Convert<false>();
			}
		};

		template <typename TArg>
		bool RunGuarded_T(const ContractID& cid, uint32_t iMethod, TArg& args)
		{
			Converter<TArg> cvt(args);
			return RunGuarded(cid, iMethod, cvt, nullptr);
		}

		template <typename T>
		bool ContractCreate_T(ContractID& cid, const Blob& code, T& args) {
			Converter<T> cvt(args);
			return RunGuarded(cid, 0, cvt, &code);
		}

		template <typename T>
		bool ContractDestroy_T(const ContractID& cid, T& args)
		{
			Converter<T> cvt(args);
			return RunGuarded(cid, 1, cvt, nullptr);
		}

		struct Code
		{
			ByteBuffer m_Vault;
			ByteBuffer m_Oracle;
			ByteBuffer m_Dummy;
			ByteBuffer m_StableCoin;

		} m_Code;

		ContractID m_cidVault;
		ContractID m_cidOracle;
		ContractID m_cidStableCoin;

		static void AddCodeEx(ByteBuffer& res, const char* sz, Kind kind)
		{
			std::FStream fs;
			fs.Open(sz, true, true);

			res.resize(static_cast<size_t>(fs.get_Remaining()));
			if (!res.empty())
				fs.read(&res.front(), res.size());

			Processor::Compile(res, res, kind);
		}

		void AddCode(ByteBuffer& res, const char* sz)
		{
			AddCodeEx(res, sz, Kind::Contract);
		}

		template <typename T>
		T& CastArg(Wasm::Word nArg)
		{
			return Cast::NotConst(get_AddrAsR<T>(nArg));
		}

		struct TempFrame
		{
			MyProcessor& m_This;
			FarCalls::Frame m_Frame;

			TempFrame(MyProcessor& x, const ContractID& cid)
				:m_This(x)
			{
				m_Frame.m_Cid = cid;
				m_Frame.m_LocalDepth = 0;
				m_This.m_FarCalls.m_Stack.push_back(m_Frame);
			}

			~TempFrame()
			{
				// don't call pop_back, in case of exc following interpreter frames won't be popped
				m_This.m_FarCalls.m_Stack.erase(intrusive::list<FarCalls::Frame>::s_iterator_to(m_Frame));
			}
		};

		virtual void CallFar(const ContractID& cid, uint32_t iMethod, Wasm::Word pArgs) override
		{
			if (cid == m_cidVault)
			{
				//TempFrame f(*this, cid);
				//switch (iMethod)
				//{
				//case 0: Shaders::Vault::Ctor(nullptr); return;
				//case 1: Shaders::Vault::Dtor(nullptr); return;
				//case 2: Shaders::Vault::Method_2(CastArg<Shaders::Vault::Deposit>(pArgs)); return;
				//case 3: Shaders::Vault::Method_3(CastArg<Shaders::Vault::Withdraw>(pArgs)); return;
				//}
			}

			if (cid == m_cidOracle)
			{
				//TempFrame f(*this, cid);
				//switch (iMethod)
				//{
				//case 0: Shaders::Oracle::Ctor(CastArg<Shaders::Oracle::Create<0> >(pArgs)); return;
				//case 1: Shaders::Oracle::Dtor(nullptr); return;
				//case 2: Shaders::Oracle::Method_2(CastArg<Shaders::Oracle::Set>(pArgs)); return;
				//case 3: Shaders::Oracle::Method_3(CastArg<Shaders::Oracle::Get>(pArgs)); return;
				//}
			}

			if (cid == m_cidStableCoin)
			{
				//TempFrame f(*this, cid);
				//switch (iMethod)
				//{
				//case 0: Shaders::StableCoin::Ctor(CastArg<Shaders::StableCoin::Create<0> >(pArgs)); return;
				//case 1: Shaders::StableCoin::Dtor(nullptr); return;
				//case 2: Shaders::StableCoin::Method_2(CastArg<Shaders::StableCoin::UpdatePosition>(pArgs)); return;
				//case 3: Shaders::StableCoin::Method_3(CastArg<Shaders::StableCoin::PlaceBid>(pArgs)); return;
				//case 4: Shaders::StableCoin::Method_4(CastArg<Shaders::StableCoin::Grab>(pArgs)); return;
				//}
			}

			ProcessorContract::CallFar(cid, iMethod, pArgs);
		}

		void TestVault();
		void TestDummy();
		void TestOracle();
		void TestStableCoin();

		void TestAll();
	};

	template <>
	struct MyProcessor::Converter<beam::Zero_>
		:public Blob
	{
		Converter(beam::Zero_&)
		{
			p = nullptr;
			n = 0;
		}
	};


	void MyProcessor::TestAll()
	{
		AddCode(m_Code.m_Vault, "vault.wasm");
		AddCode(m_Code.m_Dummy, "dummy.wasm");
		AddCode(m_Code.m_Oracle, "oracle.wasm");
		AddCode(m_Code.m_StableCoin, "StableCoin.wasm");

		TestVault();
		TestDummy();
		TestOracle();
		TestStableCoin();
	}

	void MyProcessor::TestVault()
	{
		Zero_ zero;
		verify_test(ContractCreate_T(m_cidVault, m_Code.m_Vault, zero));
		verify_test(m_cidVault == Shaders::Vault::s_CID);

		bvm2::ShaderID sid;
		bvm2::get_ShaderID(sid, m_Code.m_Vault);
		verify_test(sid == Shaders::Vault::s_SID);

		m_lstUndo.Clear();

		Shaders::Vault::Request args;

		ECC::Scalar::Native k;
		ECC::SetRandom(k);
		ECC::Point::Native pt = ECC::Context::get().G * k;

		args.m_Account = pt;
		args.m_Aid = 3;
		args.m_Amount = 45;

		verify_test(RunGuarded_T(m_cidVault, Shaders::Vault::Deposit::s_iMethod, args));

		args.m_Amount = 46;
		verify_test(!RunGuarded_T(m_cidVault, Shaders::Vault::Withdraw::s_iMethod, args)); // too much withdrawn

		args.m_Aid = 0;
		args.m_Amount = 43;
		verify_test(!RunGuarded_T(m_cidVault, Shaders::Vault::Withdraw::s_iMethod, args)); // wrong asset

		args.m_Aid = 3;
		verify_test(RunGuarded_T(m_cidVault, Shaders::Vault::Withdraw::s_iMethod, args)); // ok

		args.m_Amount = 2;
		verify_test(RunGuarded_T(m_cidVault, Shaders::Vault::Withdraw::s_iMethod, args)); // ok, pos terminated

		args.m_Amount = 0xdead2badcadebabeULL;

		verify_test(RunGuarded_T(m_cidVault, Shaders::Vault::Deposit::s_iMethod, args)); // huge amount, should work
		verify_test(!RunGuarded_T(m_cidVault, Shaders::Vault::Deposit::s_iMethod, args)); // would overflow

		UndoChanges(); // up to (but not including) contract creation

		// create several accounts with different assets
		args.m_Amount = 400000;
		args.m_Aid = 0;
		verify_test(RunGuarded_T(m_cidVault, Shaders::Vault::Deposit::s_iMethod, args));

		args.m_Amount = 300000;
		args.m_Aid = 2;
		verify_test(RunGuarded_T(m_cidVault, Shaders::Vault::Deposit::s_iMethod, args));

		pt = pt * ECC::Two;
		args.m_Account = pt;

		args.m_Amount = 700000;
		args.m_Aid = 0;
		verify_test(RunGuarded_T(m_cidVault, Shaders::Vault::Deposit::s_iMethod, args));

		args.m_Amount = 500000;
		args.m_Aid = 6;
		verify_test(RunGuarded_T(m_cidVault, Shaders::Vault::Deposit::s_iMethod, args));

		m_lstUndo.Clear();
	}

	void MyProcessor::TestDummy()
	{
		ContractID cid;
		Zero_ zero;
		verify_test(ContractCreate_T(cid, m_Code.m_Dummy, zero));

		Shaders::Dummy::MathTest1 args;
		args.m_Value = 0x1452310AB046C124;
		args.m_Rate = 0x0000010100000000;
		args.m_Factor = 0x0000000000F00000;
		args.m_Try = 0x1452310AB046C100;

		args.m_IsOk = 0;

		verify_test(RunGuarded_T(cid, args.s_iMethod, args));

		verify_test(ContractDestroy_T(cid, zero));
	}

	//template <typename T, uint32_t nSizeExtra> struct Inflated
	//{
	//	alignas (16) uint8_t m_pBuf[sizeof(T) + nSizeExtra];
	//	T& get() { return *reinterpret_cast<T*>(m_pBuf); }
	//};

	void MyProcessor::TestOracle()
	{
		Dbg dbg = m_Dbg;
		m_Dbg.m_Instructions = false;
		m_Dbg.m_Stack = false;

		typedef Shaders::Oracle::ValueType ValueType;

		constexpr uint32_t nOracles = 15;

		struct ProvData
		{
			struct Entry {
				uint32_t m_iProv2Pos;
				uint32_t m_iProv;
				ValueType m_Value;

				bool operator < (const Entry& x) const {
					return m_Value < x.m_Value;
				}
			};

			Entry m_pData[nOracles];

			ProvData()
			{
				for (uint32_t i = 0; i < nOracles; i++)
				{
					m_pData[i].m_iProv = i;
					m_pData[i].m_iProv2Pos = i;
				}
			}

			void Set(uint32_t i, ValueType val)
			{
				m_pData[m_pData[i].m_iProv2Pos].m_Value = val;
			}

			void Sort()
			{
				Entry pTmp[nOracles];
				auto* pRes = Shaders::MergeSort<Entry>::Do(m_pData, pTmp, nOracles);
				if (m_pData != pRes)
					memcpy(m_pData, pTmp, sizeof(pTmp));

				for (uint32_t i = 0; i < nOracles; i++)
					m_pData[m_pData[i].m_iProv].m_iProv2Pos = i;
			}

			void TestMedian(ValueType val)
			{
				ValueType val1 = m_pData[nOracles / 2].m_Value;
				verify_test(val == val1);
			}
		};

		ProvData pd;

		{
			// c'tor
			Shaders::Oracle::Create<nOracles> args;

			args.m_InitialValue = 194;

			for (uint32_t i = 0; i < nOracles; i++)
			{
				ECC::Scalar::Native k;
				ECC::SetRandom(k);

				ECC::Point::Native pt = ECC::Context::get().G * k;
				args.m_pPk[i] = pt;

				pd.Set(i, args.m_InitialValue);
			}

			args.m_Providers = 0;
			verify_test(!ContractCreate_T(m_cidOracle, m_Code.m_Oracle, args)); // zero providers not allowed

			args.m_Providers = nOracles;
			verify_test(ContractCreate_T(m_cidOracle, m_Code.m_Oracle, args));
		}

		Shaders::Oracle::Get argsResult;
		argsResult.m_Value = 0;
		verify_test(RunGuarded_T(m_cidOracle, argsResult.s_iMethod, argsResult));
		pd.TestMedian(argsResult.m_Value);

		// set rate, trigger median recalculation
		for (uint32_t i = 0; i < nOracles * 10; i++)
		{
			uint32_t iOracle = i % nOracles;

			Shaders::Oracle::Set args;
			args.m_iProvider = iOracle;

			ECC::GenRandom(&args.m_Value, sizeof(args.m_Value));
			pd.Set(iOracle, args.m_Value);

			verify_test(RunGuarded_T(m_cidOracle, args.s_iMethod, args));

			pd.Sort();

			argsResult.m_Value = 0;
			verify_test(RunGuarded_T(m_cidOracle, argsResult.s_iMethod, argsResult));
			pd.TestMedian(argsResult.m_Value);
		}

		Zero_ zero;
		verify_test(ContractDestroy_T(m_cidOracle, zero));

		m_Dbg = dbg;
	}

	static uint64_t RateFromFraction(uint32_t nom, uint32_t denom)
	{
		// simple impl, don't care about overflow, etc.
		assert(denom);
		uint64_t res = ((uint64_t)1) << 32;
		res *= nom;
		res /= denom;
		return res;
	}

	static uint64_t RateFromPercents(uint32_t val) {
		return RateFromFraction(val, 100);
	}

	void MyProcessor::TestStableCoin()
	{
		static const char szMyMeta[] = "cool metadata for my stable coin";

		{
			Shaders::Oracle::Create<1> args;

			args.m_InitialValue = RateFromPercents(36); // current ratio: 1 beam == 0.36 stablecoin
			args.m_Providers = 1;
			ZeroObject(args.m_pPk[0]);
			verify_test(ContractCreate_T(m_cidOracle, m_Code.m_Oracle, args));
		}

		Shaders::StableCoin::Create<sizeof(szMyMeta) - 1> argSc;
		argSc.m_RateOracle = m_cidOracle;
		argSc.m_nMetaData = sizeof(szMyMeta) - 1;
		memcpy(argSc.m_pMetaData, szMyMeta, argSc.m_nMetaData);
		argSc.m_CollateralizationRatio = RateFromPercents(150);

		verify_test(ContractCreate_T(m_cidStableCoin, m_Code.m_StableCoin, argSc));

		Shaders::StableCoin::UpdatePosition argUpd;
		argUpd.m_Change.m_Beam = 1000;
		argUpd.m_Change.m_Asset = 241;
		argUpd.m_Direction.m_BeamAdd = 1;
		argUpd.m_Direction.m_AssetAdd = 0;
		ZeroObject(argUpd.m_Pk);

		verify_test(!RunGuarded_T(m_cidStableCoin, argUpd.s_iMethod, argUpd)); // will fail, not enough collateral

		argUpd.m_Change.m_Asset = 239;
		verify_test(RunGuarded_T(m_cidStableCoin, argUpd.s_iMethod, argUpd)); // should work

		Zero_ zero;
		verify_test(!ContractDestroy_T(m_cidStableCoin, zero)); // asset was not fully burned

		verify_test(ContractDestroy_T(argSc.m_RateOracle, zero));
	}



	struct MyManager
		:public ProcessorManager
	{
		BlobMap::Set& m_Vars;
		std::ostringstream m_Out;

		MyManager(BlobMap::Set& vars)
			:m_Vars(vars)
		{
			m_pOut = &m_Out;
		}

		struct VarEnumCtx
		{
			ByteBuffer m_kMax;
			BlobMap::Set::iterator m_it;
		};

		std::unique_ptr<VarEnumCtx> m_pVarEnum;

		virtual void VarsEnum(const Blob& kMin, const Blob& kMax) override
		{
			if (!m_pVarEnum)
				m_pVarEnum = std::make_unique<VarEnumCtx>();

			m_pVarEnum->m_it = m_Vars.lower_bound(kMin, BlobMap::Set::Comparator());
			kMax.Export(m_pVarEnum->m_kMax);
		}

		virtual bool VarsMoveNext(Blob& key, Blob& val) override
		{
			assert(m_pVarEnum);
			auto& ctx = *m_pVarEnum;
			const auto& x = *ctx.m_it;

			key = x.ToBlob();
			if (key > ctx.m_kMax)
			{
				m_pVarEnum.reset();
				return false;
			}

			val = x.m_Data;

			ctx.m_it++;
			return true;
		}

		void TestHeap()
		{
			uint32_t p1, p2, p3;
			verify_test(m_Heap.Alloc(p1, 160));
			verify_test(m_Heap.Alloc(p2, 300));
			verify_test(m_Heap.Alloc(p3, 28));

			m_Heap.Free(p2);
			verify_test(m_Heap.Alloc(p2, 260));
			m_Heap.Free(p2);
			verify_test(m_Heap.Alloc(p2, 360));

			m_Heap.Free(p1);
			m_Heap.Free(p3);
			m_Heap.Free(p2);
		}

		uint32_t m_Cycles;

		void RunMany(uint32_t iMethod)
		{
			std::ostringstream os;
			m_Dbg.m_pOut = &os;

			os << "BVM Method: " << iMethod << std::endl;

			Shaders::Env::g_pEnv = this;

			m_Cycles = 0;
			uint32_t nDepth = m_LocalDepth;

			for (CallMethod(iMethod); m_LocalDepth != nDepth; )
			{
				RunOnce();

				if (m_Dbg.m_pOut)
				{
					std::cout << m_Dbg.m_pOut->str();
					m_Dbg.m_pOut->str("");
				}
			}


			os << "Done in " << m_Cycles << " cycles" << std::endl << std::endl;
			std::cout << os.str();
		}

		bool RunGuarded(uint32_t iMethod)
		{
			bool ret = true;
			try
			{
				RunMany(iMethod);
			}
			catch (const std::exception& e) {
				std::cout << "*** Shader Execution failed. Undoing changes" << std::endl;
				std::cout << e.what() << std::endl;
				ret = false;
			}
			return ret;
		}
	};


} // namespace bvm2
} // namespace beam

void Shaders::Env::CallFarN(const ContractID& cid, uint32_t iMethod, void* pArgs, uint32_t nArgs)
{
	Cast::Up<beam::bvm2::MyProcessor>(g_pEnv)->CallFarN(cid, iMethod, pArgs, nArgs);
}

int main()
{
	try
	{
		ECC::PseudoRandomGenerator prg;
		ECC::PseudoRandomGenerator::Scope scope(&prg);

		using namespace beam;
		using namespace beam::bvm2;

		TestMergeSort();

		MyProcessor proc;
		proc.TestAll();

		MyManager man(proc.m_Vars);
		man.InitMem();
		man.TestHeap();

		ByteBuffer buf;
		MyProcessor::AddCodeEx(buf, "vaultManager.wasm", Processor::Kind::Manager);
		man.m_Code = buf;

		man.RunGuarded(0); // get scheme
		std::cout << man.m_Out.str();
		man.m_Out.str("");

		man.m_Args["role"] = "all_accounts";
		man.m_Args["action"] = "view";
		man.set_ArgBlob("cid", Shaders::Vault::s_CID);

		man.RunGuarded(1);
		std::cout << man.m_Out.str();
		man.m_Out.str("");

	}
	catch (const std::exception & ex)
	{
		printf("Expression: %s\n", ex.what());
		g_TestsFailed++;
	}

	return g_TestsFailed ? -1 : 0;
}
