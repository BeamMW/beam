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
#include "../../core/keccak.h"
#include "../../utility/test_helpers.h"
#include "../../utility/blobmap.h"
#include "../bvm2.h"
#include "../bvm2_impl.h"

#include "../ethash_service/ethash_utils.h"

#include <sstream>
#include <algorithm>
#include <iterator>
#include <math.h>

#if defined(__ANDROID__) || !defined(BEAM_USE_AVX)
#include "crypto/blake/ref/blake2.h"
#else
#include "crypto/blake/sse/blake2.h"
#endif

namespace Shaders {


#ifdef _MSC_VER
#	pragma warning (disable : 4200 4702) // unreachable code
#endif // _MSC_VER

#define BEAM_EXPORT

#include "../Shaders/common.h"
#include "../Shaders/Math.h"
#include "../Shaders/Sort.h"
#include "../Shaders/BeamHeader.h"
#include "../Shaders/Eth.h"

#include "../Shaders/vault/contract.h"
#include "../Shaders/oracle/contract.h"
#include "../Shaders/dummy/contract.h"
#include "../Shaders/StableCoin/contract.h"
#include "../Shaders/faucet/contract.h"
#include "../Shaders/roulette/contract.h"
#include "../Shaders/sidechain/contract.h"
#include "../Shaders/perpetual/contract.h"
#include "../Shaders/pipe/contract.h"
#include "../Shaders/mirrorcoin/contract.h"
#include "../Shaders/voting/contract.h"
#include "../Shaders/dao-core/contract.h"
#include "../Shaders/aphorize/contract.h"

	template <bool bToShader> void Convert(Vault::Request& x) {
		ConvertOrd<bToShader>(x.m_Aid);
		ConvertOrd<bToShader>(x.m_Amount);
	}
	template <bool bToShader> void Convert(Dummy::TestFarCall& x) {
	}
	template <bool bToShader> void Convert(Dummy::MathTest1& x) {
		ConvertOrd<bToShader>(x.m_Value);
		ConvertOrd<bToShader>(x.m_Rate);
		ConvertOrd<bToShader>(x.m_Factor);
		ConvertOrd<bToShader>(x.m_Try);
		ConvertOrd<bToShader>(x.m_IsOk);
	}
	template <bool bToShader> void Convert(Dummy::DivTest1& x) {
		ConvertOrd<bToShader>(x.m_Nom);
		ConvertOrd<bToShader>(x.m_Denom);
	}
	template <bool bToShader> void Convert(Dummy::InfCycle& x) {
		ConvertOrd<bToShader>(x.m_Val);
	}
	template <bool bToShader> void Convert(Dummy::Hash1&) {}
	template <bool bToShader> void Convert(Dummy::Hash2&) {}
	template <bool bToShader> void Convert(Dummy::Hash3&) {}

	template <bool bToShader> void Convert(Dummy::VerifyBeamHeader& x) {
		x.m_Hdr.template Convert<bToShader>();
	}
	template <bool bToShader> void Convert(Dummy::TestFarCallStack& x) {
		ConvertOrd<bToShader>(x.m_iCaller);
	}
	template <bool bToShader> void Convert(Dummy::TestRingSig& x) {
	}
	template <bool bToShader> void Convert(Dummy::TestEthHeader& x) {
		ConvertOrd<bToShader>(x.m_Header.m_Difficulty);
		ConvertOrd<bToShader>(x.m_Header.m_Number);
		ConvertOrd<bToShader>(x.m_Header.m_GasLimit);
		ConvertOrd<bToShader>(x.m_Header.m_GasUsed);
		ConvertOrd<bToShader>(x.m_Header.m_Time);
		ConvertOrd<bToShader>(x.m_Header.m_Nonce);
		ConvertOrd<bToShader>(x.m_EpochDatasetSize);
	}

	template <bool bToShader> void Convert(Roulette::Params& x) {
	}
	template <bool bToShader> void Convert(Roulette::Spin& x) {
		ConvertOrd<bToShader>(x.m_PlayingSectors);
	}
	template <bool bToShader> void Convert(Roulette::BetsOff& x) {
	}
	template <bool bToShader> void Convert(Roulette::Bid& x) {
		ConvertOrd<bToShader>(x.m_iSector);
	}
	template <bool bToShader> void Convert(Roulette::Take& x) {
	}

	template <bool bToShader> void Convert(Faucet::Params& x) {
		ConvertOrd<bToShader>(x.m_BacklogPeriod);
		ConvertOrd<bToShader>(x.m_MaxWithdraw);
	}
	template <bool bToShader> void Convert(Faucet::Deposit& x) {
		ConvertOrd<bToShader>(x.m_Aid);
		ConvertOrd<bToShader>(x.m_Amount);
	}
	template <bool bToShader> void Convert(Faucet::Withdraw& x) {
		ConvertOrd<bToShader>(x.m_Key.m_Aid);
		ConvertOrd<bToShader>(x.m_Amount);
	}

	template <bool bToShader, uint32_t nMeta> void Convert(StableCoin::Create<nMeta>& x) {
		ConvertOrd<bToShader>(x.m_CollateralizationRatio);
		ConvertOrd<bToShader>(x.m_BiddingDuration);
		ConvertOrd<bToShader>(x.m_nMetaData);
	}
	template <bool bToShader> void Convert(StableCoin::Balance& x) {
		ConvertOrd<bToShader>(x.m_Beam);
		ConvertOrd<bToShader>(x.m_Asset);
	}
	template <bool bToShader> void Convert(StableCoin::UpdatePosition& x) {
		Convert<bToShader>(x.m_Change);
	}
	template <bool bToShader> void Convert(StableCoin::PlaceBid& x) {
		Convert<bToShader>(x.m_Bid);
	}

	template <bool bToShader, uint32_t nProvs> void Convert(Oracle::Create<nProvs>& x) {
		ConvertOrd<bToShader>(x.m_Providers);
		ConvertOrd<bToShader>(x.m_InitialValue);
	}
	template <bool bToShader> void Convert(Oracle::Set& x) {
		ConvertOrd<bToShader>(x.m_iProvider);
		ConvertOrd<bToShader>(x.m_Value);
	}
	template <bool bToShader> void Convert(Oracle::Get& x) {
		ConvertOrd<bToShader>(x.m_Value);
	}

	template <bool bToShader> void Convert(Sidechain::Init& x) {
		x.m_Hdr0.template Convert<bToShader>();
		ConvertOrd<bToShader>(x.m_ComissionForProof);
	}
	template <bool bToShader, uint32_t nHdrs> void Convert(Sidechain::Grow<nHdrs>& x) {
		ConvertOrd<bToShader>(x.m_nSequence);
		x.m_Prefix.template Convert<bToShader>();

		for (uint32_t i = 0; i < nHdrs; i++)
			x.m_pSequence[i].template Convert<bToShader>();
	}
	template <bool bToShader, uint32_t nNodes> void Convert(Sidechain::VerifyProof<nNodes>& x) {
		ConvertOrd<bToShader>(x.m_Height);
		ConvertOrd<bToShader>(x.m_nProof);
	}
	template <bool bToShader> void Convert(Sidechain::WithdrawComission& x) {
		ConvertOrd<bToShader>(x.m_Amount);
	}

	template <bool bToShader> void Convert(Perpetual::Create& x) {
		ConvertOrd<bToShader>(x.m_MarginRequirement_mp);
	}
	template <bool bToShader> void Convert(Perpetual::CreateOffer& x) {
		ConvertOrd<bToShader>(x.m_AmountBeam);
		ConvertOrd<bToShader>(x.m_AmountToken);
		ConvertOrd<bToShader>(x.m_TotalBeams);
	}

	template <bool bToShader> void Convert(Pipe::Create& x) {
		ConvertOrd<bToShader>(x.m_Cfg.m_In.m_ComissionPerMsg);
		ConvertOrd<bToShader>(x.m_Cfg.m_In.m_hDisputePeriod);
		ConvertOrd<bToShader>(x.m_Cfg.m_In.m_StakeForRemote);
		ConvertOrd<bToShader>(x.m_Cfg.m_Out.m_CheckpointMaxDH);
		ConvertOrd<bToShader>(x.m_Cfg.m_Out.m_CheckpointMaxMsgs);
	}
	template <bool bToShader> void Convert(Pipe::SetRemote& x) {
	}
	template <bool bToShader> void Convert(Pipe::PushLocal0& x) {
		ConvertOrd<bToShader>(x.m_MsgSize);
	}
	template <bool bToShader> void Convert(Pipe::ReadRemote0& x) {
		ConvertOrd<bToShader>(x.m_iCheckpoint);
		ConvertOrd<bToShader>(x.m_iMsg);
		ConvertOrd<bToShader>(x.m_MsgSize);
	}

	template <bool bToShader> void Convert(MirrorCoin::Create0& x) {
		ConvertOrd<bToShader>(x.m_Aid);
		ConvertOrd<bToShader>(x.m_MetadataSize);
	}
	template <bool bToShader> void Convert(MirrorCoin::SetRemote& x) {
	}
	template <bool bToShader> void Convert(MirrorCoin::Send& x) {
		ConvertOrd<bToShader>(x.m_Amount);
	}
	template <bool bToShader> void Convert(MirrorCoin::Receive& x) {
		ConvertOrd<bToShader>(x.m_iCheckpoint);
		ConvertOrd<bToShader>(x.m_iMsg);
	}
	template <bool bToShader> void Convert(Voting::OpenProposal& x) {
		ConvertOrd<bToShader>(x.m_Params.m_Aid);
		ConvertOrd<bToShader>(x.m_Params.m_hMin);
		ConvertOrd<bToShader>(x.m_Params.m_hMax);
	}
	template <bool bToShader> void Convert(Voting::Vote& x) {
		ConvertOrd<bToShader>(x.m_Amount);
		ConvertOrd<bToShader>(x.m_Variant);
	}
	template <bool bToShader> void Convert(Voting::Withdraw& x) {
		ConvertOrd<bToShader>(x.m_Amount);
	}

	template <bool bToShader> void Convert(DemoXdao::UpdPosFarming& x) {
		ConvertOrd<bToShader>(x.m_Beam);
		ConvertOrd<bToShader>(x.m_WithdrawBeamX);
	}
	template <bool bToShader> void Convert(DemoXdao::GetPreallocated& x) {
		ConvertOrd<bToShader>(x.m_Amount);
	}

	template <bool bToShader> void Convert(Aphorize::Create& x) {
		ConvertOrd<bToShader>(x.m_Cfg.m_hPeriod);
		ConvertOrd<bToShader>(x.m_Cfg.m_PriceSubmit);
	}
	template <bool bToShader> void Convert(Aphorize::Submit& x) {
		ConvertOrd<bToShader>(x.m_Len);
	}

	namespace Env {


		void CallFarN(const ContractID& cid, uint32_t iMethod, void* pArgs, uint32_t nArgs, uint8_t bInheritContext);

