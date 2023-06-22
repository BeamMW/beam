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

#include "../../core/block_crypt.h"
#include "../../utility/test_helpers.h"
#include "../evm.h"

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

	struct MyProcessor
		:public EvmProcessor
	{
		std::map<Word, Word> m_Storage;

		ByteBuffer m_CodeBuf;

		void SStore(const Word& key, const Word& w) override
		{
			if (w == Zero)
			{
				auto it = m_Storage.find(key);
				if (m_Storage.end() != it)
					m_Storage.erase(it);
			}
			else
				m_Storage[key] = w;
		}

		bool SLoad(const Word& key, Word& w) override
		{
			auto it = m_Storage.find(key);
			if (m_Storage.end() == it)
				return false;

			w = it->second;
			return true;
		}

		bool RunFull(const Word& wCaller, const Blob& args)
		{
			Reset();

			if (!m_CodeBuf.empty())
			{
				m_Code.m_p = &m_CodeBuf.front();
				m_Code.m_n = (uint32_t) m_CodeBuf.size();
			}

			m_Gas = 1000000000ULL;
			m_Args.m_Caller = wCaller;
			m_Args.m_Buf = args;

			while (ShouldRun())
				RunOnce();

			return State::Done == m_State;
		}

		bool Construct(const Word& wCaller, const char* szCode, uint32_t nLenCode, const void* pArg, uint32_t nArg)
		{
			Reset();

			uint32_t nSizeCode = nLenCode / 2;
			m_CodeBuf.resize(nSizeCode + nArg);
			if (nSizeCode)
			{
				auto ret = uintBigImpl::_Scan(&m_CodeBuf.front(), szCode, nLenCode);
				verify_test(ret == nLenCode);
				memcpy(&m_CodeBuf.front() + nSizeCode, pArg, nArg);
			}

			if (!RunFull(wCaller, Blob()))
				return false;

			// save retval as the remaining code, rerun
			m_RetVal.Export(m_CodeBuf);

			return true;
		}

		bool RunMethod(const Word& wCaller, const EvmProcessor::Method& m, uint32_t nSizeMethod)
		{
			return RunFull(wCaller, Blob(&m, nSizeMethod));

		}

		template <typename TMethod>
		bool RunMethod_T(const Word& wCaller, const TMethod& m)
		{
			return RunMethod(wCaller, m, sizeof(m));
		}

	};


	void EvmTest1()
	{
		// storage example

/*
		pragma solidity >=0.7.0 <0.9.0;

		contract Storage {

			uint256 m_Num;

			function add(uint num) public returns (uint256) {
				uint256 val = m_Num;
				val += num;
				m_Num = val;
				return val;
				//m_Num += num;
				//return m_Num;
			}
		}
*/

		static const char szCode[] = "608060405234801561001057600080fd5b5060da8061001f6000396000f3fe6080604052348015600f57600080fd5b506004361060285760003560e01c80631003e2d214602d575b600080fd5b603c60383660046066565b604e565b60405190815260200160405180910390f35b60008054605a8382607e565b60008190559392505050565b600060208284031215607757600080fd5b5035919050565b80820180821115609e57634e487b7160e01b600052601160045260246000fd5b9291505056fea26469706673582212207663ee6b0b30fe36e38ed6d65e28b6b9dbc407ec0e21a3a4c8213814e8bcf62864736f6c63430008110033";

		// owner example
		//static const char szCode[] = "608060405234801561001057600080fd5b5061005a6040518060400160405280601b81526020017f4f776e657220636f6e7472616374206465706c6f7965642062793a00000000008152503361009e60201b61011e1760201c565b600080546001600160a01b0319163390811782556040519091907f342827c97908e5e2f71151c08502a66d44b6f758e3ac2f1de95f02eb95f0a735908290a361016c565b6100e782826040516024016100b492919061010c565b60408051601f198184030181529190526020810180516001600160e01b0390811663319af33360e01b179091526100eb16565b5050565b80516a636f6e736f6c652e6c6f67602083016000808483855afa5050505050565b604081526000835180604084015260005b8181101561013a576020818701810151606086840101520161011d565b50600060608285018101919091526001600160a01b03949094166020840152601f01601f191690910190910192915050565b61024e8061017b6000396000f3fe608060405234801561001057600080fd5b50600436106100365760003560e01c8063893d20e81461003b578063a6f9dae11461005a575b600080fd5b600054604080516001600160a01b039092168252519081900360200190f35b61006d610068366004610188565b61006f565b005b6000546001600160a01b031633146100c35760405162461bcd60e51b815260206004820152601360248201527221b0b63632b91034b9903737ba1037bbb732b960691b604482015260640160405180910390fd5b600080546040516001600160a01b03808516939216917f342827c97908e5e2f71151c08502a66d44b6f758e3ac2f1de95f02eb95f0a73591a3600080546001600160a01b0319166001600160a01b0392909216919091179055565b61016382826040516024016101349291906101b8565b60408051601f198184030181529190526020810180516001600160e01b031663319af33360e01b179052610167565b5050565b80516a636f6e736f6c652e6c6f67602083016000808483855afa5050505050565b60006020828403121561019a57600080fd5b81356001600160a01b03811681146101b157600080fd5b9392505050565b604081526000835180604084015260005b818110156101e657602081870181015160608684010152016101c9565b50600060608285018101919091526001600160a01b03949094166020840152601f01601f19169091019091019291505056fea2646970667358221220378e9cbf36d8ffe76ab726d44156b5d1cf4dc7eb34584a67e3f9317e9f6c30e964736f6c63430008110033";

		MyProcessor evm;

		EvmProcessor::Word wOwner;
		memset0(wOwner.m_pData, wOwner.nBytes - 20);
		memset(wOwner.m_pData + wOwner.nBytes - 20, 0xff, 20);

		verify_test(evm.Construct(wOwner, szCode, _countof(szCode) - 1, nullptr, 0));

#pragma pack (push, 1)
		struct MyMethod
			:public EvmProcessor::Method
		{
			EvmProcessor::Word m_MyValue;
		} myArg;
#pragma pack (pop)

		uint64_t valExpected = 0;
		for (uint32_t i = 0; i < 10; i++)
		{
			uint32_t valAdd = (i + 5) * 19;
			valExpected += valAdd;

			myArg.SetSelector("add(uint256)");
			myArg.m_MyValue = valAdd;

			verify_test(evm.RunMethod_T(wOwner, myArg));

			verify_test(sizeof(EvmProcessor::Word) == evm.m_RetVal.n);
			const auto& wRes = *(const EvmProcessor::Word*) evm.m_RetVal.p;

			verify_test(EvmProcessor::Word(valExpected) == wRes);

		}
	}

} // namespace beam

int main()
{
	try
	{
		ECC::PseudoRandomGenerator prg;
		ECC::PseudoRandomGenerator::Scope scope(&prg);

		using namespace beam;

		beam::EvmTest1();

	}
	catch (const std::exception & ex)
	{
		printf("Expression: %s\n", ex.what());
		g_TestsFailed++;
	}

	return g_TestsFailed ? -1 : 0;
}
