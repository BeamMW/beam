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
	typedef beam::Amount Amount;
	typedef beam::bvm2::ContractID ContractID;

#ifdef _MSC_VER
#	pragma warning (disable : 4200) // zero-sized array
#endif // _MSC_VER
#include "../Shaders/vault.h"
#include "../Shaders/oracle.h"
#include "../Shaders/dummy.h"
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

		bool SaveVar(const Blob& key, const uint8_t* pVal, uint32_t nVal)
		{
			auto* pE = m_Vars.Find(key);
			bool bNew = !pE;

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

		virtual Asset::ID AssetCreate(const Asset::Metadata&, const PeerID&) override
		{
			return 100;
		}

		virtual bool AssetEmit(Asset::ID, const PeerID&, AmountSigned) override
		{
			return true;
		}

		virtual bool AssetDestroy(Asset::ID, const PeerID&) override
		{
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

		struct Code
		{
			ByteBuffer m_Vault;
			ByteBuffer m_Oracle;
			ByteBuffer m_Dummy;

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

		void ContractCreate(ContractID&, const Blob& code, const Blob& args);
		void ContractDestroy(const ContractID&, const Blob& args);

		void TestVault();
		void TestDummy();
		void TestOracle();

		void TestAll();
	};

	void MyProcessor::TestAll()
	{
		AddCode(m_Code.m_Vault, "vault.wasm");
		AddCode(m_Code.m_Dummy, "dummy.wasm");
		AddCode(m_Code.m_Oracle, "oracle.wasm");

		TestVault();
		TestDummy();
		TestOracle();
	}

	void MyProcessor::ContractCreate(ContractID& cid, const Blob& code, const Blob& args)
	{
		get_Cid(cid, code, args); // c'tor is empty
		SaveVar(cid, reinterpret_cast<const uint8_t*>(code.p), code.n);

		RunMany(cid, 0, args);
	}

	void MyProcessor::ContractDestroy(const ContractID& cid, const Blob& args)
	{
		RunMany(cid, 1, args);
		SaveVar(cid, nullptr, 0);
	}

	void MyProcessor::TestVault()
	{
		ContractCreate(m_cidVault, m_Code.m_Vault, Blob(nullptr, 0));

		Shaders::Vault::Request args;

		ECC::Scalar::Native k;
		ECC::SetRandom(k);
		ECC::Point::Native pt = ECC::Context::get().G * k;

		args.m_Account = pt;
		args.m_Aid = ByteOrder::to_le<Asset::ID>(3);
		args.m_Amount = ByteOrder::to_le<Amount>(45);

		RunMany(m_cidVault, Shaders::Vault::Deposit::s_iMethod, Blob(&args, sizeof(args)));

		args.m_Amount = ByteOrder::to_le<Amount>(43);
		RunMany(m_cidVault, Shaders::Vault::Withdraw::s_iMethod, Blob(&args, sizeof(args)));

		args.m_Amount = ByteOrder::to_le<Amount>(2);
		RunMany(m_cidVault, Shaders::Vault::Withdraw::s_iMethod, Blob(&args, sizeof(args))); // withdraw, pos terminated
	}

	void MyProcessor::TestDummy()
	{
		ContractID cid;
		ContractCreate(cid, m_Code.m_Dummy, Blob(nullptr, 0));

		Shaders::Dummy::MathTest1 args;
		args.m_Value = 0x1452310AB046C124;
		args.m_Rate = 0x0000010100000000;
		args.m_Factor = 0x0000000000F00000;
		args.m_Try = 0x1452310AB046C100;

		args.m_IsOk = 0;

		RunMany(cid, args.s_iMethod, Blob(&args, sizeof(args)));

		ContractDestroy(cid, Blob(nullptr, 0));
	}

	template <typename T, uint32_t nSizeExtra> struct Inflated
	{
		alignas (16) uint8_t m_pBuf[sizeof(T) + nSizeExtra];
		T& get() { return *reinterpret_cast<T*>(m_pBuf); }
	};

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
			Inflated<Shaders::Oracle::Create, sizeof(ECC::Point)* nOracles> buf;
			auto& args = buf.get();

			args.m_InitialValue = ByteOrder::to_le<ValueType>(194);
			args.m_Providers = ByteOrder::to_le(nOracles);

			for (uint32_t i = 0; i < nOracles; i++)
			{
				ECC::Scalar::Native k;
				ECC::SetRandom(k);

				ECC::Point::Native pt = ECC::Context::get().G * k;
				args.m_pPk[i] = pt;

				pd.Set(i, args.m_InitialValue);
			}

			ContractCreate(m_cidOracle, m_Code.m_Oracle, Blob(&buf, sizeof(buf)));
		}

		Shaders::Oracle::Get argsResult;
		argsResult.m_Value = 0;
		RunMany(m_cidOracle, argsResult.s_iMethod, Blob(&argsResult, sizeof(argsResult)));
		pd.TestMedian(argsResult.m_Value);

		// set rate, trigger median recalculation
		for (uint32_t i = 0; i < nOracles * 10; i++)
		{
			uint32_t iOracle = i % nOracles;

			Shaders::Oracle::Set args;
			args.m_iProvider = ByteOrder::to_le<uint32_t>(iOracle);

			ECC::GenRandom(&args.m_Value, sizeof(args.m_Value));
			pd.Set(iOracle, ByteOrder::from_le(args.m_Value));

			RunMany(m_cidOracle, args.s_iMethod, Blob(&args, sizeof(args)));

			pd.Sort();

			argsResult.m_Value = 0;
			RunMany(m_cidOracle, argsResult.s_iMethod, Blob(&argsResult, sizeof(argsResult)));
			pd.TestMedian(argsResult.m_Value);
		}

		ContractDestroy(m_cidOracle, Blob(nullptr, 0));

		m_Dbg = dbg;
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
