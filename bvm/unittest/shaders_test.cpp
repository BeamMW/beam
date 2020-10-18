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

//#include "../node.h"

#include "../../core/block_rw.h"
#include "../../utility/test_helpers.h"
#include "../../utility/byteorder.h"
#include "../../utility/blobmap.h"
#include "../bvm2.h"

#include <sstream>

namespace Shaders {
	typedef ECC::Point PubKey;
	typedef beam::Asset::ID AssetID;
	using beam::Amount;
	using beam::Height;
	using beam::bvm2::ContractID;

#ifdef _MSC_VER
#	pragma warning (disable : 4200) // zero-sized array
#endif // _MSC_VER
#include "../Shaders/vault.h"
#include "../Shaders/oracle.h"
#include "../Shaders/dummy.h"
#include "../Shaders/StableCoin.h"
#include "../Shaders/MergeSort.h"
#ifdef _MSC_VER
#	pragma warning (default : 4200)
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
		:public Processor
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

		uint32_t RunMany(const ContractID& cid, uint32_t iMethod, const Blob& args)
		{
			std::ostringstream os;
			m_Dbg.m_pOut = &os;

			os << "BVM Method: " << cid << ":" << iMethod << std::endl;

			InitStack(args, 0xcd);

			Wasm::Word pArgs = m_Stack.get_AlasSp();

			CallFar(cid, iMethod, pArgs);

			uint32_t nCycles = 0;
			for (; !IsDone(); nCycles++)
			{
				RunOnce();

				std::cout << os.str();
				os.str("");
			}

			os << "Done in " << nCycles << " cycles" << std::endl << std::endl;
			std::cout << os.str();

			verify_test(pArgs == m_Stack.get_AlasSp()); // stack must be restored

			// copy retval (for test only)
			memcpy(Cast::NotConst(args.p), reinterpret_cast<uint8_t*>(m_Stack.m_pPtr) + m_Stack.m_BytesCurrent, args.n);

			return nCycles;
		}

		uint32_t RunGuarded(const ContractID& cid, uint32_t iMethod, const Blob& args)
		{
			uint32_t ret = 0;
			size_t nChanges = m_lstUndo.size();
			try
			{
				ret = RunMany(cid, iMethod, args);
			}
			catch (const std::exception&) {
				std::cout << "*** Shader Execution failed. Undoing changes" << std::endl;
				UndoChanges(nChanges);
				m_FarCalls.m_Stack.Clear();

				ret = 0;
			}

			return ret;
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

		void AddCode(ByteBuffer& res, const char* sz)
		{
			std::FStream fs;
			fs.Open(sz, true, true);

			res.resize(static_cast<size_t>(fs.get_Remaining()));
			if (!res.empty())
				fs.read(&res.front(), res.size());

			Processor::Compile(res, res);
		}

		uint32_t ContractCreate(ContractID&, const Blob& code, const Blob& args);
		uint32_t ContractDestroy(const ContractID&, const Blob& args);

		void TestVault();
		void TestDummy();
		void TestOracle();
		void TestStableCoin();

		void TestAll();
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

	uint32_t MyProcessor::ContractCreate(ContractID& cid, const Blob& code, const Blob& args)
	{
		size_t nChanges = m_lstUndo.size();

		get_Cid(cid, code, args); // c'tor is empty
		SaveVar(cid, reinterpret_cast<const uint8_t*>(code.p), code.n);

		uint32_t ret = RunGuarded(cid, 0, args);
		if (!ret)
			UndoChanges(nChanges);

		return ret;
	}

	uint32_t MyProcessor::ContractDestroy(const ContractID& cid, const Blob& args)
	{
		uint32_t ret = RunGuarded(cid, 1, args);
		if (ret)
			SaveVar(cid, nullptr, 0);

		return ret;
	}

	void MyProcessor::TestVault()
	{
		verify_test(ContractCreate(m_cidVault, m_Code.m_Vault, Blob(nullptr, 0)));

		m_lstUndo.Clear();

		Shaders::Vault::Request args;

		ECC::Scalar::Native k;
		ECC::SetRandom(k);
		ECC::Point::Native pt = ECC::Context::get().G * k;

		args.m_Account = pt;
		args.m_Aid = ByteOrder::to_le<Asset::ID>(3);
		args.m_Amount = ByteOrder::to_le<Amount>(45);

		verify_test(RunGuarded(m_cidVault, Shaders::Vault::Deposit::s_iMethod, Blob(&args, sizeof(args))));

		args.m_Amount = ByteOrder::to_le<Amount>(46);
		verify_test(!RunGuarded(m_cidVault, Shaders::Vault::Withdraw::s_iMethod, Blob(&args, sizeof(args)))); // too much withdrawn

		args.m_Aid = 0;
		args.m_Amount = ByteOrder::to_le<Amount>(43);
		verify_test(!RunGuarded(m_cidVault, Shaders::Vault::Withdraw::s_iMethod, Blob(&args, sizeof(args)))); // wrong asset

		args.m_Aid = ByteOrder::to_le<Asset::ID>(3);
		verify_test(RunGuarded(m_cidVault, Shaders::Vault::Withdraw::s_iMethod, Blob(&args, sizeof(args)))); // ok

		args.m_Amount = ByteOrder::to_le<Amount>(2);
		verify_test(RunGuarded(m_cidVault, Shaders::Vault::Withdraw::s_iMethod, Blob(&args, sizeof(args)))); // ok, pos terminated

		args.m_Amount = ByteOrder::to_le<Amount>(0xdead2badcadebabeULL);

		verify_test(RunGuarded(m_cidVault, Shaders::Vault::Deposit::s_iMethod, Blob(&args, sizeof(args)))); // huge amount, should work
		verify_test(!RunGuarded(m_cidVault, Shaders::Vault::Deposit::s_iMethod, Blob(&args, sizeof(args)))); // would overflow

		UndoChanges(); // up to (but not including) contract creation
	}

	void MyProcessor::TestDummy()
	{
		ContractID cid;
		verify_test(ContractCreate(cid, m_Code.m_Dummy, Blob(nullptr, 0)));

		Shaders::Dummy::MathTest1 args;
		args.m_Value = 0x1452310AB046C124;
		args.m_Rate = 0x0000010100000000;
		args.m_Factor = 0x0000000000F00000;
		args.m_Try = 0x1452310AB046C100;

		args.m_IsOk = 0;

		verify_test(RunGuarded(cid, args.s_iMethod, Blob(&args, sizeof(args))));

		verify_test(ContractDestroy(cid, Blob(nullptr, 0)));
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
				verify_test(val == ByteOrder::to_le(val1));
			}
		};