		template <typename T>
		void CallFar_T(const ContractID& cid, T& args, uint8_t bInheritContext = 0)
		{
			Convert<true>(args);
			CallFarN(cid, args.s_iMethod, &args, sizeof(args), bInheritContext);
			Convert<false>(args);
		}

	} // namespace Env

	namespace Vault {
#include "../Shaders/vault/contract.cpp"
	}
	namespace Oracle {
#include "../Shaders/oracle/contract.cpp"
	}
	namespace StableCoin {
#include "../Shaders/StableCoin/contract.cpp"
	}
	namespace Faucet {
#include "../Shaders/faucet/contract.cpp"
	}
	namespace Roulette {
#include "../Shaders/roulette/contract.cpp"
	}
	namespace Dummy {
#include "../Shaders/dummy/contract.cpp"
	}
	namespace Perpetual {
#include "../Shaders/perpetual/contract.cpp"
	}
	namespace Sidechain {
#include "../Shaders/sidechain/contract.cpp"
	}
	namespace Pipe {
#include "../Shaders/pipe/contract.cpp"
	}
	namespace MirrorCoin {
#include "../Shaders/mirrorcoin/contract.cpp"
	}
	namespace Voting {
#include "../Shaders/voting/contract.cpp"
	}
	namespace DemoXdao {
#include "../Shaders/dao-core/contract.cpp"
	}
	namespace Aphorize {
#include "../Shaders/aphorize/contract.cpp"
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

#include "contract_test_processor.h"

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
		:public ContractTestProcessor
	{

		struct Code
		{
			ByteBuffer m_Vault;
			ByteBuffer m_Oracle;
			ByteBuffer m_Dummy;
			ByteBuffer m_Sidechain;
			ByteBuffer m_StableCoin;
			ByteBuffer m_Faucet;
			ByteBuffer m_Roulette;
			ByteBuffer m_Perpetual;
			ByteBuffer m_Pipe;
			ByteBuffer m_MirrorCoin;
			ByteBuffer m_Voting;
			ByteBuffer m_DemoXdao;
			ByteBuffer m_Aphorize;

		} m_Code;

		ContractID m_cidVault;
		ContractID m_cidOracle;
		ContractID m_cidStableCoin;
		ContractID m_cidFaucet;
		ContractID m_cidRoulette;
		ContractID m_cidDummy;
		ContractID m_cidSidechain;
		ContractID m_cidPerpetual;
		ContractID m_cidPipe;
		ContractID m_cidMirrorCoin1;
		ContractID m_cidMirrorCoin2;
		ContractID m_cidVoting;
		ContractID m_cidDemoXdao;
		ContractID m_cidAphorize;

		struct {

			Shaders::Eth::Header m_Header;
			uint32_t m_DatasetCount;
			ByteBuffer m_Proof;

		} m_Eth;


		virtual void CallFar(const ContractID& cid, uint32_t iMethod, Wasm::Word pArgs, uint8_t bInheritContext) override
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

			if (cid == m_cidFaucet)
			{
				//TempFrame f(*this, cid);
				//switch (iMethod)
				//{
				//case 0: Shaders::Faucet::Ctor(CastArg<Shaders::Faucet::Params>(pArgs)); return;
				//case 1: Shaders::Faucet::Dtor(nullptr); return;
				//case 2: Shaders::Faucet::Method_2(CastArg<Shaders::Faucet::Deposit>(pArgs)); return;
				//case 3: Shaders::Faucet::Method_3(CastArg<Shaders::Faucet::Withdraw>(pArgs)); return;
				//}
			}

			if (cid == m_cidRoulette)
			{
				//TempFrame f(*this, cid);
				//switch (iMethod)
				//{
				//case 0: Shaders::Roulette::Ctor(CastArg<Shaders::Roulette::Params>(pArgs)); return;
				//case 1: Shaders::Roulette::Dtor(nullptr); return;
				//case 2: Shaders::Roulette::Method_2(CastArg<Shaders::Roulette::Spin>(pArgs)); return;
				//case 3: Shaders::Roulette::Method_3(nullptr); return;
				//case 4: Shaders::Roulette::Method_4(CastArg<Shaders::Roulette::Bid>(pArgs)); return;
				//case 5: Shaders::Roulette::Method_5(CastArg<Shaders::Roulette::Take>(pArgs)); return;
				//}
			}

			if (cid == m_cidDummy)
			{
				//TempFrame f(*this, cid);
				//switch (iMethod)
				//{
				//case 9: Shaders::Dummy::Method_9(CastArg<Shaders::Dummy::VerifyBeamHeader>(pArgs)); return;
				//case 11: Shaders::Dummy::Method_11(CastArg<Shaders::Dummy::TestRingSig>(pArgs)); return;
				//case 12: Shaders::Dummy::Method_12(CastArg<Shaders::Dummy::TestEthHeader>(pArgs)); return;
				//}
			}

			if (cid == m_cidSidechain)
			{
				//TempFrame f(*this, cid);
				//switch (iMethod)
				//{
				//case 0: Shaders::Sidechain::Ctor(CastArg<Shaders::Sidechain::Init>(pArgs)); return;
				//case 2: Shaders::Sidechain::Method_2(CastArg<Shaders::Sidechain::Grow<0> >(pArgs)); return;
				//case 3: Shaders::Sidechain::Method_3(CastArg<Shaders::Sidechain::VerifyProof<0> >(pArgs)); return;
				//case 4: Shaders::Sidechain::Method_4(CastArg<Shaders::Sidechain::WithdrawComission>(pArgs)); return;
				//}
			}

			if (cid == m_cidPerpetual)
			{
				//TempFrame f(*this, cid);
				//switch (iMethod)
				//{
				//case 0: Shaders::Perpetual::Ctor(CastArg<Shaders::Perpetual::Create>(pArgs)); return;
				//case 2: Shaders::Perpetual::Method_2(CastArg<Shaders::Perpetual::CreateOffer>(pArgs)); return;
				//case 3: Shaders::Perpetual::Method_3(CastArg<Shaders::Perpetual::CancelOffer>(pArgs)); return;
				//}
			}

			if (cid == m_cidPipe)
			{
				//TempFrame f(*this, cid);
				//switch (iMethod)
				//{
				//case 0: Shaders::Pipe::Ctor(CastArg<Shaders::Pipe::Create>(pArgs)); return;
				//case 2: Shaders::Pipe::Method_2(CastArg<Shaders::Pipe::SetRemote>(pArgs)); return;
				//case 3: Shaders::Pipe::Method_3(CastArg<Shaders::Pipe::PushLocal0>(pArgs)); return;
				//case 4: Shaders::Pipe::Method_4(CastArg<Shaders::Pipe::PushRemote0>(pArgs)); return;
				//case 5: Shaders::Pipe::Method_5(CastArg<Shaders::Pipe::FinalyzeRemote>(pArgs)); return;
				//case 6: Shaders::Pipe::Method_6(CastArg<Shaders::Pipe::ReadRemote0>(pArgs)); return;
				//case 7: Shaders::Pipe::Method_7(CastArg<Shaders::Pipe::Withdraw>(pArgs)); return;
				//}
			}

			if ((cid == m_cidMirrorCoin1) || (cid == m_cidMirrorCoin2))
			{
				//TempFrame f(*this, cid);
				//switch (iMethod)
				//{
				//case 0: Shaders::MirrorCoin::Ctor(CastArg<Shaders::MirrorCoin::Create0>(pArgs)); return;
				//case 2: Shaders::MirrorCoin::Method_2(CastArg<Shaders::MirrorCoin::SetRemote>(pArgs)); return;
				//case 3: Shaders::MirrorCoin::Method_3(CastArg<Shaders::MirrorCoin::Send>(pArgs)); return;
				//case 4: Shaders::MirrorCoin::Method_4(CastArg<Shaders::MirrorCoin::Receive>(pArgs)); return;
				//}
			}

			if (cid == m_cidVoting)
			{
				//TempFrame f(*this, cid);
				//switch (iMethod)
				//{
				//case 2: Shaders::Voting::Method_2(CastArg<Shaders::Voting::OpenProposal>(pArgs)); return;
				//case 3: Shaders::Voting::Method_3(CastArg<Shaders::Voting::Vote>(pArgs)); return;
				//case 4: Shaders::Voting::Method_4(CastArg<Shaders::Voting::Withdraw>(pArgs)); return;
				//}
			}

			if (cid == m_cidDemoXdao)
			{
				//TempFrame f(*this, cid);
				//switch (iMethod)
				//{
				//case 0: Shaders::DemoXdao::Ctor(nullptr); return;
				//case 3: Shaders::DemoXdao::Method_3(CastArg<Shaders::DemoXdao::GetPreallocated>(pArgs)); return;
				//case 4: Shaders::DemoXdao::Method_4(CastArg<Shaders::DemoXdao::UpdPosFarming>(pArgs)); return;
				//}
			}

			if (cid == m_cidAphorize)
			{
				//TempFrame f(*this, cid);
				//switch (iMethod)
				//{
				//case 0: Shaders::Aphorize::Ctor(CastArg<Shaders::Aphorize::Create>(pArgs)); return;
				//case 2: Shaders::Aphorize::Method_2(CastArg<Shaders::Aphorize::Submit>(pArgs)); return;
				//}
			}


			ProcessorContract::CallFar(cid, iMethod, pArgs, bInheritContext);
		}

		void TestVault();
		void TestDummy();
		void TestOracle();
		void TestStableCoin();
		void TestFaucet();
		void TestRoulette();
		void TestSidechain();
		void TestPerpetual();
		void TestPipe();
		void TestMirrorCoin();
		void TestVoting();
		void TestDemoXdao();
		void TestAphorize();

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
		AddCode(m_Code.m_Vault, "vault/contract.wasm");
		AddCode(m_Code.m_Dummy, "dummy/contract.wasm");
		AddCode(m_Code.m_Oracle, "oracle/contract.wasm");
		AddCode(m_Code.m_StableCoin, "StableCoin/contract.wasm");
		AddCode(m_Code.m_Faucet, "faucet/contract.wasm");
		AddCode(m_Code.m_Roulette, "roulette/contract.wasm");
		AddCode(m_Code.m_Sidechain, "sidechain/contract.wasm");
		AddCode(m_Code.m_Perpetual, "perpetual/contract.wasm");
		AddCode(m_Code.m_Pipe, "pipe/contract.wasm");
		AddCode(m_Code.m_MirrorCoin, "mirrorcoin/contract.wasm");
		AddCode(m_Code.m_Voting, "voting/contract.wasm");
		AddCode(m_Code.m_DemoXdao, "dao-core/contract.wasm");
		AddCode(m_Code.m_Aphorize, "aphorize/contract.wasm");

		TestVault();
		TestAphorize();
		TestFaucet();
		TestRoulette();
		TestVoting();
		TestDemoXdao();
		TestDummy();
		TestSidechain();
		TestOracle();
		TestStableCoin();
		TestPerpetual();
		TestPipe();
		TestMirrorCoin();
	}

