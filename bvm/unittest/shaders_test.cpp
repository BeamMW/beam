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

namespace beam
{

	namespace bvm2
	{
		void Compile(ByteBuffer& res, const char* sz)
		{
			std::FStream fs;
			fs.Open(sz, true, true);

			res.resize(static_cast<size_t>(fs.get_Remaining()));
			if (!res.empty())
				fs.read(&res.front(), res.size());

			bvm2::Processor::Compile(res, res);
		}

	}


	struct MyBvm2Processor
		:public bvm2::Processor
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

		void SaveContract(const bvm2::ContractID& cid, const ByteBuffer& b)
		{
			SaveVar(cid, &b.front(), static_cast<uint32_t>(b.size()));
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

		MyBvm2Processor()
		{
			m_Dbg.m_Stack = true;
			m_Dbg.m_Instructions = true;
			m_Dbg.m_ExtCall = true;
		}

		uint32_t RunMany(const bvm2::ContractID& cid, uint32_t iMethod, const Blob& args)
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

        static const uint32_t s_ElemWidth = 5;

        static void CalcXors(uint8_t* pDst, const uint8_t* pSrc, uint32_t nSize)
        {
            for (uint32_t i = 0; i < nSize; i++)
                pDst[i % s_ElemWidth] ^= pSrc[i];
        }

        static void TestSort()
        {
            ECC::PseudoRandomGenerator prg;


            ArrayContext ac;
            ac.m_nKeyPos = 1;
            ac.m_nKeyWidth = 2;
            ac.m_nElementWidth = s_ElemWidth;

            ByteBuffer buf;

            uint8_t pXor0[s_ElemWidth];
            memset0(pXor0, s_ElemWidth);

            for (ac.m_nCount = 1; ac.m_nCount < 500; ac.m_nCount++)
            {
				ac.m_nSize = ac.m_nCount * ac.m_nElementWidth;
                buf.resize(ac.m_nSize);
                uint8_t* p = &buf.front();

                for (uint32_t n = 0; n < 10; n++)
                {
                    prg.Generate(p, ac.m_nSize);

                    CalcXors(pXor0, p, ac.m_nSize);

                    ac.MergeSort(p);

                    CalcXors(pXor0, p, ac.m_nSize);
                    verify_test(memis0(pXor0, s_ElemWidth));

                    uint8_t* pK = p + ac.m_nKeyPos;

                    for (uint32_t i = 0; i + 1 < ac.m_nCount; i++)
                    {
                        uint8_t* pK0 = pK;
                        pK += ac.m_nElementWidth;

                        verify_test(memcmp(pK0, pK, ac.m_nKeyWidth) <= 0);
                    }
                }

            }
        }

		static void TestSort2()
		{
			ECC::PseudoRandomGenerator prg;

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
					prg.Generate(p + 1, sizeof(uint64_t) * nCount);

					uint64_t* pRes = Shaders::MergeSort<uint64_t>::Do(p + 1, &buf2.front() + 1, nCount);

					verify_test(1 == p[0]);
					verify_test(2 == p[nCount + 1]);
					verify_test(3 == buf2[0]);
					verify_test(4 == buf2[nCount + 1]);

					for (uint32_t i = 0; i + 1 < nCount; i++)
						verify_test(pRes[i] <= pRes[i +  1]);

				}

			}
		}

	};


	void TestContract1()
	{
		ByteBuffer data;
		bvm2::Compile(data, "vault.wasm");

		MyBvm2Processor proc;

		bvm2::ContractID cid;

		bvm2::get_Cid(cid, data, Blob(nullptr, 0)); // c'tor is empty
		proc.SaveContract(cid, data);

		Shaders::Vault::Request args;

		ECC::Scalar::Native k;
		ECC::SetRandom(k);
		ECC::Point::Native pt = ECC::Context::get().G * k;

		args.m_Account = pt;
		args.m_Aid = ByteOrder::to_le<Asset::ID>(3);
		args.m_Amount = ByteOrder::to_le<Amount>(45);

		proc.RunMany(cid, Shaders::Vault::Deposit::s_iMethod, Blob(&args, sizeof(args)));

		args.m_Amount = ByteOrder::to_le<Amount>(43);
		proc.RunMany(cid, Shaders::Vault::Withdraw::s_iMethod, Blob(&args, sizeof(args)));

		args.m_Amount = ByteOrder::to_le<Amount>(2);
		proc.RunMany(cid, Shaders::Vault::Withdraw::s_iMethod, Blob(&args, sizeof(args))); // withdraw, pos terminated
	}

	void TestContract2()
	{
		ByteBuffer data;
		bvm2::Compile(data, "vault.wasm");

		MyBvm2Processor proc;

		bvm2::ContractID cid;

		bvm2::get_Cid(cid, data, Blob(nullptr, 0)); // c'tor is empty
		proc.SaveContract(cid, data);


		bvm2::Compile(data, "dummy.wasm");
		bvm2::get_Cid(cid, data, Blob(nullptr, 0)); // c'tor is empty
		proc.SaveContract(cid, data);

		proc.RunMany(cid, 0, Blob(nullptr, 0)); // c'tor

		Shaders::Dummy::MathTest1 args;
		args.m_Value = 0x1452310AB046C124;
		args.m_Rate = 0x0000010100000000;
		args.m_Factor = 0x0000000000F00000;
		args.m_Try = 0x1452310AB046C100;

		args.m_IsOk = 0;

		proc.RunMany(cid, args.s_iMethod, Blob(&args, sizeof(args)));


		proc.RunMany(cid, 1, Blob(nullptr, 0)); // d'tor
	}

	void TestContract3()
	{
		ByteBuffer data;
		bvm2::Compile(data, "oracle.wasm");

		MyBvm2Processor proc;
		bvm2::ContractID cid;

		proc.m_Dbg.m_Instructions = false;
		proc.m_Dbg.m_Stack = false;

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
			ByteBuffer buf;
			size_t nSizePks = sizeof(ECC::Point) * nOracles;

			buf.resize(sizeof(Shaders::Oracle::Create) + nSizePks);
			auto& args = *reinterpret_cast<Shaders::Oracle::Create*>(&buf.front());

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

			bvm2::get_Cid(cid, data, Blob(&args, sizeof(args)));
			proc.SaveContract(cid, data);

			proc.RunMany(cid, 0, buf);
		}

		Shaders::Oracle::Get argsResult;
		argsResult.m_Value = 0;
		proc.RunMany(cid, argsResult.s_iMethod, Blob(&argsResult, sizeof(argsResult)));
		pd.TestMedian(argsResult.m_Value);

		// set rate, trigger median recalculation
		for (uint32_t i = 0; i < nOracles * 10; i++)
		{
			uint32_t iOracle = i % nOracles;

			Shaders::Oracle::Set args;
			args.m_iProvider = ByteOrder::to_le<uint32_t>(iOracle);

			ECC::GenRandom(&args.m_Value, sizeof(args.m_Value));
			pd.Set(iOracle, ByteOrder::from_le(args.m_Value));

			proc.RunMany(cid, args.s_iMethod, Blob(&args, sizeof(args)));

			pd.Sort();

			argsResult.m_Value = 0;
			proc.RunMany(cid, argsResult.s_iMethod, Blob(&argsResult, sizeof(argsResult)));
			pd.TestMedian(argsResult.m_Value);
		}

		// d'tor
		proc.RunMany(cid, 1, Blob(nullptr, 0));
	}

}

int main()
{
	try
	{
		beam::MyBvm2Processor::TestSort();
		beam::MyBvm2Processor::TestSort2();

		beam::TestContract1();
		beam::TestContract2();
		beam::TestContract3();
	}
	catch (const std::exception & ex)
	{
		printf("Expression: %s\n", ex.what());
		g_TestsFailed++;
	}

	return g_TestsFailed ? -1 : 0;
}