		ProvData pd;

		{
			// c'tor
			Shaders::Oracle::Create<nOracles> args;

			args.m_InitialValue = ByteOrder::to_le<ValueType>(194);

			for (uint32_t i = 0; i < nOracles; i++)
			{
				ECC::Scalar::Native k;
				ECC::SetRandom(k);

				ECC::Point::Native pt = ECC::Context::get().G * k;
				args.m_pPk[i] = pt;

				pd.Set(i, args.m_InitialValue);
			}

			args.m_Providers = 0;
			verify_test(!ContractCreate(m_cidOracle, m_Code.m_Oracle, Blob(&args, sizeof(args)))); // zero providers not allowed

			args.m_Providers = ByteOrder::to_le(nOracles);
			verify_test(ContractCreate(m_cidOracle, m_Code.m_Oracle, Blob(&args, sizeof(args))));
		}

		Shaders::Oracle::Get argsResult;
		argsResult.m_Value = 0;
		verify_test(RunGuarded(m_cidOracle, argsResult.s_iMethod, Blob(&argsResult, sizeof(argsResult))));
		pd.TestMedian(argsResult.m_Value);

		// set rate, trigger median recalculation
		for (uint32_t i = 0; i < nOracles * 10; i++)
		{
			uint32_t iOracle = i % nOracles;

			Shaders::Oracle::Set args;
			args.m_iProvider = ByteOrder::to_le<uint32_t>(iOracle);

			ECC::GenRandom(&args.m_Value, sizeof(args.m_Value));
			pd.Set(iOracle, ByteOrder::from_le(args.m_Value));

			verify_test(RunGuarded(m_cidOracle, args.s_iMethod, Blob(&args, sizeof(args))));

			pd.Sort();

			argsResult.m_Value = 0;
			verify_test(RunGuarded(m_cidOracle, argsResult.s_iMethod, Blob(&argsResult, sizeof(argsResult))));
			pd.TestMedian(argsResult.m_Value);
		}

		verify_test(ContractDestroy(m_cidOracle, Blob(nullptr, 0)));

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

		Shaders::StableCoin::Ctor<sizeof(szMyMeta)-1> argSc;

		{
			Shaders::Oracle::Create<1> args;

			args.m_InitialValue = ByteOrder::to_le<uint64_t>(RateFromPercents(36)); // current ratio: 1 beam == 0.36 stablecoin
			args.m_Providers = ByteOrder::to_le(1U);
			ZeroObject(args.m_pPk[0]);
			verify_test(ContractCreate(argSc.m_RateOracle, m_Code.m_Oracle, Blob(&args, sizeof(args))));
		}

		argSc.m_nMetaData = sizeof(szMyMeta) - 1;
		memcpy(argSc.m_pMetaData, szMyMeta, argSc.m_nMetaData);
		argSc.m_CollateralizationRatio = ByteOrder::to_le<uint64_t>(RateFromPercents(150));

		ContractID cidSc;
		verify_test(ContractCreate(cidSc, m_Code.m_StableCoin, Blob(&argSc, sizeof(argSc))));

		Shaders::StableCoin::UpdatePosition argUpd;
		argUpd.m_Change.m_Beam = ByteOrder::to_le<Amount>(1000);
		argUpd.m_Change.m_Asset = ByteOrder::to_le<Amount>(241);
		argUpd.m_Direction.m_BeamAdd = 1;
		argUpd.m_Direction.m_AssetAdd = 0;
		ZeroObject(argUpd.m_Pk);

		verify_test(!RunGuarded(cidSc, argUpd.s_iMethod, Blob(&argUpd, sizeof(argUpd)))); // will fail, not enough collateral

		argUpd.m_Change.m_Asset = ByteOrder::to_le<Amount>(239);
		verify_test(RunGuarded(cidSc, argUpd.s_iMethod, Blob(&argUpd, sizeof(argUpd)))); // should work

		verify_test(!ContractDestroy(cidSc, Blob(nullptr, 0))); // asset was not fully burned

		verify_test(ContractDestroy(argSc.m_RateOracle, Blob(nullptr, 0)));
	}


} // namespace bvm2
} // namespace beam

int main()
{
	try
	{
		ECC::PseudoRandomGenerator prg;
		ECC::PseudoRandomGenerator::Scope scope(&prg);

		using namespace beam::bvm2;

		TestMergeSort();

		MyProcessor proc;
		proc.TestAll();
	}
	catch (const std::exception & ex)
	{
		printf("Expression: %s\n", ex.what());
		g_TestsFailed++;
	}

	return g_TestsFailed ? -1 : 0;
}