	struct CidTxt
	{
		char m_szBuf[Shaders::ContractID::nBytes * 5];

		void Set(const Shaders::ContractID& x)
		{
			char* p = m_szBuf;
			for (uint32_t i = 0; i < x.nBytes; i++)
			{
				if (i)
					*p++ = ',';

				*p++ = '0';
				*p++ = 'x';

				uintBigImpl::_Print(x.m_pData + i, 1, p);
				p += 2;
			}

			assert(p - m_szBuf < (long int)_countof(m_szBuf));
			*p = 0;
		}
	};

	static void VerifyId(const ContractID& cidExp, const ContractID& cid, const char* szName)
	{
		if (cidExp != cid)
		{
			CidTxt ct;
			ct.Set(cid);

			printf("Incorrect %s. Actual value: %s\n", szName, ct.m_szBuf);
			g_TestsFailed++;
			fflush(stdout);
		}
	}

#define VERIFY_ID(exp, actual) VerifyId(exp, actual, #exp)

	void MyProcessor::TestVault()
	{
		Zero_ zero;
		verify_test(ContractCreate_T(m_cidVault, m_Code.m_Vault, zero));
		VERIFY_ID(Shaders::Vault::s_CID, m_cidVault);

		bvm2::ShaderID sid;
		bvm2::get_ShaderID(sid, m_Code.m_Vault);
		VERIFY_ID(Shaders::Vault::s_SID, sid);

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

	void MyProcessor::TestAphorize()
	{
		{
			Shaders::Aphorize::Create args;
			args.m_Cfg.m_hPeriod = 10;
			args.m_Cfg.m_PriceSubmit = Shaders::g_Beam2Groth * 2;
			ZeroObject(args.m_Cfg.m_Moderator);
			verify_test(ContractCreate_T(m_cidAphorize, m_Code.m_Aphorize, args));
		}

		for (uint32_t i = 0; i < 20; i++)
		{
			struct MySubmit :public Shaders::Aphorize::Submit {
				char m_szText[20];
			};

			MySubmit args;
			ZeroObject(args.m_Pk);
			args.m_Len = _countof(args.m_szText);

			memset(args.m_szText, 'a' + i, sizeof(args.m_szText));

			verify_test(RunGuarded_T(m_cidAphorize, args.s_iMethod, args));
		}
	}

	namespace IndexDecoder
	{
		typedef uint32_t Word;

		template <uint32_t n>
		struct SrcRemaining
		{
			template <uint32_t nBitsPerIndex, uint32_t nSrcIdx>
			struct State
			{
				static const uint32_t s_BitsDecoded = nSrcIdx * 8;

				static const uint32_t s_iDst = s_BitsDecoded / nBitsPerIndex;
				static const uint32_t s_nDstBitsDone = s_BitsDecoded % nBitsPerIndex;
				static const uint32_t s_nDstBitsRemaining = nBitsPerIndex - s_nDstBitsDone;

				static void Do(Word* pDst, const uint8_t* pSrc)
				{
					uint8_t src = pSrc[nSrcIdx];
					if constexpr (s_nDstBitsRemaining >= 8)
					{
						if constexpr (!s_nDstBitsDone)
							pDst[s_iDst] = src;
						else
							pDst[s_iDst] |= ((Word) src)  << s_nDstBitsDone;
					}
					else
					{
						const uint8_t msk = (1 << s_nDstBitsRemaining) - 1;
						pDst[s_iDst] |= ((Word) (src & msk)) << s_nDstBitsDone;
						pDst[s_iDst + 1] = src >> s_nDstBitsRemaining;
					}

					typedef typename SrcRemaining<n - 1>::template State<nBitsPerIndex, nSrcIdx + 1> StateNext;
					StateNext::Do(pDst, pSrc);
				}
			};
		};

		template <>
		struct SrcRemaining<0>
		{
			template <uint32_t nBitsPerIndex, uint32_t nSrcIdx>
			struct State
			{
				static void Do(Word* pDst, const uint8_t* pSrc)
				{
				}
			};
		};
	}



	namespace IndexDecoder2
	{
		typedef uint32_t Word;
		static const uint32_t s_WordBits = sizeof(Word) * 8;

		template <uint32_t nBitsPerIndex, uint32_t nSrcIdx, uint32_t nSrcTotal>
		struct State
		{
			static_assert(nBitsPerIndex <= s_WordBits, "");
			static_assert(nBitsPerIndex >= s_WordBits/2, "unpack should affect no more than 3 adjacent indices");

			static const Word s_Msk = (((Word) 1) << nBitsPerIndex) - 1;

			static const uint32_t s_BitsDecoded = nSrcIdx * s_WordBits;

			static const uint32_t s_iDst = s_BitsDecoded / nBitsPerIndex;
			static const uint32_t s_nDstBitsDone = s_BitsDecoded % nBitsPerIndex;
			static const uint32_t s_nDstBitsRemaining = nBitsPerIndex - s_nDstBitsDone;

			static void Do(Word* pDst, const Word* pSrc)
			{
				Word src = ByteOrder::from_le(pSrc[nSrcIdx]);

				if constexpr (s_nDstBitsDone > 0)
					pDst[s_iDst] |= (src << s_nDstBitsDone) & s_Msk;
				else
					pDst[s_iDst] = src & s_Msk;

				pDst[s_iDst + 1] = (src >> s_nDstBitsRemaining) & s_Msk;

				if constexpr (s_nDstBitsRemaining + nBitsPerIndex < s_WordBits)
					pDst[s_iDst + 2] = (src >> (s_nDstBitsRemaining + nBitsPerIndex)) & s_Msk;

				if constexpr (nSrcIdx + 1 < nSrcTotal)
					State<nBitsPerIndex, nSrcIdx + 1, nSrcTotal>::Do(pDst, pSrc);
			}
		};
	}

	void CvtHdrPrefix(Shaders::BlockHeader::Prefix& bh, const Block::SystemState::Sequence::Prefix& s)
	{
		bh.m_Prev = s.m_Prev;
		bh.m_ChainWork = s.m_ChainWork;
		bh.m_Height = s.m_Height;
	}

	void CvtHdrElement(Shaders::BlockHeader::Element& bh, const Block::SystemState::Sequence::Element& s)
	{
		bh.m_Definition = s.m_Definition;
		bh.m_Kernels = s.m_Kernels;
		bh.m_Timestamp = s.m_TimeStamp;
		memcpy(&bh.m_PoW.m_pIndices, &s.m_PoW.m_Indices, s.m_PoW.m_Indices.size());
		memcpy(&bh.m_PoW.m_pNonce, &s.m_PoW.m_Nonce, s.m_PoW.m_Nonce.nBytes);
		bh.m_PoW.m_Difficulty = s.m_PoW.m_Difficulty.m_Packed;
	}

	void CreateRingSignature(const ECC::Hash::Value& msg, uint32_t nRing, const ECC::Point* pPk, uint32_t iProver, const ECC::Scalar::Native& sk, ECC::Scalar& e0, ECC::Scalar* pK)
	{
		assert(iProver < nRing);

		ECC::NonceGenerator ng("r-sig");
		ng << msg;
		ng << Blob(pPk, sizeof(*pPk) * nRing);

		{
			ECC::NoLeak<ECC::Scalar> s_;
			s_.V = sk;
			ng << s_.V.m_Value;
		}

		ECC::Scalar::Native n0; // the resid that we're going to leave
		ng >> n0;

		ECC::Point::Native ptN = ECC::Context::get().G * n0;

		for (uint32_t i = 0; ; i++)
		{
			uint32_t iUser = (i + iProver + 1) % nRing;

			ECC::SignatureBase sb;
			sb.m_NoncePub = ptN;

			ECC::Scalar::Native e;
			sb.get_Challenge(e, msg);

			if (!iUser)
				e0 = e;

			ECC::Scalar::Native k;

			if (iProver == iUser)
			{
				// calculate k such that:
				// k*G + e*P == n0*G
				// k = n0 - e*s

				e *= sk;
				n0 = n0 - e;

				pK[iProver] = n0;
				break;
			}

			ECC::Point::Native pk;
			pk.Import(pPk[iUser]);

			ng >> k;
			pK[iUser] = k;

			ptN = ECC::Context::get().G * k;
			ptN += pk * e;
		}
	}

	void MyProcessor::TestDummy()
	{
		ContractID cid;
		Zero_ zero;
		verify_test(ContractCreate_T(cid, m_Code.m_Dummy, zero));
		m_cidDummy = cid;

		{
			Shaders::Dummy::TestFarCall args;
			args.m_Variant = 0;
			args.m_InheritCtx = 0;
			verify_test(RunGuarded_T(cid, args.s_iMethod, args));
			args.m_Variant = 1;
			verify_test(!RunGuarded_T(cid, args.s_iMethod, args));
			args.m_Variant = 2;
			verify_test(!RunGuarded_T(cid, args.s_iMethod, args));
			args.m_Variant = 3;
			verify_test(!RunGuarded_T(cid, args.s_iMethod, args));
			args.m_Variant = 4;
			verify_test(!RunGuarded_T(cid, args.s_iMethod, args));
			args.m_Variant = 5;
			verify_test(!RunGuarded_T(cid, args.s_iMethod, args));
			args.m_Variant = 6;
			verify_test(!RunGuarded_T(cid, args.s_iMethod, args));
			args.m_Variant = 7;
			verify_test(!RunGuarded_T(cid, args.s_iMethod, args));

			args.m_Variant = 0;
			args.m_InheritCtx = 1;
			verify_test(RunGuarded_T(cid, args.s_iMethod, args)); // should succeed, but won't affect the callee contract
		}

		{
			Shaders::Dummy::MathTest1 args;
			args.m_Value = 0x1452310AB046C124;
			args.m_Rate = 0x0000010100000000;
			args.m_Factor = 0x0000000000F00000;
			args.m_Try = 0x1452310AB046C100;

			args.m_IsOk = 0;

			verify_test(RunGuarded_T(cid, args.s_iMethod, args));
		}

		{
			Shaders::Dummy::DivTest1 args;
			args.m_Nom = 0;
			args.m_Denom = 12;
			verify_test(RunGuarded_T(cid, args.s_iMethod, args));

			// division by 0 should be caought
			args.m_Nom = 13;
			args.m_Denom = 0;
			verify_test(!RunGuarded_T(cid, args.s_iMethod, args));
		}

		{
			Shaders::Dummy::InfCycle args;
			args.m_Val = 12;
			verify_test(!RunGuarded_T(cid, args.s_iMethod, args));
		}

		{
			Shaders::Dummy::Hash1 args;

			memset(args.m_pInp, 0xab, sizeof(args.m_pInp));
			verify_test(RunGuarded_T(cid, args.s_iMethod, args));

			ECC::Hash::Value hv;
			ECC::Hash::Processor()
				<< Blob(args.m_pInp, static_cast<uint32_t>(sizeof(args.m_pInp)))
				>> hv;

			static_assert(sizeof(hv) == sizeof(args.m_pRes));
			verify_test(!memcmp(hv.m_pData, args.m_pRes, sizeof(args.m_pRes)));
		}

		{
			Shaders::Dummy::Hash2 args;

			memset(args.m_pInp, 0xcd, sizeof(args.m_pInp));
			verify_test(RunGuarded_T(cid, args.s_iMethod, args));


			blake2b_state s;
			blake2b_param pars = { 0 };
			pars.digest_length = static_cast<uint8_t>(sizeof(args.m_pRes));
			pars.fanout = 1;
			pars.depth = 1;

			static const char szPers[] = "abcd";
			memcpy(pars.personal, szPers, sizeof(szPers) - 1);

			blake2b_init_param(&s, &pars);
			blake2b_update(&s, args.m_pInp, sizeof(args.m_pInp));

			uint8_t pRes[sizeof(args.m_pRes)];
			blake2b_final(&s, pRes, sizeof(pRes));

			verify_test(!memcmp(pRes, args.m_pRes, sizeof(pRes)));
		}

		{
			Shaders::Dummy::Hash3 args;

			// Test vector is taken from here: https://asecuritysite.com/encryption/s3
			static const char szTest[] = "abcdefgh01234567abcdefgh01234567abcdefgh01234567abcdefgh01234567abcdefgh01234567abcdefgh01234567abcdefgh01234567abcdefgh01234567abcdefgh01234567abcdefgh01234567abcdefgh01234567abcdefgh01234567abcdefgh01234567abcdefgh01234567abcdefgh01234567abcdefgh01234567abcdefgh01234567abcdefgh01234567abcdefgh01234567abcdefgh01234567abcdefgh01234567abcdefgh01234567abcdefgh01234567abcdefgh01234567abcdefgh01234567abcdefgh01234567abcdefgh01234567abcdefgh01234567abcdefgh01234567abcdefgh01234567abcdefgh01234567abcdefgh01234567111";

			uintBig_t<256 / 8> hvRes2; hvRes2.Scan("16b7283e0cd3cc3a1fbcd0d34372bf1ee530a1c3d2229c84927ea8ee2bdf49da");
			uintBig_t<384 / 8> hvRes3; hvRes3.Scan("5e72a175afd3d80982543e2decef851a9912cc114e5ae3693f5d82075f550fb867a2ba1bedd8f332afe67a754ed73f87");
			uintBig_t<512 / 8> hvRes4; hvRes4.Scan("3987b7c9927bd9bb1804467490283ff6ffc7d378634efe51d62b28d8b81be30dffe34a999b77017efc954c37345900051d0e0823ac78bcbb2e248021b0e9a96c");

			const uint32_t nInp = static_cast<uint32_t>(sizeof(szTest) - 1);
			static_assert(nInp <= sizeof(args.m_pInp));

			memcpy(args.m_pInp, szTest, nInp);
			args.m_Inp = nInp;

			for (args.m_NaggleBytes = 1; ; args.m_NaggleBytes += 13)
			{
				args.m_Bits = hvRes2.nBits;
				verify_test(RunGuarded_T(cid, args.s_iMethod, args));
				verify_test(!memcmp(args.m_pRes, hvRes2.m_pData, hvRes2.nBytes));

				args.m_Bits = hvRes3.nBits;
				verify_test(RunGuarded_T(cid, args.s_iMethod, args));
				verify_test(!memcmp(args.m_pRes, hvRes3.m_pData, hvRes3.nBytes));

				args.m_Bits = hvRes4.nBits;
				verify_test(RunGuarded_T(cid, args.s_iMethod, args));
				verify_test(!memcmp(args.m_pRes, hvRes4.m_pData, hvRes4.nBytes));

				if (args.m_NaggleBytes >= nInp)
					break;
			}
		}

		{
			// set mainnet rules
			auto& r = Rules::get();
			r.pForks[0].m_Height = 0;
			r.pForks[0].m_Hash.Scan("ed91a717313c6eb0e3f082411584d0da8f0c8af2a4ac01e5af1959e0ec4338bc");
			r.pForks[1].m_Height = 321321;
			r.pForks[1].m_Hash.Scan("622e615cfd29d0f8cdd9bdd76d3ca0b769c8661b29d7ba9c45856c96bc2ec5bc");
			r.pForks[2].m_Height = 777777;
			r.pForks[2].m_Hash.Scan("1ce8f721bf0c9fa7473795a97e365ad38bbc539aab821d6912d86f24e67720fc");

			r.pForks[3].m_Height = 999999999;
			r.pForks[3].m_Hash = Zero;

			beam::Block::SystemState::Full s;
			s.m_Height = 903720;
			s.m_Prev.Scan("62020e8ee408de5fdbd4c815e47ea098f5e30b84c788be566ac9425e9b07804d");
			s.m_ChainWork.Scan("0000000000000000000000000000000000000000000000aa0bd15c0cf6e00000");
			s.m_Kernels.Scan("ccabdcee29eb38842626ad1155014e2d7fc1b00d0a70ccb3590878bdb7f26a02");
			s.m_Definition.Scan("da1cf1a333d3e8b0d44e4c0c167df7bf604b55352e5bca3bc67dfd350fb707e9");
			s.m_TimeStamp = 1600968920;
			reinterpret_cast<uintBig_t<sizeof(s.m_PoW)>*>(&s.m_PoW)->Scan("188306068af692bdd9d40355eeca8640005aa7ff65b61a85b45fc70a8a2ac127db2d90c4fc397643a5d98f3e644f9f59fcf9677a0da2e90f597f61a1bf17d67512c6d57e680d0aa2642f7d275d2700188dbf8b43fac5c88fa08fa270e8d8fbc33777619b00000000ad636476f7117400acd56618");
			
			ECC::Hash::Value hv, hvExpected;
			hvExpected.Scan("23fe8673db74c43d4933b1f2d16db11b1a4895e3924a2f9caf92afa89fd01faf");

			s.get_Hash(hv);
			verify_test(hv == hvExpected);
			verify_test(s.IsValid());

			Shaders::Dummy::VerifyBeamHeader args;
			CvtHdrPrefix(args.m_Hdr, s);
			CvtHdrElement(args.m_Hdr, s);
			args.m_RulesCfg = r.pForks[2].m_Hash;

			verify_test(RunGuarded_T(cid, args.s_iMethod, args));

			verify_test(args.m_Hash == hv);

			Difficulty::Raw diff;
			s.m_PoW.m_Difficulty.Unpack(diff);
			diff.Negate();
			diff += s.m_ChainWork;
			verify_test(diff == args.m_ChainWork0);

			//uint32_t pIndices[32];
			//IndexDecoder2::State<25, 0, 25>::Do(pIndices, (const uint32_t*) &s.m_PoW.m_Indices.at(0));
			//verify_test(!memcmp(pIndices, args.m_pIndices, sizeof(pIndices)));

			//s.get_HashForPoW(hvExpected);
			//verify_test(args.m_HashForPoW == hvExpected);

			//Difficulty::Raw diffRaw;
			//s.m_PoW.m_Difficulty.Unpack(diffRaw);

			//verify_test(diffRaw == args.m_DiffUnpacked);
			//verify_test(args.m_DiffTestOk);
		}

		{
			Shaders::Dummy::TestFarCallStack args;
			ZeroObject(args);

			verify_test(RunGuarded_T(cid, args.s_iMethod, args));
			verify_test(args.m_Cid == m_cidDummy);

			args.m_iCaller = 1;
			verify_test(!RunGuarded_T(cid, args.s_iMethod, args));
		}

		{
			Shaders::Dummy::TestRingSig args;
			ZeroObject(args);

			ECC::SetRandom(args.m_Msg);

			const uint32_t nRing = Shaders::Dummy::TestRingSig::s_Ring;
			ECC::Scalar::Native pS[nRing];
			for (uint32_t i = 0; i < nRing; i++)
			{
				ECC::SetRandom(pS[i]);
				ECC::Point::Native pt = ECC::Context::get().G * pS[i];
				args.m_pPks[i] = pt;
			}

			const uint32_t iProver = 2;
			static_assert(iProver < nRing);

			CreateRingSignature(args.m_Msg, nRing, args.m_pPks, iProver, pS[iProver], args.m_e, args.m_pK);

			verify_test(RunGuarded_T(cid, args.s_iMethod, args));
		}

		//{
		//	Shaders::Dummy::TestEthash args;
		//	ZeroObject(args);
		//	args.m_BlockNumber = 5306861;
		//	args.m_HeaderHash.Scan("53a005f209a4dc013f022a5078c6b38ced76e767a30367ff64725f23ec652a9f");
		//	args.m_Nonce = 0xd337f82001e992c5ULL;
		//	args.m_Difficulty = 3250907161412814ULL;

		//	verify_test(RunGuarded_T(cid, args.s_iMethod, args));

		//	verify_test(RunGuarded_T(cid, args.s_iMethod, args)); // make sure it loads ok from our cache 

		//	args.m_Nonce++;
		//	verify_test(!RunGuarded_T(cid, args.s_iMethod, args));
		//}

		if (!m_Eth.m_Proof.empty())
		{
			ByteBuffer buf;
			buf.resize(sizeof(Shaders::Dummy::TestEthHeader) + m_Eth.m_Proof.size());

			auto& args = *reinterpret_cast<Shaders::Dummy::TestEthHeader*>(&buf.front());
			memcpy(&args + 1, &m_Eth.m_Proof.front(), m_Eth.m_Proof.size());

			ZeroObject(args);
			args.m_Header = m_Eth.m_Header;
			args.m_EpochDatasetSize = m_Eth.m_DatasetCount;

			verify_test(RunGuarded(cid, args.s_iMethod, buf, nullptr));
		}

		verify_test(ContractDestroy_T(cid, zero));
	}

	void CvtHdrSequence(Shaders::BlockHeader::Prefix& bhp, Shaders::BlockHeader::Element* pSeq, uint32_t n, const Block::SystemState::Full* pS)
	{
		CvtHdrPrefix(bhp, pS[0]);
		for (uint32_t i = 0; i < n; i++)
			CvtHdrElement(pSeq[i], pS[i]);
	}

	void MyProcessor::TestSidechain()
	{
		Block::SystemState::Full s;
		ZeroObject(s);
		s.m_Height = 920000;
		s.m_ChainWork = 100500U;

		std::vector<Block::SystemState::Full> vChain;
		vChain.push_back(s);

		{
			Shaders::Sidechain::Init args;
			ZeroObject(args);
			args.m_ComissionForProof = 400;
			CvtHdrPrefix(args.m_Hdr0, s);
			CvtHdrElement(args.m_Hdr0, s);
			args.m_Rules = Rules::get().pForks[2].m_Hash;
			verify_test(ContractCreate_T(m_cidSidechain, m_Code.m_Sidechain, args));
		}

		bvm2::ShaderID sid;
		bvm2::get_ShaderID(sid, m_Code.m_Sidechain);
		VERIFY_ID(Shaders::Sidechain::s_SID, sid);

		{
			const uint32_t nSeq = 10;

			Shaders::Sidechain::Grow<nSeq> args;
			ZeroObject(args);
			args.m_nSequence = nSeq;

			for (uint32_t i = 0; i < nSeq; i++)
			{
				s.NextPrefix();
				s.m_ChainWork += s.m_PoW.m_Difficulty;
				vChain.push_back(s);
			}

			CvtHdrSequence(args.m_Prefix, args.m_pSequence, nSeq, &vChain.at(1));

			verify_test(RunGuarded_T(m_cidSidechain, args.s_iMethod, args));

			verify_test(!RunGuarded_T(m_cidSidechain, args.s_iMethod, args)); // chainwork didn't grow

			args.m_Prefix.m_ChainWork.Inc(); // tamper with chainwork
			verify_test(!RunGuarded_T(m_cidSidechain, args.s_iMethod, args));
		}

		Merkle::FixedMmr fmmr;
		fmmr.Resize(14);

		for (uint32_t i = 0; i < 14; i++)
		{
			fmmr.Append(i);
		}

		{
			vChain.resize(1);
			s = vChain.back();

			const uint32_t nSeq = 6;

			Shaders::Sidechain::Grow<nSeq> args;
			ZeroObject(args);
			args.m_nSequence = nSeq;
			args.m_Contributor.m_X = 116U;

			for (uint32_t i = 0; i < nSeq; i++)
			{
				s.NextPrefix();
				s.m_PoW.m_Difficulty.m_Packed = 1 << Difficulty::s_MantissaBits; // difficulty x2
				s.m_ChainWork += s.m_PoW.m_Difficulty;

				if (i)
					s.m_Kernels = Zero;
				else
					fmmr.get_Hash(s.m_Kernels);

				vChain.push_back(s);
			}

			CvtHdrSequence(args.m_Prefix, args.m_pSequence, nSeq, &vChain.at(1));

			verify_test(RunGuarded_T(m_cidSidechain, args.s_iMethod, args)); // reorg should be ok, despite the fact it's shorter
		}

		{
			Merkle::Proof vProof;
			fmmr.get_Proof(vProof, 4);

			Shaders::Sidechain::VerifyProof<20> args;
			args.m_Height = vChain[1].m_Height;
			args.m_KernelID = 4U;

			args.m_nProof = static_cast<uint32_t>(vProof.size());
			for (uint32_t i = 0; i < args.m_nProof; i++)
			{
				args.m_pProof[i].m_OnRight = vProof[i].first;
				args.m_pProof[i].m_Value = vProof[i].second;
			}

			verify_test(RunGuarded_T(m_cidSidechain, args.s_iMethod, args));
			verify_test(RunGuarded_T(m_cidSidechain, args.s_iMethod, args)); // redundant proofs is ok
		}

		{
			Shaders::Sidechain::WithdrawComission args;
			ZeroObject(args);
			args.m_Contributor.m_X = 116U;
			args.m_Amount = 400;

			verify_test(RunGuarded_T(m_cidSidechain, args.s_iMethod, args));
			verify_test(RunGuarded_T(m_cidSidechain, args.s_iMethod, args));
			verify_test(!RunGuarded_T(m_cidSidechain, args.s_iMethod, args));
		}
	}

	void MyProcessor::TestOracle()
	{
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

		//verify_test(ContractDestroy_T(argSc.m_RateOracle, zero));
	}

	void MyProcessor::TestPerpetual()
	{
		bvm2::ShaderID sid;
		bvm2::get_ShaderID(sid, m_Code.m_Perpetual);
		VERIFY_ID(Shaders::Perpetual::s_SID, sid);

		{
			Shaders::Perpetual::Create arg;
			arg.m_Oracle =  m_cidOracle;
			arg.m_MarginRequirement_mp = 15 * 1000;
			verify_test(ContractCreate_T(m_cidPerpetual, m_Code.m_Perpetual, arg));
		}

		{
			Shaders::Perpetual::CreateOffer arg;
			ZeroObject(arg.m_Account);
			arg.m_AmountBeam = 1000;
			arg.m_AmountToken = 140;
			arg.m_TotalBeams = 1149;
			verify_test(!RunGuarded_T(m_cidPerpetual, arg.s_iMethod, arg)); // less than 15% collateral

			arg.m_TotalBeams = 1150;
			verify_test(RunGuarded_T(m_cidPerpetual, arg.s_iMethod, arg));
		}
	}

	void MyProcessor::TestPipe()
	{
		bvm2::ShaderID sid;
		bvm2::get_ShaderID(sid, m_Code.m_Pipe);
		VERIFY_ID(Shaders::Pipe::s_SID, sid);

		{
			Shaders::Pipe::Create arg;
			ZeroObject(arg);
			arg.m_Cfg.m_Out.m_CheckpointMaxDH = 5;
			arg.m_Cfg.m_Out.m_CheckpointMaxMsgs = 3;

			arg.m_Cfg.m_In.m_ComissionPerMsg = Rules::Coin;
			arg.m_Cfg.m_In.m_StakeForRemote = Rules::Coin * 100;
			arg.m_Cfg.m_In.m_hContenderWaitPeriod = 10;
			arg.m_Cfg.m_In.m_hDisputePeriod = 20;
			ZeroObject(arg.m_Cfg.m_In.m_RulesRemote);
			arg.m_Cfg.m_In.m_FakePoW = true;


			verify_test(ContractCreate_T(m_cidPipe, m_Code.m_Pipe, arg));
		}

		{
			Shaders::Pipe::SetRemote arg;
			ZeroObject(arg);
			verify_test(!RunGuarded_T(m_cidPipe, arg.s_iMethod, arg));

			arg.m_cid.Inc();
			verify_test(RunGuarded_T(m_cidPipe, arg.s_iMethod, arg));
		}

		ByteBuffer bufMsgs;

		for (uint32_t i = 0; i < 10; i++)
		{
			struct Arg {
				Shaders::Pipe::PushLocal0 m_Push;
				uint8_t m_pMsg[128];
			} arg;

			if (1 & i)
				ECC::SetRandom(arg.m_Push.m_Receiver);
			else
				arg.m_Push.m_Receiver = Zero;

			arg.m_Push.m_MsgSize = (uint32_t) sizeof(arg.m_pMsg) - i;
			memset(arg.m_pMsg, '0' + i, arg.m_Push.m_MsgSize);

			verify_test(RunGuarded(m_cidPipe, arg.m_Push.s_iMethod, Blob(&arg, sizeof(arg)), nullptr));

			size_t iPos = bufMsgs.size();
			bufMsgs.resize(bufMsgs.size() + sizeof(Shaders::Pipe::MsgHdr) + sizeof(uint32_t) + arg.m_Push.m_MsgSize);

			auto* pPtr = &bufMsgs.front() + iPos;

			Shaders::Pipe::MsgHdr hdr;
			hdr.m_Sender = Zero;
			hdr.m_Receiver = arg.m_Push.m_Receiver;

			memcpy(pPtr, &arg.m_Push.m_MsgSize, sizeof(arg.m_Push.m_MsgSize));
			pPtr += sizeof(arg.m_Push.m_MsgSize);

			memcpy(pPtr, &hdr, sizeof(hdr));
			pPtr += sizeof(hdr);

			memcpy(pPtr, arg.m_pMsg, arg.m_Push.m_MsgSize);
		}

		{
			ByteBuffer bufArg;
			bufArg.resize(sizeof(Shaders::Pipe::PushRemote0) + sizeof(uint32_t) + bufMsgs.size());
			auto* pArg = reinterpret_cast<Shaders::Pipe::PushRemote0*>(&bufArg.front());

			ECC::SetRandom(pArg->m_User.m_X);
			pArg->m_User.m_Y = 0;

			pArg->m_Flags = Shaders::Pipe::PushRemote0::Flags::Msgs;

			uint32_t nSize = static_cast<uint32_t>(bufMsgs.size());
			memcpy((void*)(pArg + 1), &nSize, sizeof(nSize));

			memcpy(&bufArg.front() + sizeof(Shaders::Pipe::PushRemote0) + sizeof(uint32_t), &bufMsgs.front(), nSize);

			verify_test(RunGuarded(m_cidPipe, pArg->s_iMethod, bufArg, nullptr)); // should be ok

			verify_test(!RunGuarded(m_cidPipe, pArg->s_iMethod, bufArg, nullptr)); // should evaluate both variants, and notice they're same
		}

		{
			Shaders::Pipe::FinalyzeRemote arg;
			arg.m_DepositStake = 0;
			verify_test(!RunGuarded(m_cidPipe, arg.s_iMethod, Blob(&arg, sizeof(arg)), nullptr)); // too early

			m_Height += 40;
			verify_test(RunGuarded(m_cidPipe, arg.s_iMethod, Blob(&arg, sizeof(arg)), nullptr));
		}

		{
#pragma pack (push, 1)
			struct Arg {
				Shaders::Pipe::ReadRemote0 m_Read;
				uint8_t m_pMsg[140];
			} arg;
#pragma pack (pop)

			arg.m_Read.m_iCheckpoint = 0;
			arg.m_Read.m_iMsg = 1;
			arg.m_Read.m_Wipe = 1;
			arg.m_Read.m_MsgSize = sizeof(arg.m_pMsg);

			verify_test(!RunGuarded(m_cidPipe, arg.m_Read.s_iMethod, Blob(&arg, sizeof(arg)), nullptr)); // private msg, we can't read it

			arg.m_Read.m_iMsg = 2;
			verify_test(!RunGuarded(m_cidPipe, arg.m_Read.s_iMethod, Blob(&arg, sizeof(arg)), nullptr)); // public, can't wipe

			arg.m_Read.m_Wipe = 0;
			verify_test(RunGuarded(m_cidPipe, arg.m_Read.s_iMethod, Blob(&arg, sizeof(arg)), nullptr)); // ok
			verify_test(RunGuarded(m_cidPipe, arg.m_Read.s_iMethod, Blob(&arg, sizeof(arg)), nullptr)); // ok, msg is not wiped

			verify_test(arg.m_Read.m_MsgSize == 126);
			for (uint32_t i = 0; i < arg.m_Read.m_MsgSize; i++)
				verify_test(arg.m_pMsg[i] == '2');
		}
	}

	void MyProcessor::TestMirrorCoin()
	{
		bvm2::ShaderID sid;
		bvm2::get_ShaderID(sid, m_Code.m_MirrorCoin);
		VERIFY_ID(Shaders::MirrorCoin::s_SID, sid);

		{
			Shaders::MirrorCoin::Create0 arg;
			arg.m_Aid = 0;
			arg.m_MetadataSize = 0;
			arg.m_PipeID = m_cidPipe;

			verify_test(ContractCreate_T(m_cidMirrorCoin1, m_Code.m_MirrorCoin, arg));
		}

		{
#pragma pack (push, 1)
			struct Arg :public Shaders::MirrorCoin::Create0 {
				char m_chMeta = 'x';
			} arg;
#pragma pack (pop)
			arg.m_Aid = 0;
			arg.m_MetadataSize = sizeof(arg.m_chMeta);
			arg.m_PipeID = m_cidPipe;

			verify_test(ContractCreate_T(m_cidMirrorCoin2, m_Code.m_MirrorCoin, arg));
		}

		{
			Shaders::MirrorCoin::SetRemote arg;
			arg.m_Cid = m_cidMirrorCoin2;
			verify_test(RunGuarded_T(m_cidMirrorCoin1, arg.s_iMethod, arg));
			arg.m_Cid = m_cidMirrorCoin1;
			verify_test(RunGuarded_T(m_cidMirrorCoin2, arg.s_iMethod, arg));
		}

#pragma pack (push, 1)
		struct MirrorMsg
			:public Shaders::Pipe::MsgHdr
			,public Shaders::MirrorCoin::Message
		{
		};
#pragma pack (pop)

		for (uint32_t iCycle = 0; iCycle < 2; iCycle++)
		{
			const ContractID& cidSrc = iCycle ? m_cidMirrorCoin2 : m_cidMirrorCoin1;
			const ContractID& cidDst = iCycle ? m_cidMirrorCoin1 : m_cidMirrorCoin2;

			Shaders::MirrorCoin::Send argS;
			argS.m_Amount = 450;
			ECC::SetRandom(argS.m_User.m_X);
			argS.m_User.m_Y = 0;
			
			verify_test(RunGuarded_T(cidSrc, argS.s_iMethod, argS));

			// simulate message passed
			Shaders::Env::Key_T<Shaders::Pipe::MsgHdr::KeyIn> key;
			key.m_Prefix.m_Cid = m_cidPipe;
			key.m_KeyInContract.m_iCheckpoint_BE = 0;
			key.m_KeyInContract.m_iMsg_BE = ByteOrder::to_be(1U);

			MirrorMsg msg;
			msg.m_Sender = cidSrc;
			msg.m_Receiver = cidDst;
			msg.m_User = argS.m_User;
			msg.m_Amount = argS.m_Amount;

			SaveVar(Blob(&key, sizeof(key)), Blob(&msg, sizeof(msg)));

			Shaders::MirrorCoin::Receive argR;
			argR.m_iCheckpoint = 0;
			argR.m_iMsg = 1;

			verify_test(RunGuarded_T(cidDst, argR.s_iMethod, argR));
			verify_test(!RunGuarded_T(cidDst, argR.s_iMethod, argR)); // double-spend should not be possible
		}

	}

	void MyProcessor::TestFaucet()
	{
		Shaders::Faucet::Params pars;
		pars.m_BacklogPeriod = 5;
		pars.m_MaxWithdraw = 400;

		verify_test(ContractCreate_T(m_cidFaucet, m_Code.m_Faucet, pars));

		bvm2::ShaderID sid;
		bvm2::get_ShaderID(sid, m_Code.m_Faucet);
		VERIFY_ID(Shaders::Faucet::s_SID, sid);

		m_lstUndo.Clear();

		Shaders::Faucet::Deposit deps;
		deps.m_Aid = 10;
		deps.m_Amount = 20000;
		verify_test(RunGuarded_T(m_cidFaucet, Shaders::Faucet::Deposit::s_iMethod, deps));

		Shaders::Faucet::Withdraw wdrw;

		ECC::Scalar::Native k;
		ECC::SetRandom(k);
		ECC::Point::Native pt = ECC::Context::get().G * k;

		wdrw.m_Key.m_Account = pt;
		wdrw.m_Key.m_Aid = 10;
		wdrw.m_Amount = 150;

		verify_test(RunGuarded_T(m_cidFaucet, Shaders::Faucet::Withdraw::s_iMethod, wdrw));
		verify_test(RunGuarded_T(m_cidFaucet, Shaders::Faucet::Withdraw::s_iMethod, wdrw));
		verify_test(!RunGuarded_T(m_cidFaucet, Shaders::Faucet::Withdraw::s_iMethod, wdrw));

		m_Height += 15;
		verify_test(RunGuarded_T(m_cidFaucet, Shaders::Faucet::Withdraw::s_iMethod, wdrw));

		wdrw.m_Amount = 0;
		verify_test(RunGuarded_T(m_cidFaucet, Shaders::Faucet::Withdraw::s_iMethod, wdrw));

		m_Height += 5;
		verify_test(RunGuarded_T(m_cidFaucet, Shaders::Faucet::Withdraw::s_iMethod, wdrw));

		UndoChanges(); // up to (but not including) contract creation
	}

	void MyProcessor::TestRoulette()
	{
		Shaders::Roulette::Params pars;
		memset(reinterpret_cast<void*>(&pars.m_Dealer), 0xe1, sizeof(pars.m_Dealer));

		verify_test(ContractCreate_T(m_cidRoulette, m_Code.m_Roulette, pars));

		bvm2::ShaderID sid;
		bvm2::get_ShaderID(sid, m_Code.m_Roulette);
		VERIFY_ID(Shaders::Roulette::s_SID, sid);

		Shaders::Roulette::Spin spin;
		verify_test(RunGuarded_T(m_cidRoulette, spin.s_iMethod, spin));

		Shaders::Roulette::Bid bid;
		bid.m_Player.m_Y = 0;

		for (uint32_t i = 0; i < Shaders::Roulette::State::s_Sectors * 2; i++)
		{
			bid.m_Player.m_X = i;
			bid.m_iSector = i % Shaders::Roulette::State::s_Sectors;
			verify_test(RunGuarded_T(m_cidRoulette, bid.s_iMethod, bid));
		}

		verify_test(!RunGuarded_T(m_cidRoulette, bid.s_iMethod, bid)); // redundant bid

		bid.m_iSector = Shaders::Roulette::State::s_Sectors + 3;
		bid.m_Player.m_X = Shaders::Roulette::State::s_Sectors * 2 + 8;
		verify_test(!RunGuarded_T(m_cidRoulette, bid.s_iMethod, bid)); // invalid sector

		// alleged winner
		Block::SystemState::Full s;
		ZeroObject(s);
		s.m_Height = m_Height;
		Merkle::Hash hv;
		s.get_Hash(hv);

		uint64_t val;
		memcpy(&val, hv.m_pData, sizeof(val));
		Shaders::ConvertOrd<false>(val);
		uint32_t iWinner = static_cast<uint32_t>(val % Shaders::Roulette::State::s_Sectors);


		Shaders::Roulette::Take take;
		take.m_Player.m_X = iWinner;
		take.m_Player.m_Y = 0;
		verify_test(!RunGuarded_T(m_cidRoulette, take.s_iMethod, take)); // round isn't over

		Zero_ zero;
		verify_test(RunGuarded_T(m_cidRoulette, Shaders::Roulette::BetsOff::s_iMethod, zero));

		verify_test(RunGuarded_T(m_cidRoulette, take.s_iMethod, take)); // ok
		verify_test(!RunGuarded_T(m_cidRoulette, take.s_iMethod, take)); // already took

		UndoChanges(); // up to (but not including) contract creation
	}

	void MyProcessor::TestVoting()
	{
		Zero_ zero;
		verify_test(ContractCreate_T(m_cidVoting, m_Code.m_Voting, zero));
		VERIFY_ID(Shaders::Voting::s_CID, m_cidVoting);

		bvm2::ShaderID sid;
		bvm2::get_ShaderID(sid, m_Code.m_Voting);
		VERIFY_ID(Shaders::Voting::s_SID, sid);

		m_lstUndo.Clear();
		m_Height = 10;

		ECC::Hash::Value hvProposal = 443U;

		{
			Shaders::Voting::OpenProposal args;
			args.m_ID = hvProposal;
			args.m_Params.m_Aid = 5;
			args.m_Params.m_hMin = 0;
			args.m_Params.m_hMax = 1;
			args.m_Variants = 12;
			verify_test(!RunGuarded_T(m_cidVoting, args.s_iMethod, args)); // too late

			args.m_Params.m_hMax = 10;
			verify_test(RunGuarded_T(m_cidVoting, args.s_iMethod, args));

			verify_test(!RunGuarded_T(m_cidVoting, args.s_iMethod, args)); // duplicated ID
		}

		ECC::Scalar::Native k;
		ECC::SetRandom(k);
		ECC::Point::Native pt_ = ECC::Context::get().G * k;
		ECC::Point pt = pt_;

		{
			Shaders::Voting::Vote args;
			args.m_ID = Zero; // invalid
			args.m_Pk = pt;
			args.m_Amount = 100;
			args.m_Variant = 4;

			verify_test(!RunGuarded_T(m_cidVoting, args.s_iMethod, args));

			args.m_ID = hvProposal;
			args.m_Variant = 12; // out-of-bounds
			verify_test(!RunGuarded_T(m_cidVoting, args.s_iMethod, args));

			args.m_Variant = 11;
			m_Height = 15; // too late
			verify_test(!RunGuarded_T(m_cidVoting, args.s_iMethod, args));

			m_Height = 5; // ok
			verify_test(RunGuarded_T(m_cidVoting, args.s_iMethod, args));
		}

		{
			Shaders::Voting::Withdraw args;
			args.m_ID = hvProposal;
			args.m_Pk = pt;
			args.m_Amount = 50;

			verify_test(!RunGuarded_T(m_cidVoting, args.s_iMethod, args)); // voting isn't over yet

			m_Height = 11;
			verify_test(RunGuarded_T(m_cidVoting, args.s_iMethod, args)); // ok, withdrew half
			verify_test(RunGuarded_T(m_cidVoting, args.s_iMethod, args)); // withdrew all
			verify_test(!RunGuarded_T(m_cidVoting, args.s_iMethod, args)); // no more left
		}
	}

	struct LutGenerator
	{
		typedef uint64_t TX;
		typedef double TY;
		virtual double Evaluate(TX) = 0;

		std::vector<TX> m_vX;
		std::vector<TY> m_vY;
		std::vector<uint32_t> m_vYNorm;

		bool IsGoodEnough(TX x, TX x1, double y1, double yPrecise, double tolerance)
		{
			const TX& xPrev = m_vX.back();
			const TY& yPrev = m_vY.back();

			double yInterp = yPrev + (y1 - yPrev) * (x - xPrev) / (x1 - xPrev);

			double yErr = yInterp - yPrecise;
			return (fabs(yErr / yPrecise) <= tolerance);
		}

		bool IsGoodEnough(TX x, TX x1, double y1, double tolerance)
		{
			double yPrecise = Evaluate(x);
			return IsGoodEnough(x, x1, y1, yPrecise, tolerance);
		}

		void Generate(TX x0, TX x1, double tolerance)
		{
			assert(x0 < x1);

			m_vX.push_back(x0);
			m_vY.push_back(Evaluate(x0));

			double y1 = Evaluate(x1);

			while (true)
			{
				TX xNext = x1;
				TY yNext = y1;

				uint32_t nCycles = 0;
				for (; ; nCycles++)
				{
					// probe 3 points: begin, end, mid
					const TX& xPrev = m_vX.back();
					TX dx = xNext - xPrev;
					TX xMid = xPrev + dx / 2;
					double yMid = Evaluate(xMid);

					bool bOk = true;
					double tolerance_der = tolerance / (double) (dx - 2);
					if (bOk && !IsGoodEnough(xPrev + 1, xNext, yNext, tolerance_der))
						bOk = false;

					if (bOk && !IsGoodEnough(xNext - 1, xNext, yNext, tolerance_der))
						bOk = false;

					if (bOk && !IsGoodEnough(xMid, xNext, yNext, yMid, tolerance))
						bOk = false;

					if (bOk)
						break;

					xNext = xMid;
					yNext = yMid;
				}

				m_vX.push_back(xNext);
				m_vY.push_back(yNext);

				if (!nCycles)
					break;
			}

		}

		void Normalize(uint32_t nMax)
		{
			double maxVal = 0;
			for (size_t i = 0; i < m_vY.size(); i++)
				std::setmax(maxVal, m_vY[i]);

			m_vYNorm.resize(m_vY.size());
			for (size_t i = 0; i < m_vY.size(); i++)
				m_vYNorm[i] = static_cast<uint32_t>(nMax * m_vY[i] / maxVal);
		}
	};

	void MyProcessor::TestDemoXdao()
	{
		//struct MyLutGenerator
		//	:public LutGenerator
		//{
		//	virtual double Evaluate(TX x)
		//	{
		//		double k = ((double) x) / (double) (Shaders::g_Beam2Groth * 100);
		//		return pow(k, 0.7);
		//	}
		//};

		//MyLutGenerator lg;
		//lg.Generate(Shaders::g_Beam2Groth * 16, Shaders::g_Beam2Groth * 1000000, 0.1);
		//lg.Normalize(1000000);

		Zero_ zero;
		verify_test(ContractCreate_T(m_cidDemoXdao, m_Code.m_DemoXdao, zero));

		bvm2::ShaderID sid;
		bvm2::get_ShaderID(sid, m_Code.m_DemoXdao);
		VERIFY_ID(Shaders::DemoXdao::s_SID, sid);

		for (uint32_t i = 0; i < 10; i++)
		{
			Shaders::DemoXdao::UpdPosFarming args;
			ZeroObject(args);

			args.m_Beam = Shaders::g_Beam2Groth * 20000 * (i + 3);
			args.m_BeamLock = 1;
			args.m_Pk.m_X = i;
			verify_test(RunGuarded_T(m_cidDemoXdao, args.s_iMethod, args));

			if (i & 1)
				m_Height += 1000;
		}

		for (uint32_t i = 0; i < 10; i++)
		{
			Shaders::DemoXdao::UpdPosFarming args;
			ZeroObject(args);

			args.m_Beam = Shaders::g_Beam2Groth * 20000 * (i + 3);
			args.m_Pk.m_X = i;
			verify_test(RunGuarded_T(m_cidDemoXdao, args.s_iMethod, args));

			if (i & 1)
				m_Height += 1000;
		}

		// the following is disabled, since the contract in this test is standalone, not under Upgradable, hence it doesn' allocate anything in c'tor
/*
		{
			Shaders::DemoXdao::GetPreallocated args;
			ZeroObject(args);
			args.m_Amount = 50;
			Cast::Reinterpret<beam::uintBig_t<33> >(args.m_Pk).Scan("8bb3375b455d9c577134b00e8b0b108a29ce2bc0fce929049306cf4fed723b7d00");
			verify_test(!RunGuarded_T(m_cidDemoXdao, args.s_iMethod, args)); // wrong pk

			Cast::Reinterpret<beam::uintBig_t<33> >(args.m_Pk).Scan("8bb3375b455d9c577134b00e8b0b108a29ce2bc0fce929049306cf4fed723b7d01");
			verify_test(RunGuarded_T(m_cidDemoXdao, args.s_iMethod, args)); // ok

			args.m_Amount = 31000 / 2 * Shaders::g_Beam2Groth;
			verify_test(!RunGuarded_T(m_cidDemoXdao, args.s_iMethod, args)); // too much
		}
*/
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
			:public IReadVars
		{
			BlobMap::Set::iterator m_it;
			BlobMap::Set::iterator m_itEnd;

			virtual bool MoveNext() override
			{
				if (m_it == m_itEnd)
					return false;

				const auto& x = *m_it;

				m_LastKey = x.ToBlob();
				m_LastVal = x.m_Data;

				m_it++;
				return true;
			}
		};

		virtual void VarsEnum(const Blob& kMin, const Blob& kMax, IReadVars::Ptr& pRes) override
		{
			auto p = std::make_unique<VarEnumCtx>();

			ZeroObject(p->m_LastKey);
			ZeroObject(p->m_LastVal);

			p->m_it = m_Vars.lower_bound(kMin, BlobMap::Set::Comparator());
			p->m_itEnd = m_Vars.upper_bound(kMax, BlobMap::Set::Comparator());

			pRes = std::move(p);
		}

		void TestHeap()
		{
			uint32_t p1, p2, p3;
			verify_test(HeapAllocEx(p1, 160));
			verify_test(HeapAllocEx(p2, 300));
			verify_test(HeapAllocEx(p3, 28));

			HeapFreeEx(p2);
			verify_test(HeapAllocEx(p2, 260));
			HeapFreeEx(p2);
			verify_test(HeapAllocEx(p2, 360));

			HeapFreeEx(p1);
			HeapFreeEx(p3);
			HeapFreeEx(p2);

			verify_test(HeapAllocEx(p1, 37443));
			HeapFreeEx(p1);
		}

		void RunMany(uint32_t iMethod)
		{
			std::ostringstream os;
			//m_Dbg.m_pOut = &os;

			os << "BVM Method: " << iMethod << std::endl;

			Shaders::Env::g_pEnv = this;

			uint32_t nCycles = 0;

			for (CallMethod(iMethod); !IsDone(); nCycles++)
			{
				RunOnce();

#ifdef WASM_INTERPRETER_DEBUG
				if (m_Dbg.m_pOut)
				{
					std::cout << m_Dbg.m_pOut->str();
					m_Dbg.m_pOut->str("");
				}
#endif // WASM_INTERPRETER_DEBUG
			}


			os << "Done in " << nCycles << " cycles" << std::endl << std::endl;
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

void Shaders::Env::CallFarN(const ContractID& cid, uint32_t iMethod, void* pArgs, uint32_t nArgs, uint8_t bInheritContext)
{
	Cast::Up<beam::bvm2::MyProcessor>(g_pEnv)->CallFarN(cid, iMethod, pArgs, nArgs, bInheritContext);
}

namespace 
{
	void TestRLP()
	{
		using namespace Shaders;
		using namespace beam;
		using namespace Eth;

		struct ByteStream
		{
			ByteBuffer m_Buffer;

			void Write(uint8_t b)
			{
				m_Buffer.emplace_back(b);
			}

			void Write(const uint8_t* p, uint32_t n)
			{
				::std::copy(p, p + n, ::std::back_inserter(m_Buffer));
			}
		};

		struct RlpVisitor
		{
			struct Node
			{
				Rlp::Node::Type m_Type;
				ByteBuffer m_Buffer;
			};

			bool OnNode(const Rlp::Node& node)
			{
				auto& item = m_Items.emplace_back();
				item.m_Type = node.m_Type;
				item.m_Buffer.assign(node.m_pBuf, node.m_pBuf + node.m_nLen);
				return false;
			}

	
			std::vector<Node> m_Items;
		};

		auto dog = to_opaque("dog");
		// The string 'dog' = [0x83, 'd', 'o', 'g']
		{
			Rlp::Node n(dog);
			ByteStream bs;
			n.Write(bs);
			verify_test(bs.m_Buffer == ByteBuffer({ 0x83, 'd', 'o', 'g' }));

			RlpVisitor v;
			verify_test(Rlp::Decode(bs.m_Buffer.data(), (uint32_t)bs.m_Buffer.size(), v));
			verify_test(v.m_Items[0].m_Type == Rlp::Node::Type::String && v.m_Items[0].m_Buffer == ByteBuffer({ 'd', 'o', 'g' }));
		}
		// The list['cat', 'dog'] = [0xc8, 0x83, 'c', 'a', 't', 0x83, 'd', 'o', 'g']
		{
			auto cat = to_opaque("cat");
			Rlp::Node nodes[] = {Rlp::Node(cat), Rlp::Node(dog)};
			Rlp::Node list(nodes);

			ByteStream bs;
			list.Write(bs);
			verify_test(bs.m_Buffer == ByteBuffer({ 0xc8, 0x83, 'c', 'a', 't', 0x83, 'd', 'o', 'g' }));

			RlpVisitor v;
			verify_test(Rlp::Decode(bs.m_Buffer.data(), (uint32_t)bs.m_Buffer.size(), v));
			RlpVisitor vt;
			verify_test(!Rlp::Decode(bs.m_Buffer.data(), (uint32_t)bs.m_Buffer.size()-1, vt));
			
			verify_test(v.m_Items[0].m_Type == Rlp::Node::Type::List);
			RlpVisitor v2;
			verify_test(Rlp::Decode(v.m_Items[0].m_Buffer.data(), (uint32_t)v.m_Items[0].m_Buffer.size(), v2));

			verify_test(v2.m_Items[0].m_Buffer == ByteBuffer({ 'c', 'a', 't' }));
			verify_test(v2.m_Items[1].m_Buffer == ByteBuffer({ 'd', 'o', 'g' }));
			
		}

		// The empty string('null') = [0x80]
		{
			Rlp::Node n(to_opaque(""));
			ByteStream bs;
			n.Write(bs);
			verify_test(bs.m_Buffer == ByteBuffer({ 0x80 }));
			RlpVisitor v;
			verify_test(Rlp::Decode(bs.m_Buffer.data(), (uint32_t)bs.m_Buffer.size(), v));
			verify_test(v.m_Items[0].m_Type == Rlp::Node::Type::String && v.m_Items[0].m_Buffer.empty());
		}
		
		auto createEmptyList = []()
		{
			Rlp::Node list;
			list.m_Type = Rlp::Node::Type::List;
			list.m_nLen = 0;
			return list;
		};

		//The empty list = [0xc0]
		{
			Rlp::Node list = createEmptyList();

			ByteStream bs;
			list.Write(bs);
			verify_test(bs.m_Buffer == ByteBuffer({ 0xc0 }));
			RlpVisitor v;
			verify_test(Rlp::Decode(bs.m_Buffer.data(), (uint32_t)bs.m_Buffer.size(), v));
			verify_test(v.m_Items[0].m_Type == Rlp::Node::Type::List && v.m_Items[0].m_Buffer.empty());
		}

			
		//The integer 0 = [0x80]
		{
			Rlp::Node n(0);
			ByteStream bs;
			n.Write(bs);
			verify_test(bs.m_Buffer == ByteBuffer({ 0x80 }));
			RlpVisitor v;
			verify_test(Rlp::Decode(bs.m_Buffer.data(), (uint32_t)bs.m_Buffer.size(), v));
			verify_test(v.m_Items[0].m_Type == Rlp::Node::Type::String && v.m_Items[0].m_Buffer.empty());// == ByteBuffer({ '\0' }));
		}

		//The encoded integer 0 ('\x00') = [0x00]
		{
			auto op = to_opaque("\0");
			Rlp::Node n(op);
			ByteStream bs;
			n.Write(bs);
			verify_test(bs.m_Buffer == ByteBuffer({ 0x00 }));

			RlpVisitor v;
			verify_test(Rlp::Decode(bs.m_Buffer.data(), (uint32_t)bs.m_Buffer.size(), v));
			verify_test(v.m_Items[0].m_Type == Rlp::Node::Type::String && v.m_Items[0].m_Buffer == ByteBuffer({ 0x00 }));
		}

		//The encoded integer 15 ('\x0f') = [0x0f]
		{
			auto op = to_opaque("\x0f");
			Rlp::Node n(op);
			ByteStream bs;
			n.Write(bs);
			verify_test(bs.m_Buffer == ByteBuffer({ 0x0f }));

			RlpVisitor v;
			verify_test(Rlp::Decode(bs.m_Buffer.data(), (uint32_t)bs.m_Buffer.size(), v));
			verify_test(v.m_Items[0].m_Type == Rlp::Node::Type::String && v.m_Items[0].m_Buffer == ByteBuffer({ 0x0f }));
		}

		//The encoded integer 1024 ('\x04\x00') = [0x82, 0x04, 0x00]
		{
			auto op = to_opaque("\x04\x0");
			Rlp::Node n(op);
			ByteStream bs;
			n.Write(bs);
			verify_test(bs.m_Buffer == ByteBuffer({ 0x82, 0x04, 0x00 }));

			RlpVisitor v;
			verify_test(Rlp::Decode(bs.m_Buffer.data(), (uint32_t)bs.m_Buffer.size(), v));
			verify_test(v.m_Items[0].m_Type == Rlp::Node::Type::String && v.m_Items[0].m_Buffer == ByteBuffer({ 0x04, 0x00 }));
		}

		//The set theoretical representation of three, [[], [[]], [[], [[]] ] ] = [0xc7, 0xc0, 0xc1, 0xc0, 0xc3, 0xc0, 0xc1, 0xc0]
		{
			Rlp::Node n1[1] = { createEmptyList() };
			Rlp::Node n2[2] = { createEmptyList(), Rlp::Node(n1) };
			Rlp::Node n3[3] = { createEmptyList(), Rlp::Node(n1), Rlp::Node(n2)};
			Rlp::Node root(n3);
			ByteStream bs;
			root.Write(bs);
			verify_test(bs.m_Buffer == ByteBuffer({ 0xc7, 0xc0, 0xc1, 0xc0, 0xc3, 0xc0, 0xc1, 0xc0 }));

			RlpVisitor v;
			verify_test(Rlp::Decode(bs.m_Buffer.data(), (uint32_t)bs.m_Buffer.size(), v));
			verify_test(v.m_Items[0].m_Type == Rlp::Node::Type::List && v.m_Items[0].m_Buffer.size() == 7);
			RlpVisitor v2;
			verify_test(Rlp::Decode(v.m_Items[0].m_Buffer.data(), (uint32_t)v.m_Items[0].m_Buffer.size(), v2));

			verify_test(v2.m_Items.size() == 3);
			verify_test(v2.m_Items[0].m_Type == Rlp::Node::Type::List);
			verify_test(v2.m_Items[1].m_Type == Rlp::Node::Type::List);
			verify_test(v2.m_Items[2].m_Type == Rlp::Node::Type::List);

			RlpVisitor v3;
			verify_test(Rlp::Decode(v2.m_Items[1].m_Buffer.data(), (uint32_t)v2.m_Items[1].m_Buffer.size(), v3));
			verify_test(v3.m_Items.size() == 1 && v3.m_Items[0].m_Type == Rlp::Node::Type::List);

			RlpVisitor v4;
			verify_test(Rlp::Decode(v2.m_Items[2].m_Buffer.data(), (uint32_t)v2.m_Items[2].m_Buffer.size(), v4));
			verify_test(v4.m_Items.size() == 2 && v4.m_Items[0].m_Type == Rlp::Node::Type::List && v4.m_Items[1].m_Type == Rlp::Node::Type::List);
		}

		//The string 'Lorem ipsum dolor sit amet, consectetur adipisicing elit' = [0xb8, 0x38, 'L', 'o', 'r', 'e', 'm', ' ', ..., 'e', 'l', 'i', 't']
		{
			auto op = to_opaque("Lorem ipsum dolor sit amet, consectetur adipisicing elit");
			Rlp::Node n(op);
			ByteStream bs;
			n.Write(bs);
			verify_test(bs.m_Buffer == ByteBuffer({ 0xb8, 0x38, 'L', 'o', 'r', 'e', 'm', ' ', 'i', 'p', 's', 'u', 'm', ' ', 'd', 'o', 'l', 'o', 'r', ' ', 's', 'i', 't', ' ', 'a', 'm', 'e', 't', ',', ' ', 'c', 'o', 'n', 's', 'e', 'c', 't', 'e', 't', 'u', 'r', ' ', 'a', 'd', 'i', 'p', 'i', 's', 'i', 'c', 'i', 'n', 'g', ' ',  'e', 'l', 'i', 't' }));

			RlpVisitor v;
			verify_test(Rlp::Decode(bs.m_Buffer.data(), (uint32_t)bs.m_Buffer.size(), v));
			verify_test(v.m_Items[0].m_Type == Rlp::Node::Type::String && v.m_Items[0].m_Buffer == ByteBuffer({ 'L', 'o', 'r', 'e', 'm', ' ', 'i', 'p', 's', 'u', 'm', ' ', 'd', 'o', 'l', 'o', 'r', ' ', 's', 'i', 't', ' ', 'a', 'm', 'e', 't', ',', ' ', 'c', 'o', 'n', 's', 'e', 'c', 't', 'e', 't', 'u', 'r', ' ', 'a', 'd', 'i', 'p', 'i', 's', 'i', 'c', 'i', 'n', 'g', ' ',  'e', 'l', 'i', 't' }));

		}
	}
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
		TestRLP();

		MyProcessor proc;

		{

			// const char szPathData[] = "S:\\Beam\\Data\\EthEpoch\\";

			// 1. Create local data for all the epochs (VERY long)

/*
			ExecutorMT_R exec;

			for (uint32_t iEpoch = 0; iEpoch < 1024; iEpoch++)
			{
				struct MyTask :public Executor::TaskAsync
				{
					uint32_t m_iEpoch;

					void Exec(Executor::Context&) override
					{
						std::string sPath = szPathData + std::to_string(m_iEpoch);

						//+"-3.bin"

						beam::EthashUtils::GenerateLocalData(m_iEpoch, (sPath + ".cache").c_str(), (sPath + ".tre3").c_str(), 3); // skip 1st 3 levels, size reduction of 2^3 == 8

						beam::EthashUtils::CropLocalData((sPath + ".tre5").c_str(), (sPath + ".tre3").c_str(), 2); // skip 2 more levels
					}
				};

				auto pTask = std::make_unique<MyTask>();
				pTask->m_iEpoch = iEpoch;
				exec.Push(std::move(pTask));
			}
			exec.Flush(0);
*/

			// 2. Generate the 'SuperTree'
			//beam::EthashUtils::GenerateSuperTree((std::string(szPathData) + "Super.tre").c_str(), szPathData, szPathData, 3);


			// eth block number 12496979
			auto& hdr = proc.m_Eth.m_Header;
			hdr.m_ParentHash.Scan("7a4bf8dc58922f2f8814399542abb379d0b0cc295687f3d5c32e0ce0b9005e3d");
			hdr.m_UncleHash.Scan("1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347");
			hdr.m_Coinbase.Scan("ea674fdde714fd979de3edf0f56aa9716b898ec8");
			hdr.m_Root.Scan("416e7a6de2f67bdb1321c8a9fda385f91e5cd13ddaf1d7b943321110c38be679");
			hdr.m_TxHash.Scan("127d45c910e002965ca732674236310e0bc2880f05df3d85492550a605867891");
			hdr.m_ReceiptHash.Scan("de44f15c086f97cbc6255359280916a75c7a23785941ebacf98be5ae5554b0e9");
			hdr.m_Bloom.Scan("55a861f3f6165d14d38f269a8823d263888cd00d94984854c7a50081198a2b2f922c356c07949a35d040535a1e400dd11e18838449433926924593ad55e6682b605c64922b02a33efcebf28d14aa5cf30522133c9b48380eee4616fcc2475715dcad428cbf818c8601d6d3002100f9c8566dd46488380603c2c94ff7048c9be4c954cb7fc616ef71b4d147ec40973e4611359fa9af6f5068097a2b67891115a1e7dfa91137b721e25785df8cfd41f7e18bc03ea50a035021c166864a8c00cd255dd004762b87a4f16bc6d2b406db3a1d0358ee036e65797bc55872ce0cea246582fd68118d1a6008e04c9c6c90b27694cc33764c5b4b834884bb7306a073faa1");
			hdr.m_nExtra = hdr.m_Extra.Scan("65746865726d696e652d6575726f70652d6e6f72746831") / 2;
			hdr.m_Difficulty = 0x1ac0292081bbf2;
			hdr.m_Number = 0xbeb053; // height
			hdr.m_GasLimit = 0xe4e157;
			hdr.m_GasUsed = 0xe4c170;
			hdr.m_Time = 0x60ab9baf;
			hdr.m_Nonce = 0x9e2b2184779e0239;

			Shaders::Dummy::Ethash::Hash512 hvSeed;
			hdr.get_SeedForPoW(hvSeed);

			// 3. Generate proof
			/*
			auto iEpoch = hdr.get_Epoch();
			std::string sEpoch = std::to_string(iEpoch);

			proc.m_Eth.m_DatasetCount = beam::EthashUtils::GenerateProof(
				iEpoch,
				(std::string(szPathData) + sEpoch + ".cache").c_str(),
				(std::string(szPathData) + sEpoch + ".tre5").c_str(),
				(std::string(szPathData) + "Super.tre").c_str(),
				hvSeed, proc.m_Eth.m_Proof);
			*/
		}

		proc.TestAll();

		MyManager man(proc.m_Vars);
		man.InitMem();
		man.TestHeap();

		ByteBuffer buf;
		MyProcessor::AddCodeEx(buf, "vault/app.wasm", Processor::Kind::Manager);
		man.m_Code = buf;

		man.RunGuarded(0); // get scheme
		std::cout << man.m_Out.str();
		man.m_Out.str("");

		man.m_Args["role"] = "manager";
		man.m_Args["action"] = "view_accounts";
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
