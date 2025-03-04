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
#include "../../utility/hex.h"
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
#include "../Shaders/oracle2/contract.h"
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
#include "../Shaders/dao-vote/contract.h"
#include "../Shaders/dao-vault/contract.h"
#include "../Shaders/dao-accumulator/contract.h"
#include "../Shaders/aphorize/contract.h"
#include "../Shaders/nephrite/contract.h"
#include "../Shaders/amm/contract.h"
#include "../Shaders/minter/contract.h"

	template <bool bToShader> void Convert(Vault::Request& x) {
		ConvertOrd<bToShader>(x.m_Aid);
		ConvertOrd<bToShader>(x.m_Amount);
	}
	template <bool bToShader> void Convert(Dummy::TestFarCall& x) {
	}
	template <bool bToShader> void Convert(Dummy::TestFarCallFlags& x) {
		ConvertOrd<bToShader>(x.m_Flags);
	}
	template <bool bToShader> void Convert(Dummy::MathTest1& x) {
		ConvertOrd<bToShader>(x.m_Value);
		ConvertOrd<bToShader>(x.m_Rate);
		ConvertOrd<bToShader>(x.m_Factor);
		ConvertOrd<bToShader>(x.m_Try);
		ConvertOrd<bToShader>(x.m_IsOk);
	}
	template <bool bToShader> void Convert(Dummy::MathTest2 x) {
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
	template <bool bToShader> void Convert(Dummy::FindVarTest& x) {
	}
	template <bool bToShader> void Convert(Dummy::TestFloat1& x) {
	}
	template <bool bToShader> void Convert(Dummy::TestFloat2& x) {
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

	template <bool bToShader> void Convert(MultiPrecision::Float& x) {
		ConvertOrd<bToShader>(x.m_Num);
		ConvertOrd<bToShader>((uint32_t&) x.m_Order);
	}

	template <bool bToShader> void Convert(Oracle2::Method::Create& x) {
	}
	template <bool bToShader> void Convert(Oracle2::Method::ProviderAdd& x) {
		ConvertOrd<bToShader>(x.m_ApproveMask);
	}
	template <bool bToShader> void Convert(Oracle2::Method::FeedData& x) {
		ConvertOrd<bToShader>(x.m_iProvider);
		Convert<bToShader>(x.m_Value);
	}
	template <bool bToShader> void Convert(Oracle2::Method::Get& x) {
		Convert<bToShader>(x.m_Value);
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

	template <bool bToShader> void Convert(DaoCore::UpdPosFarming& x) {
		ConvertOrd<bToShader>(x.m_Beam);
		ConvertOrd<bToShader>(x.m_WithdrawBeamX);
	}
	template <bool bToShader> void Convert(DaoCore::GetPreallocated& x) {
		ConvertOrd<bToShader>(x.m_Amount);
	}

	template <bool bToShader> void Convert(DaoVote::Method::Create& x) {
		ConvertOrd<bToShader>(x.m_Cfg.m_Aid);
		ConvertOrd<bToShader>(x.m_Cfg.m_hEpochDuration);
	}
	template <bool bToShader> void Convert(DaoVote::Method::AddProposal& x) {
		ConvertOrd<bToShader>(x.m_TxtLen);
		ConvertOrd<bToShader>(x.m_Data.m_Variants);
	}
	template <bool bToShader> void Convert(DaoVote::Method::AddDividend& x) {
		ConvertOrd<bToShader>(x.m_Val.m_Aid);
		ConvertOrd<bToShader>(x.m_Val.m_Amount);
	}
	template <bool bToShader> void Convert(DaoVote::Method::MoveFunds& x) {
		ConvertOrd<bToShader>(x.m_Amount);
	}
	template <bool bToShader> void Convert(DaoVote::Method::Vote& x) {
		ConvertOrd<bToShader>(x.m_iEpoch);
	}
	template <bool bToShader> void Convert(DaoVote::Method::SetModerator& x) {
	}

	template <bool bToShader> void Convert(DaoVault::Method::Create& x) {
		ConvertOrd<bToShader>(x.m_Upgradable.m_hMinUpgradeDelay);
		ConvertOrd<bToShader>(x.m_Upgradable.m_MinApprovers);
	}
	template <bool bToShader> void Convert(DaoVault::Method::Deposit& x) {
		ConvertOrd<bToShader>(x.m_Amount);
		ConvertOrd<bToShader>(x.m_Aid);
	}

	template <bool bToShader> void Convert(Upgradable3::Method::Control::ScheduleUpgrade& x) {
		ConvertOrd<bToShader>(x.m_ApproveMask);
		ConvertOrd<bToShader>(x.m_SizeShader);
		ConvertOrd<bToShader>(x.m_Next.m_hTarget);
	}
	template <bool bToShader> void Convert(Upgradable3::Method::Control::ExplicitUpgrade& x) {
	}

	template <bool bToShader> void Convert(DaoAccumulator::Method::Create& x) {
		ConvertOrd<bToShader>(x.m_aidBeamX);
		ConvertOrd<bToShader>(x.m_hPrePhaseEnd);
		ConvertOrd<bToShader>(x.m_Upgradable.m_hMinUpgradeDelay);
		ConvertOrd<bToShader>(x.m_Upgradable.m_MinApprovers);
	}
	template <bool bToShader> void Convert(DaoAccumulator::Method::FarmStart& x) {
		ConvertOrd<bToShader>(x.m_ApproveMask);
		ConvertOrd<bToShader>(x.m_aidLpToken);
		ConvertOrd<bToShader>(x.m_FarmBeamX);
		ConvertOrd<bToShader>(x.m_hFarmDuration);
	}
	template <bool bToShader> void Convert(DaoAccumulator::Method::UserLock& x) {
		ConvertOrd<bToShader>(x.m_LpToken);
		ConvertOrd<bToShader>(x.m_hEnd);
		ConvertOrd<bToShader>(x.m_PoolType);
	}


	template <bool bToShader> void Convert(Aphorize::Create& x) {
		ConvertOrd<bToShader>(x.m_Cfg.m_hPeriod);
		ConvertOrd<bToShader>(x.m_Cfg.m_PriceSubmit);
	}
	template <bool bToShader> void Convert(Aphorize::Submit& x) {
		ConvertOrd<bToShader>(x.m_Len);
	}

	template <bool bToShader> void Convert(Nephrite::Method::BaseTx& x) {
		ConvertOrd<bToShader>(x.m_Flow.Tok.m_Val);
		ConvertOrd<bToShader>(x.m_Flow.Col.m_Val);
	}
	template <bool bToShader> void Convert(Nephrite::Method::Create& x) {
		ConvertOrd<bToShader>(x.m_Settings.m_TroveLiquidationReserve);
	}
	template <bool bToShader> void Convert(Nephrite::Method::TroveOpen& x) {
		Convert<bToShader>(Cast::Down<Nephrite::Method::BaseTx>(x));
		ConvertOrd<bToShader>(x.m_Amounts.Tok);
		ConvertOrd<bToShader>(x.m_Amounts.Col);
	}
	template <bool bToShader> void Convert(Nephrite::Method::TroveModify& x) {
		Convert<bToShader>(Cast::Down<Nephrite::Method::BaseTx>(x));
		ConvertOrd<bToShader>(x.m_Amounts.Tok);
		ConvertOrd<bToShader>(x.m_Amounts.Col);
	}
	template <bool bToShader> void Convert(Nephrite::Method::UpdStabPool& x) {
		Convert<bToShader>(Cast::Down<Nephrite::Method::BaseTx>(x));
		ConvertOrd<bToShader>(x.m_NewAmount);
	}
	template <bool bToShader> void Convert(Nephrite::Method::Liquidate& x) {
		ConvertOrd<bToShader>(x.m_Count);
	}
	template <bool bToShader> void Convert(Minter::Method::Base& x) {
		ConvertOrd<bToShader>(x.m_Aid);
	}

	template <bool bToShader> void Convert(Amm::Method::Create& x) {
		ConvertOrd<bToShader>(x.m_Upgradable.m_hMinUpgradeDelay);
		ConvertOrd<bToShader>(x.m_Upgradable.m_MinApprovers);
	}
	template <bool bToShader> void Convert(Amm::Pool::ID& x) {
		ConvertOrd<bToShader>(x.m_Aid1);
		ConvertOrd<bToShader>(x.m_Aid2);
		ConvertOrd<bToShader>(x.m_Fees.m_Kind);
	}
	template <bool bToShader> void Convert(Amm::Method::PoolCreate& x) {
		Convert<bToShader>(x.m_Pid);
	}
	template <bool bToShader> void Convert(Amm::Method::AddLiquidity& x) {
		Convert<bToShader>(x.m_Pid);
		ConvertOrd<bToShader>(x.m_Amounts.m_Tok1);
		ConvertOrd<bToShader>(x.m_Amounts.m_Tok2);
	}
	template <bool bToShader> void Convert(Amm::Method::Withdraw& x) {
		Convert<bToShader>(x.m_Pid);
		ConvertOrd<bToShader>(x.m_Ctl);
	}
	template <bool bToShader> void Convert(Amm::Method::Trade& x) {
		Convert<bToShader>(x.m_Pid);
		ConvertOrd<bToShader>(x.m_Buy1);
	}

	namespace Env {

		typedef beam::bvm2::Limits::Cost Cost;
		typedef Shaders::Env::KeyID KeyID;

		void CallFarN(const ContractID& cid, uint32_t iMethod, void* pArgs, uint32_t nArgs, uint32_t nFlags);

		template <typename T>
		void CallFar_T(const ContractID& cid, T& args, uint32_t nFlags = 0)
		{
			Convert<true>(args);
			CallFarN(cid, args.s_iMethod, &args, sizeof(args), nFlags);
			Convert<false>(args);
		}

	} // namespace Env

//#include "../Shaders/app_common_impl.h"
//#include "../Shaders/app_comm.h"

	namespace Vault {
#include "../Shaders/vault/contract.cpp"
	}
	namespace Oracle {
#include "../Shaders/oracle/contract.cpp"
	}

//#include "../Shaders/oracle2/contract.cpp"

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
	namespace DaoCore {
#include "../Shaders/dao-core/contract.cpp"
	}
	namespace Aphorize {
#include "../Shaders/aphorize/contract.cpp"
	}

//#include "../Shaders/amm/contract.cpp"
//#include "../Shaders/dao-vote/contract.cpp" // already within namespace
//#include "../Shaders/dao-vault/contract.cpp" // already within namespace
//#include "../Shaders/nephrite/contract.cpp" // already within namespace
//#include "../Shaders/nephrite/app.cpp"
//#include "../Shaders/amm/contract.cpp" // already within namespace
//#include "../Shaders/amm/app.cpp"
#include "../Shaders/upgradable2/app_common_impl.h"

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
		struct ShaderWrap
		{
			ByteBuffer m_Code;
			ShaderID m_Sid;
			Wasm::Compiler::DebugInfo m_DbgInfo;
		};

		struct ContractWrap
			:public ShaderWrap
		{
			ContractID m_Cid;
		};

		ContractWrap m_Vault;
		ContractWrap m_Oracle;
		ContractWrap m_Oracle2;
		ContractWrap m_Dummy;
		ContractWrap m_Sidechain;
		ContractWrap m_StableCoin;
		ContractWrap m_Faucet;
		ContractWrap m_Roulette;
		ContractWrap m_Perpetual;
		ContractWrap m_Pipe;
		ContractWrap m_MirrorCoin;
		ContractWrap m_Voting;
		ContractWrap m_DaoCore;
		ContractWrap m_DaoVote;
		ContractWrap m_DaoAccumulator;
		ShaderWrap m_DaoAccumulator_v2;
		ContractWrap m_Aphorize;
		ContractWrap m_DaoVault;
		ContractWrap m_Nephrite;
		ContractWrap m_Minter;
		ContractWrap m_Amm;

		std::map<ShaderID, const Wasm::Compiler::DebugInfo*> m_mapDbgInfo;

		void AddCode(ShaderWrap& cw, const char* sz)
		{
			AddCodeEx(cw.m_Code, sz, Kind::Contract, &cw.m_DbgInfo);
			get_ShaderID(cw.m_Sid, cw.m_Code);

			m_mapDbgInfo[cw.m_Sid] = &cw.m_DbgInfo;
		}

		const Wasm::Compiler::DebugInfo* get_DbgInfo(const ShaderID& sid) const override
		{
			auto it = m_mapDbgInfo.find(sid);
			return (m_mapDbgInfo.end() == it) ? nullptr : it->second;
		}

		ContractID m_cidMirrorCoin2;

		struct {

			Shaders::Eth::Header m_Header;
			uint32_t m_DatasetCount;
			ByteBuffer m_Proof;

		} m_Eth;


		virtual void CallFar(const ContractID& cid, uint32_t iMethod, Wasm::Word pArgs, uint32_t nArgs, uint32_t nFlags) override
		{
			if (cid == m_Vault.m_Cid)
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

			if (cid == m_Oracle.m_Cid)
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
/*
			if (cid == m_Oracle2.m_Cid)
			{
				TempFrame f(*this, cid);
				switch (iMethod)
				{
				case 0: Shaders::Oracle2::Ctor(CastArg<Shaders::Oracle2::Method::Create>(pArgs)); return;
				case 3: Shaders::Oracle2::Method_3(CastArg<Shaders::Oracle2::Method::Get>(pArgs)); return;
				case 4: Shaders::Oracle2::Method_4(CastArg<Shaders::Oracle2::Method::FeedData>(pArgs)); return;
				case 5: Shaders::Oracle2::Method_6(CastArg<Shaders::Oracle2::Method::ProviderAdd>(pArgs)); return;
				}
			}
*/
			if (cid == m_StableCoin.m_Cid)
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

			if (cid == m_Faucet.m_Cid)
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

			if (cid == m_Roulette.m_Cid)
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

			if (cid == m_Dummy.m_Cid)
			{
				//TempFrame f(*this, cid);
				//switch (iMethod)
				//{
				//case 9: Shaders::Dummy::Method_9(CastArg<Shaders::Dummy::VerifyBeamHeader>(pArgs)); return;
				//case 11: Shaders::Dummy::Method_11(CastArg<Shaders::Dummy::TestRingSig>(pArgs)); return;
				//case 12: Shaders::Dummy::Method_12(CastArg<Shaders::Dummy::TestEthHeader>(pArgs)); return;
				//}
			}

			if (cid == m_Sidechain.m_Cid)
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

			if (cid == m_Perpetual.m_Cid)
			{
				//TempFrame f(*this, cid);
				//switch (iMethod)
				//{
				//case 0: Shaders::Perpetual::Ctor(CastArg<Shaders::Perpetual::Create>(pArgs)); return;
				//case 2: Shaders::Perpetual::Method_2(CastArg<Shaders::Perpetual::CreateOffer>(pArgs)); return;
				//case 3: Shaders::Perpetual::Method_3(CastArg<Shaders::Perpetual::CancelOffer>(pArgs)); return;
				//}
			}

			if (cid == m_Pipe.m_Cid)
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

			if ((cid == m_MirrorCoin.m_Cid) || (cid == m_cidMirrorCoin2))
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

			if (cid == m_Voting.m_Cid)
			{
				//TempFrame f(*this, cid);
				//switch (iMethod)
				//{
				//case 2: Shaders::Voting::Method_2(CastArg<Shaders::Voting::OpenProposal>(pArgs)); return;
				//case 3: Shaders::Voting::Method_3(CastArg<Shaders::Voting::Vote>(pArgs)); return;
				//case 4: Shaders::Voting::Method_4(CastArg<Shaders::Voting::Withdraw>(pArgs)); return;
				//}
			}

			if (cid == m_DaoCore.m_Cid)
			{
				//TempFrame f(*this, cid);
				//switch (iMethod)
				//{
				//case 0: Shaders::DaoCore::Ctor(nullptr); return;
				//case 3: Shaders::DaoCore::Method_3(CastArg<Shaders::DaoCore::GetPreallocated>(pArgs)); return;
				//case 4: Shaders::DaoCore::Method_4(CastArg<Shaders::DaoCore::UpdPosFarming>(pArgs)); return;
				//}
			}

			//if (cid == m_DaoVote.m_Cid)
			//{
			//	TempFrame f(*this, cid);
			//	switch (iMethod)
			//	{
			//	case 0: Shaders::DaoVote::Ctor(CastArg<Shaders::DaoVote::Method::Create>(pArgs)); return;
			//	case 3: Shaders::DaoVote::Method_3(CastArg<Shaders::DaoVote::Method::AddProposal>(pArgs)); return;
			//	case 4: Shaders::DaoVote::Method_4(CastArg<Shaders::DaoVote::Method::MoveFunds>(pArgs)); return;
			//	case 5: Shaders::DaoVote::Method_5(CastArg<Shaders::DaoVote::Method::Vote>(pArgs)); return;
			//	case 6: Shaders::DaoVote::Method_6(CastArg<Shaders::DaoVote::Method::AddDividend>(pArgs)); return;
			//	}
			//}

			if (cid == m_Aphorize.m_Cid)
			{
				//TempFrame f(*this, cid);
				//switch (iMethod)
				//{
				//case 0: Shaders::Aphorize::Ctor(CastArg<Shaders::Aphorize::Create>(pArgs)); return;
				//case 2: Shaders::Aphorize::Method_2(CastArg<Shaders::Aphorize::Submit>(pArgs)); return;
				//}
			}
/*
			if (cid == m_Nephrite.m_Cid)
			{
				TempFrame f(*this, cid);
				switch (iMethod)
				{
				case 0: Shaders::Nephrite::Ctor(CastArg<Shaders::Nephrite::Method::Create>(pArgs)); return;
				case 3: Shaders::Nephrite::Method_3(CastArg<Shaders::Nephrite::Method::TroveOpen>(pArgs)); return;
				case 4: Shaders::Nephrite::Method_4(CastArg<Shaders::Nephrite::Method::TroveClose>(pArgs)); return;
				case 7: Shaders::Nephrite::Method_7(CastArg<Shaders::Nephrite::Method::UpdStabPool>(pArgs)); return;
				case 8: Shaders::Nephrite::Method_8(CastArg<Shaders::Nephrite::Method::Liquidate>(pArgs)); return;
				case 9: Shaders::Nephrite::Method_9(CastArg<Shaders::Nephrite::Method::Redeem>(pArgs)); return;
				}
			}
*/
/*
			if (cid == m_Amm.m_Cid)
			{
				TempFrame f(*this, cid);
				switch (iMethod)
				{
				////case 0: Shaders::Amm::Ctor(CastArg<Shaders::Amm::Method::Create>(pArgs)); return;
				////case 3: Shaders::Amm::Method_3(CastArg<Shaders::Amm::Method::PoolCreate>(pArgs)); return;
				////case 4: Shaders::Amm::Method_4(CastArg<Shaders::Amm::Method::PoolDestroy>(pArgs)); return;
				////case 5: Shaders::Amm::Method_5(CastArg<Shaders::Amm::Method::AddLiquidity>(pArgs)); return;
				//case 6: Shaders::Amm::Method_6(CastArg<Shaders::Amm::Method::Withdraw>(pArgs)); return;
				////case 7: Shaders::Amm::Method_7(CastArg<Shaders::Amm::Method::Trade>(pArgs)); return;
				}
			}
*/
/*
			if (cid == m_Amm.m_Cid)
			{
				TempFrame f(*this, cid);
				switch (iMethod)
				{
				}
			}
*/
			ProcessorContract::CallFar(cid, iMethod, pArgs, nArgs, nFlags);
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
		void TestDaoCore();
		void TestDaoVote();
		void TestDaoAccumulator();
		void TestAphorize();
		void TestNephrite();
		void TestMinter();
		void TestAmm();

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


	struct MyManager
		:public ProcessorManager
	{
		MyProcessor& m_Proc;
		std::ostringstream m_Out;
		uint32_t m_Charge;
		bool m_LogIO = false;

		ByteBuffer m_bufCode;
		Wasm::Compiler::DebugInfo m_DbgInfo;

		MyManager(MyProcessor& proc)
			:m_Proc(proc)
		{
			m_pOut = &m_Out;
		}

		void SetCode(const char* szPath)
		{
			MyProcessor::AddCodeEx(m_bufCode, szPath, Kind::Manager, &m_DbgInfo);
			m_Debug = true;
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

		void SelectContext(bool /* bDependent */, uint32_t /* nChargeNeeded */) override
		{
			m_Context.m_Height = m_Proc.m_Height;
		}

		virtual void VarsEnum(const Blob& kMin, const Blob& kMax, IReadVars::Ptr& pRes) override
		{
			auto p = std::make_unique<VarEnumCtx>();

			ZeroObject(p->m_LastKey);
			ZeroObject(p->m_LastVal);

			p->m_it = m_Proc.m_Vars.lower_bound(kMin, BlobMap::Set::Comparator());
			p->m_itEnd = m_Proc.m_Vars.upper_bound(kMax, BlobMap::Set::Comparator());

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
			TemporarySwap ts(m_LogIO, m_Proc.m_LogIO);

			ResetBase();
			m_Code = m_bufCode;

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

				std::cout << m_Out.str() << std::endl;
				m_Out.str("");

			}
			catch (const std::exception& e) {
				std::cout << "*** Shader Execution failed. Undoing changes" << std::endl;
				std::cout << e.what() << std::endl;
				ret = false;

				DumpCallstack(std::cout, &m_DbgInfo);
			}
			return ret;
		}

		bool RunGuardedEx(void* pArgs, uint32_t nArgs)
		{
			if (!RunGuarded(1))
				return false;

			auto vInv = std::move(m_InvokeData.m_vec);

			if (vInv.size() != 1)
				return false;

			auto& x = vInv.front();
			if (x.m_Args.size() != nArgs)
				return false;

			m_Charge = x.m_Charge;

			memcpy(pArgs, &x.m_Args.front(), nArgs);
			return true;
		}

		template <typename TMethod>
		bool RunGuarded_T(TMethod& ret)
		{
			return RunGuardedEx(&ret, sizeof(ret));
		}
	};

	struct CidTxt
	{
		char m_szBuf[Shaders::ContractID::nBytes * 5];

		CidTxt() {}
		CidTxt(const Shaders::ContractID& sid) { Set(sid); }

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

	void SaveAsHex(const char* szFilePath, const Blob& b)
	{
		std::FStream fs;
		fs.Open(szFilePath, false, true);

		char szBuf[0x10];
		szBuf[0] = '0';
		szBuf[1] = 'x';
		szBuf[5] = '\n';

		for (uint32_t i = 0; i < b.n; )
		{
			uintBigImpl::_Print(reinterpret_cast<const uint8_t*>(b.p) + i, 1, szBuf + 2);
			szBuf[4] = ',';
			uint32_t nLen = 5;

			if (!(++i & 0x1f))
				nLen = 6;

			fs.write(szBuf, nLen);
		}
	}

	void MyProcessor::TestAll()
	{
		AddCode(m_Vault, "vault/contract.wasm");
		AddCode(m_Dummy, "dummy/contract.wasm");
		AddCode(m_Oracle, "oracle/contract.wasm");
		AddCode(m_Oracle2, "oracle2/contract.wasm");
		AddCode(m_StableCoin, "StableCoin/contract.wasm");
		AddCode(m_Faucet, "faucet/contract.wasm");
		AddCode(m_Roulette, "roulette/contract.wasm");
		AddCode(m_Sidechain, "sidechain/contract.wasm");
		AddCode(m_Perpetual, "perpetual/contract.wasm");
		AddCode(m_Pipe, "pipe/contract.wasm");
		AddCode(m_MirrorCoin, "mirrorcoin/contract.wasm");
		AddCode(m_Voting, "voting/contract.wasm");
		AddCode(m_DaoCore, "dao-core/contract.wasm");
		AddCode(m_DaoVote, "dao-vote/contract.wasm");
		AddCode(m_DaoAccumulator, "dao-accumulator/contract.wasm");
		AddCode(m_DaoAccumulator_v2, "dao-accumulator/contract_v2.wasm");
		AddCode(m_Aphorize, "aphorize/contract.wasm");
		AddCode(m_DaoVault, "dao-vault/contract.wasm");
		AddCode(m_Nephrite, "nephrite/contract.wasm");
		AddCode(m_Minter, "minter/contract.wasm");
		AddCode(m_Amm, "amm/contract.wasm");

		m_FarCalls.m_SaveLocal = true;

		TestVault();
		TestAphorize();
		TestNephrite();
		TestMinter();
		TestAmm();
		TestFaucet();
		TestRoulette();
		TestVoting();
		TestDaoCore();
		TestDaoVote();
		TestDaoAccumulator();
		TestDummy();
		TestSidechain();
		TestOracle();
		TestStableCoin();
		TestPerpetual();
		TestPipe();
		TestMirrorCoin();
	}

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
		verify_test(ContractCreate_T(m_Vault.m_Cid, m_Vault.m_Code, zero));
		VERIFY_ID(Shaders::Vault::s_CID, m_Vault.m_Cid);
		VERIFY_ID(Shaders::Vault::s_SID, m_Vault.m_Sid);

		m_lstUndo.Clear();

		Shaders::Vault::Request args;

		ECC::Scalar::Native k;
		ECC::SetRandom(k);
		ECC::Point::Native pt = ECC::Context::get().G * k;

		args.m_Account = pt;
		args.m_Aid = 3;
		args.m_Amount = 45;

		verify_test(RunGuarded_T(m_Vault.m_Cid, Shaders::Vault::Deposit::s_iMethod, args));

		args.m_Amount = 46;
		verify_test(!RunGuarded_T(m_Vault.m_Cid, Shaders::Vault::Withdraw::s_iMethod, args)); // too much withdrawn

		args.m_Aid = 0;
		args.m_Amount = 43;
		verify_test(!RunGuarded_T(m_Vault.m_Cid, Shaders::Vault::Withdraw::s_iMethod, args)); // wrong asset

		args.m_Aid = 3;
		verify_test(RunGuarded_T(m_Vault.m_Cid, Shaders::Vault::Withdraw::s_iMethod, args)); // ok

		args.m_Amount = 2;
		verify_test(RunGuarded_T(m_Vault.m_Cid, Shaders::Vault::Withdraw::s_iMethod, args)); // ok, pos terminated

		args.m_Amount = 0xdead2badcadebabeULL;

		verify_test(RunGuarded_T(m_Vault.m_Cid, Shaders::Vault::Deposit::s_iMethod, args)); // huge amount, should work
		verify_test(!RunGuarded_T(m_Vault.m_Cid, Shaders::Vault::Deposit::s_iMethod, args)); // would overflow

		UndoChanges(); // up to (but not including) contract creation

		// create several accounts with different assets
		args.m_Amount = 400000;
		args.m_Aid = 0;
		verify_test(RunGuarded_T(m_Vault.m_Cid, Shaders::Vault::Deposit::s_iMethod, args));

		args.m_Amount = 300000;
		args.m_Aid = 2;
		verify_test(RunGuarded_T(m_Vault.m_Cid, Shaders::Vault::Deposit::s_iMethod, args));

		pt = pt * ECC::Two;
		args.m_Account = pt;

		args.m_Amount = 700000;
		args.m_Aid = 0;
		verify_test(RunGuarded_T(m_Vault.m_Cid, Shaders::Vault::Deposit::s_iMethod, args));

		args.m_Amount = 500000;
		args.m_Aid = 6;
		verify_test(RunGuarded_T(m_Vault.m_Cid, Shaders::Vault::Deposit::s_iMethod, args));

		m_lstUndo.Clear();
	}

	void MyProcessor::TestAphorize()
	{
		{
			Shaders::Aphorize::Create args;
			args.m_Cfg.m_hPeriod = 10;
			args.m_Cfg.m_PriceSubmit = Shaders::g_Beam2Groth * 2;
			ZeroObject(args.m_Cfg.m_Moderator);
			verify_test(ContractCreate_T(m_Aphorize.m_Cid, m_Aphorize.m_Code, args));
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

			verify_test(RunGuarded_T(m_Aphorize.m_Cid, args.s_iMethod, args));
		}
	}

	double Val2Num(Amount x)
	{
		double res = static_cast<double>(x);
		return res * 1e-8;
	}

	static double ToDouble(Shaders::MultiPrecision::Float x)
	{
		if (x.IsZero())
			return 0;

		return ldexp(x.m_Num, x.m_Order);
	}

	static double ToDouble(Shaders::MultiPrecision::FloatEx x)
	{
		if (x.IsZero())
			return 0;
		if (x.IsNaN())
			return NAN;

		auto num = (double) x.get_Num();
		if (x.IsNegative())
			num = -num;
		return ldexp(num, x.get_Order());
	}

	static void AssertDoubleRelEqual(double a, double b)
	{
		if (a == 0.)
		{
			verify_test(b == 0.);
		}
		else
		{
			verify_test(isnormal(a) && isnormal(b));

			a /= b;
			verify_test(isnormal(a));

			// double floating-point mantissa is 52 bits, our implementation has 63 bits
			// we should have at least 50 bits of precision
			const double eps = ldexp(1, -50);

			verify_test(a >= 1. - eps);
			verify_test(a <= 1. + eps);
		}
	}

	struct NephriteContext
	{
		MyProcessor& m_Proc;
		NephriteContext(MyProcessor& proc) :m_Proc(proc) {}

		typedef Shaders::Nephrite::Balance Balance;
		typedef Shaders::Nephrite::Pair Pair;

		static double Flow2Num(Shaders::Nephrite::Flow f)
		{
			double res = Val2Num(f.m_Val);
			return f.m_Spend ? -res : res;
		}

		Pair get_Balance(const PubKey& pk)
		{
			Shaders::Env::Key_T<Balance::Key> key;
			key.m_Prefix.m_Cid = m_Proc.m_Nephrite.m_Cid;
			key.m_KeyInContract.m_Pk = pk;

			Blob b;
			m_Proc.LoadVar(Blob(&key, sizeof(key)), b);

			if (sizeof(Balance) == b.n)
				return reinterpret_cast<const Balance*>(b.p)->m_Amounts;

			Pair vals;
			ZeroObject(vals);
			return vals;
		}

		struct KeyWalker
		{
			MyProcessor& m_Proc;
			BlobMap::Set::iterator m_it;
			uint8_t m_Tag;
			Blob m_Data;

			KeyWalker(MyProcessor& proc, const ContractID& cid, uint8_t nTag)
				:m_Proc(proc)
				,m_Tag(nTag)
			{
				Shaders::Env::Key_T<uint8_t> key;
				key.m_Prefix.m_Cid = cid;
				key.m_KeyInContract = nTag;
				m_it = m_Proc.m_Vars.lower_bound(Blob(&key, sizeof(key)), BlobMap::Set::Comparator());
			}

			template <typename T>
			static T* FromBlob(const Blob& x) {
				return (x.n >= sizeof(T)) ? reinterpret_cast<T*>(Cast::NotConst(x.p)) : nullptr;
			}

			template <typename TKey, typename TValue>
			bool MoveNext_T(TKey*& pKey, TValue*& pVal)
			{
				if (m_Proc.m_Vars.end() == m_it)
					return false;

				pKey = FromBlob<TKey>(m_it->ToBlob());
				if (!pKey || (pKey->m_KeyInContract.m_Tag != m_Tag))
					return false;

				m_Data = m_it->m_Data;
				pVal = FromBlob<TValue>(m_Data);
				if (!pVal)
					return false;

				m_it++;
				return true;
			}
		};

		template <typename TKey, typename TValue>
		struct KeyWalker_T
			:public KeyWalker
		{
			using KeyWalker::KeyWalker;

			Shaders::Env::Key_T<TKey>* m_pKey;
			TValue* m_pVal;

			bool MoveNext() {
				return KeyWalker::MoveNext_T(m_pKey, m_pVal);
			}
		};

		void PrintBankExcess()
		{
			//bool bDelPrev = false;
			for (KeyWalker_T<Balance::Key, Balance> wlk(m_Proc, m_Proc.m_Nephrite.m_Cid, Shaders::Nephrite::Tags::s_Balance); ; )
			{
				//auto it = wlk.m_it;
				bool bNext = wlk.MoveNext();

				//if (bDelPrev)
				//{
				//	bDelPrev = false;
				//	m_Proc.m_Vars.Delete(*it);
				//}

				if (!bNext)
					break;

				const auto& bal = *wlk.m_pVal;
				verify_test(bal.m_Amounts.Tok || bal.m_Amounts.Col || bal.m_Gov);

				std::cout << "\tUser=" << wlk.m_pKey->m_KeyInContract.m_Pk
					<< ", Tok=" << Val2Num(bal.m_Amounts.Tok)
					<< ", Col=" << Val2Num(bal.m_Amounts.Col)
					<< std::endl;

				//bDelPrev = true;
			}
		}

		//Balance ReadBalance(const Shaders::Env::Key_T<Balance::Key>& key)
		//{
		//	Blob blVal;
		//	m_Proc.LoadVar(Blob(&key, sizeof(key)), blVal);
		//	if (sizeof(Balance) == blVal.n)
		//		return *Cast::Reinterpret<Balance*>(blVal.p);

		//	Balance x;
		//	ZeroObject(x);
		//	return x;
		//}

		//void SetBalance(const Shaders::Env::Key_T<Balance::Key>& key, const Balance& x)
		//{
		//	if (memis0(&x, sizeof(x)))
		//		m_Proc.SaveVar(Blob(&key, sizeof(key)), Blob(nullptr, 0));
		//	else
		//		m_Proc.SaveVar(Blob(&key, sizeof(key)), Blob(&x, sizeof(x)));
		//}

		bool InvokeBase(Shaders::Nephrite::Method::BaseTx& args, uint32_t nSizeArgs, uint32_t iMethod, const PubKey& pkUser, uint32_t nChargeEst, bool bShouldUseVault)
		{
			bool bLogIO = false;
			TemporarySwap ts(m_Proc.m_LogIO, bLogIO);

			Shaders::Env::Key_T<Balance::Key> key;
			key.m_Prefix.m_Cid = m_Proc.m_Nephrite.m_Cid;
			key.m_KeyInContract.m_Pk = pkUser;

			//Balance ub0 = ReadBalance(key);

			std::cout << "\tUser=" << pkUser
				<< ", Tok=" << Flow2Num(args.m_Flow.Tok)
				<< ", Col=" << Flow2Num(args.m_Flow.Col)
				<< std::endl;

			// do not add anything, test the contract locked amounts are ok.
			// Change the following lines to test the bank usage instead
			const Amount valTok = 0;
			const Amount valCol = 0;

			args.m_Flow.Tok.Add(valTok, 1);
			args.m_Flow.Col.Add(valCol, 1);

			{
				TemporarySwap ts2(m_Proc.m_LogIO, bLogIO);
				if (!m_Proc.RunGuarded(m_Proc.m_Nephrite.m_Cid, iMethod, Blob(&args, nSizeArgs), nullptr))
					return false;
			}

			std::cout << "Estimated charge: " << nChargeEst << std::endl;
			verify_test(nChargeEst >= Limits::BlockCharge - m_Proc.m_Charge);

			//// verify the init-guess
			//Balance ub1 = ReadBalance(key);
			//verify_test(ub1.m_Amounts.Tok == ub0.m_Amounts.Tok + valTok);
			//verify_test(ub1.m_Amounts.Col == ub0.m_Amounts.Col + valCol);
			//SetBalance(key, ub0);

			PrintBankExcess();
			return true;
		}

		template <typename TMethod>
		bool InvokeTx(TMethod& args, const PubKey& pkUser, uint32_t nChargeEst, bool bShouldUseVault = true)
		{
			return InvokeBase(args, sizeof(args), args.s_iMethod, pkUser, nChargeEst, bShouldUseVault);
		}

		template <typename TMethod>
		bool InvokeTxUser(TMethod& args, uint32_t nChargeEst, bool bShouldUseVault = true)
		{
			return InvokeBase(args, sizeof(args), args.s_iMethod, args.m_pkUser, nChargeEst, bShouldUseVault);
		}

		template <uint32_t nDims>
		struct EpochStorage
		{
			typedef Shaders::HomogenousPool::Epoch<nDims> MyEpoch;
			std::map<uint32_t, MyEpoch> m_Data;

			static void AddTotals1(Amount& vSell, Amount& vBuy, const MyEpoch& e)
			{
				vSell += e.m_Sell;
				vBuy += e.m_pDim[0].m_Buy;
			}

			void AddTotals(Amount& vSell, Amount& vBuy) const
			{
				for (const auto& x : m_Data)
					AddTotals1(vSell, vBuy, x.second);
			}

			void Load(uint32_t iEpoch, MyEpoch& res)
			{
				auto it = m_Data.find(iEpoch);
				verify_test(m_Data.end() != it);
				res = it->second;
			}

			void Save(uint32_t iEpoch, const MyEpoch& res)
			{
			}

			void Del(uint32_t iEpoch)
			{
			}
		};

		template <uint32_t nDims>
		void LoadEpochs(EpochStorage<nDims>& res, uint8_t nTag)
		{
			for (KeyWalker_T<Shaders::Nephrite::EpochKey, Shaders::HomogenousPool::Epoch<nDims> > wlk(m_Proc, m_Proc.m_Nephrite.m_Cid, nTag); wlk.MoveNext(); )
				res.m_Data[wlk.m_pKey->m_KeyInContract.m_iEpoch] = *wlk.m_pVal;
		}

		struct Entry :public Shaders::Nephrite::Trove
		{
			Entry()
			{
				ZeroObject(*this);
			}

			uint32_t m_iTrove;
			Shaders::Nephrite::Float m_Rcr;
		};

		std::vector<Entry> m_Troves;
		uint32_t m_ActiveTroves = 0;
		uint32_t m_iHeadTrove = 0;

		void PrintAll()
		{
			bool bLogIO = false;
			TemporarySwap ts(m_Proc.m_LogIO, bLogIO);

			Shaders::Nephrite::Global g;
			{
				Shaders::Env::Key_T<uint8_t> key;
				key.m_Prefix.m_Cid = m_Proc.m_Nephrite.m_Cid;
				key.m_KeyInContract = Shaders::Nephrite::Tags::s_State;

				Blob b;
				m_Proc.LoadVar(Blob(&key, sizeof(key)), b);
				verify_test(sizeof(g) == b.n);
				memcpy(&g, b.p, sizeof(g));
			}

			g.m_BaseRate.Decay(m_Proc.m_Height);

			Pair totalStab, totalRedist;

			EpochStorage<2> storStab;
			LoadEpochs(storStab, Shaders::Nephrite::Tags::s_Epoch_Stable);

			ZeroObject(totalStab);
			storStab.AddTotals1(totalStab.Tok, totalStab.Col, g.m_StabPool.m_Active);
			storStab.AddTotals1(totalStab.Tok, totalStab.Col, g.m_StabPool.m_Draining);
			storStab.AddTotals(totalStab.Tok, totalStab.Col);

			EpochStorage<1> storRedist;
			LoadEpochs(storRedist, Shaders::Nephrite::Tags::s_Epoch_Redist);

			ZeroObject(totalRedist);
			storRedist.AddTotals1(totalRedist.Tok, totalRedist.Col, g.m_RedistPool.m_Active);
			storRedist.AddTotals1(totalRedist.Tok, totalRedist.Col, g.m_RedistPool.m_Draining);
			storRedist.AddTotals(totalRedist.Tok, totalRedist.Col);

			Shaders::Nephrite::Global::Price price;
			{
				Shaders::Env::Key_T<uint8_t> key;
				key.m_Prefix.m_Cid = g.m_Settings.m_cidOracle1;
				key.m_KeyInContract = Shaders::Oracle2::Tags::s_Median;

				Blob b;
				m_Proc.LoadVar(Blob(&key, sizeof(key)), b);
				verify_test(sizeof(Shaders::Oracle2::Median) == b.n);
				price.m_Value = ((Shaders::Oracle2::Median*) b.p)->m_Res;
			}

			std::cout << "Totals Tok=" << Val2Num(g.m_Troves.m_Totals.Tok) << ", Col=" << Val2Num(g.m_Troves.m_Totals.Col) << std::endl;
			std::cout << "TCR = " << (ToDouble(price.ToCR(g.m_Troves.m_Totals.get_Rcr())) * 100.) << "" << std::endl;
			std::cout << "kRate = " << ToDouble(g.m_BaseRate.m_k) * 100. << "%" << std::endl;

			{
				Shaders::Env::Key_T<uintBigFor<AssetID>::Type> key;
				key.m_Prefix.m_Cid = g.m_Settings.m_cidDaoVault;
				key.m_Prefix.m_Tag = Shaders::KeyTag::LockedAmount;
				Blob b;
				Amount valCol = 0, valTok = 0;

				key.m_KeyInContract = 0u;
				m_Proc.LoadVar(Blob(&key, sizeof(key)), b);

				if (sizeof(AmountBig::Type) == b.n)
				{
					auto val = ((const uintBig_t<AmountBig::Number::nSize>*) b.p)->ToNumber();
					valCol = AmountBig::get_Lo(val);
				}

				key.m_KeyInContract = g.m_Aid;
				m_Proc.LoadVar(Blob(&key, sizeof(key)), b);

				if (sizeof(AmountBig::Type) == b.n)
				{
					auto val = ((const uintBig_t<AmountBig::Number::nSize>*) b.p)->ToNumber();
					valTok = AmountBig::get_Lo(val);
				}

				std::cout << "Dao-vault , Col=" << Val2Num(valCol) << ", Tok=" << Val2Num(valTok) << std::endl;
			}

			verify_test(g.m_Troves.m_Totals.Tok == totalRedist.Tok); // all troves must participate in the RedistPool

			auto itAsset = m_Proc.m_Assets.find(g.m_Aid);
			verify_test(m_Proc.m_Assets.end() != itAsset);
			verify_test(itAsset->second.m_Amount == g.m_Troves.m_Totals.Tok); // total debt must be equal minted amount

			Amount totalCol = totalRedist.Col;

			Pair valsTroves = { {0} };

			m_Troves.clear();
			m_Troves.resize(g.m_Troves.m_iLastCreated);

			m_ActiveTroves = 0;
			for (KeyWalker_T<Shaders::Nephrite::Trove::Key, Shaders::Nephrite::Trove> wlk(m_Proc, m_Proc.m_Nephrite.m_Cid, Shaders::Nephrite::Tags::s_Trove); wlk.MoveNext(); )
			{
				auto iTrove = wlk.m_pKey->m_KeyInContract.m_iTrove; // not stored in BE form
				verify_test(iTrove <= g.m_Troves.m_iLastCreated);

				auto& x = m_Troves[iTrove - 1];
				Cast::Down<Shaders::Nephrite::Trove>(x) = *wlk.m_pVal;

				totalCol += x.m_Amounts.Col; // before accounting for redist

				x.m_Amounts = g.m_RedistPool.get_UpdatedAmounts(x, storRedist);
				x.m_Rcr = x.m_Amounts.get_Rcr();

				valsTroves.Tok += x.m_Amounts.Tok;
				valsTroves.Col += x.m_Amounts.Col;

				m_ActiveTroves++;
			}

			verify_test(g.m_Troves.m_Totals.Col == totalCol);

			// check troves order
			Shaders::Nephrite::Float rcrPrev;
			rcrPrev.Set0();
			uint32_t nCount = 0;
			for (uint32_t iTrove = g.m_Troves.m_iHead; iTrove; nCount++)
			{
				verify_test(iTrove <= g.m_Troves.m_iLastCreated);
				auto& x = m_Troves[iTrove - 1];

				verify_test(!x.m_iTrove); // ensure no loops
				x.m_iTrove = iTrove;

				std::cout << "\tiTrove=" << x.m_iTrove <<  ", Tok=" << Val2Num(x.m_Amounts.Tok) << ", Col=" << Val2Num(x.m_Amounts.Col)
					<< ", CR = " << (ToDouble(price.ToCR(x.m_Rcr)) * 100.) << "" << std::endl;

				verify_test(rcrPrev <= x.m_Rcr);
				rcrPrev = x.m_Rcr;

				iTrove = x.m_iNext;
			}
			verify_test(nCount == m_ActiveTroves);
			m_iHeadTrove = g.m_Troves.m_iHead;

			// Stabpool positions
			Pair valsStabs = { {0} };

			for (KeyWalker_T<Shaders::Nephrite::StabPoolEntry::Key, Shaders::Nephrite::StabPoolEntry> wlk(m_Proc, m_Proc.m_Nephrite.m_Cid, Shaders::Nephrite::Tags::s_StabPool); wlk.MoveNext(); )
			{
				Shaders::Nephrite::ExchangePool::User::Out out;
				g.m_StabPool.UserDel<true, false>(wlk.m_pVal->m_User, out, 0, storStab);

				std::cout << "\tStab User=" << wlk.m_pKey->m_KeyInContract.m_pkUser
					<< ", Tok=" << Val2Num(out.m_Sell)
					<< ", Col=" << Val2Num(out.m_pBuy[0])
					<< std::endl;

				valsStabs.Tok += out.m_Sell;
				valsStabs.Col += out.m_pBuy[0];
			}

			int64_t dTok = g.m_Troves.m_Totals.Tok - valsTroves.Tok;
			int64_t dCol = g.m_Troves.m_Totals.Col - valsTroves.Col;
			verify_test((abs(dTok) < 100) && (abs(dCol) < 100));

			std::cout << "RedistPool iActive=" << g.m_RedistPool.m_iActive
				<< "\n\tTok=" << Val2Num(totalRedist.Tok) << ", Col=" << Val2Num(totalRedist.Col)
				<< "\n\tDelta=[" << dTok << "," << dCol << "]"
				<< std::endl;

			dTok = totalStab.Tok - valsStabs.Tok;
			dCol = totalStab.Col - valsStabs.Col;
			verify_test((abs(dTok) < 100) && (abs(dCol) < 100));

			std::cout << "StabPool iActive=" << g.m_StabPool.m_iActive
				<< "\n\tTok=" << Val2Num(totalStab.Tok) << ", Col=" << Val2Num(totalStab.Col)
				<< "\n\tDelta=[" << dTok << "," << dCol << "]"
				<< std::endl;
			

		}

	};

	void MyProcessor::TestNephrite()
	{
		m_Height++;

		VERIFY_ID(Shaders::Nephrite::s_pSID[_countof(Shaders::Nephrite::s_pSID) - 1], m_Nephrite.m_Sid);
		VERIFY_ID(Shaders::Oracle2::s_pSID[_countof(Shaders::Oracle2::s_pSID) - 1], m_Oracle2.m_Sid);
		VERIFY_ID(Shaders::DaoVault::s_pSID[_countof(Shaders::DaoVault::s_pSID) - 1], m_DaoVault.m_Sid);

		MyManager man(*this);
		man.InitMem();

		man.SetCode("nephrite/app.wasm");

		const AssetID aidGov = 77;
		{
			Shaders::DaoVault::Method::Create args;
			ZeroObject(args);
			args.m_Upgradable.m_MinApprovers = 1;

			verify_test(ContractCreate_T(m_DaoVault.m_Cid, m_DaoVault.m_Code, args));

		}

		{
			Shaders::Oracle2::Method::Create args;
			ZeroObject(args);
			args.m_Upgradable.m_MinApprovers = 1;
			args.m_Settings.m_MinProviders = 1;
			args.m_Settings.m_hValidity = 40000;
			verify_test(ContractCreate_T(m_Oracle2.m_Cid, m_Oracle2.m_Code, args));
		}

		{
			Shaders::Oracle2::Method::ProviderAdd args;
			ZeroObject(args);
			args.m_ApproveMask = 1;
			verify_test(RunGuarded_T(m_Oracle2.m_Cid, args.s_iMethod, args));
		}

		{
			Shaders::Oracle2::Method::FeedData args;
			ZeroObject(args);
			args.m_Value = 45; // to the moon!
			verify_test(RunGuarded_T(m_Oracle2.m_Cid, args.s_iMethod, args));
		}


		{
			Shaders::Nephrite::Method::Create args;
			ZeroObject(args);
			args.m_Settings.m_cidDaoVault = m_DaoVault.m_Cid;
			args.m_Settings.m_cidOracle1 = m_Oracle2.m_Cid;
			args.m_Settings.m_cidOracle2 = m_Oracle2.m_Cid;
			args.m_Settings.m_TroveLiquidationReserve = Rules::Coin * 1;
			args.m_Settings.m_AidGov = aidGov;
			args.m_Upgradable.m_MinApprovers = 1; // 0 is illegal atm

			verify_test(ContractCreate_T(m_Nephrite.m_Cid, m_Nephrite.m_Code, args));
		}

		NephriteContext lc(*this);

		const uint32_t s_Users = 5;

		Key::IKdf::Ptr ppKdf[s_Users];
		PubKey pPk[s_Users];

		man.set_ArgBlob("cid", m_Nephrite.m_Cid);
		man.m_Args["role"] = "user";

		for (uint32_t i = 0; i < s_Users; i++)
		{
			ECC::SetRandom(ppKdf[i]);
			man.m_pPKdf = ppKdf[i];
/*
			if (i < 2)
			{
				Shaders::DaoVault::Method::UserUpdate args;
				ZeroObject(args);
				args.m_pkUser.m_X = i;
				args.m_NewStaking = Rules::Coin * (5 + i);

				std::cout << "Deposit to profit pool" << std::endl;

				verify_test(RunGuarded_T(m_DaoVault.m_Cid, args.s_iMethod, args));
			}
*/
			Amount col = Rules::Coin * (35 + i * 5); // should be enough for 150% tcr
			if (1 & i)
				col -= Rules::Coin * 7; // play with order

			man.m_Args["action"] = "trove_modify";
			man.m_Args["tok"] = std::to_string(Rules::Coin * 1000);
			man.m_Args["col"] = std::to_string(col);

			Shaders::Nephrite::Method::TroveOpen args;
			verify_test(man.RunGuarded_T(args));

			pPk[i] = args.m_pkUser;

			verify_test(lc.InvokeTxUser(args, man.m_Charge));

			std::cout << "Trove opened" << std::endl;
			lc.PrintAll();
		}

		m_Height++;

		for (uint32_t i = 0; i < 2; i++)
		{
			man.m_pPKdf = ppKdf[i];

			man.m_Args["action"] = "upd_stab";
			man.m_Args["newVal"] = std::to_string(Rules::Coin * 1750);

			Shaders::Nephrite::Method::UpdStabPool args;
			verify_test(man.RunGuarded_T(args));

			verify_test(lc.InvokeTxUser(args, man.m_Charge));

			std::cout << "Stab" << i << ": Put=" << Val2Num(args.m_NewAmount) << std::endl;
			lc.PrintAll();
		}

		for (uint32_t iCycle = 0; iCycle < 2; iCycle++)
		{
			man.m_pPKdf = ppKdf[s_Users - 1];

			man.m_Args["action"] = "redeem";
			man.m_Args["val"] = std::to_string(Rules::Coin * (iCycle ? 1200 : 350));

			Shaders::Nephrite::Method::Redeem args;
			verify_test(man.RunGuarded_T(args));

			verify_test(lc.InvokeTxUser(args, man.m_Charge));

			std::cout << "Redeem" << std::endl;
			lc.PrintAll();
		}

		{
			Shaders::Oracle2::Method::FeedData args;
			ZeroObject(args);
			args.m_Value = 25; // price drop
			verify_test(RunGuarded_T(m_Oracle2.m_Cid, args.s_iMethod, args));
		}

		m_Height += 10;
		std::cout << "Price drop" << std::endl;
		lc.PrintAll();

		for (uint32_t i = 1; i < 3; i++) 
		{
			man.m_pPKdf = ppKdf[0];

			man.m_Args["action"] = "liquidate";
			man.m_Args["nMaxTroves"] = "1";

			Shaders::Nephrite::Method::Liquidate args;
			verify_test(man.RunGuarded_T(args));

			verify_test(lc.InvokeTxUser(args, man.m_Charge));

			std::cout << "Trove liquidating" << std::endl;
			lc.PrintAll();
		}

		std::cout << "Price recover" << std::endl;

		{
			Shaders::Oracle2::Method::FeedData args;
			ZeroObject(args);
			args.m_Value = 40; // otherwise we can't withdraw from stab pool
			verify_test(RunGuarded_T(m_Oracle2.m_Cid, args.s_iMethod, args));
		}

		for (uint32_t i = 0; i < 2; i++)
		{
			man.m_pPKdf = ppKdf[i];

			man.m_Args["action"] = "upd_stab";
			man.m_Args["newVal"] = "0";

			Shaders::Nephrite::Method::UpdStabPool args;
			verify_test(man.RunGuarded_T(args));

			verify_test(lc.InvokeTxUser(args, man.m_Charge));

			std::cout << "Stab" << i << " all out" << std::endl;
			lc.PrintAll();
		}

		while (lc.m_ActiveTroves)
		{
			auto iTrove = lc.m_iHeadTrove;

			for (uint32_t iMid = lc.m_ActiveTroves / 2; iMid--; )
				iTrove = lc.m_Troves[iTrove - 1].m_iNext;

			man.m_pPKdf = ppKdf[iTrove - 1];

			man.m_Args["action"] = "trove_modify";
			man.m_Args["tok"] = "0";
			man.m_Args["col"] = "0";


			Shaders::Nephrite::Method::TroveClose args;
			verify_test(man.RunGuarded_T(args));

			verify_test(lc.InvokeTx(args, pPk[iTrove - 1], man.m_Charge));

			std::cout << "Trove closing" << std::endl;
			lc.PrintAll();
		}

		// withdraw the surplus
		for (uint32_t i = 0; i < s_Users; i++)
		{
			man.m_pPKdf = ppKdf[i];

			man.m_Args["action"] = "withdraw_surplus";
			man.m_Args["newVal"] = "0";

			Shaders::Nephrite::Method::FundsAccess args;
			if (!man.RunGuarded_T(args))
				continue;

			verify_test(lc.InvokeTxUser(args, man.m_Charge));

			std::cout << "User " << i << " surplus out" << std::endl;
			lc.PrintAll();
		}

/*
		// Stress-test redist pool
		for (uint32_t iCycle = 0; iCycle < 100; iCycle++)
		{
			{
				Shaders::Oracle2::Method::FeedData args;
				ZeroObject(args);
				args.m_Value = 1;
				verify_test(RunGuarded_T(m_Oracle2.m_Cid, args.s_iMethod, args));
			}

			man.m_Args["action"] = "trove_modify";

			for (uint32_t i = 0; i < s_Users; i++)
			{
				ECC::SetRandom(ppKdf[i]);
				man.m_pPKdf = ppKdf[i];
				Amount tok = Rules::Coin * (35 + i * 5);

				man.m_Args["tok"] = std::to_string(tok);
				man.m_Args["col"] = std::to_string(tok * (i + 100) / 50); // approx. 200% ICR

				Shaders::Nephrite::Method::TroveOpen args;
				verify_test(man.RunGuarded_T(args));

				pPk[i] = args.m_pkUser;

				verify_test(lc.InvokeTxUser(args, man.m_Charge));
				lc.PrintAll();
			}

			{
				Shaders::Oracle2::Method::FeedData args;
				ZeroObject(args);
				args.m_Value = 1;
				args.m_Value.m_Order -= 4;
				verify_test(RunGuarded_T(m_Oracle2.m_Cid, args.s_iMethod, args));
				lc.PrintAll();
			}

			uint32_t nCount = s_Users;
			if (!iCycle)
				nCount--;

			man.m_Args["action"] = "liquidate";
			man.m_Args["nMaxTroves"] = "1";

			while (nCount--)
			{

				Shaders::Nephrite::Method::Liquidate args;
				verify_test(man.RunGuarded_T(args));

				verify_test(lc.InvokeTxUser(args, man.m_Charge));

				std::cout << "Trove liquidating" << std::endl;
				lc.PrintAll();
			}

		}

		// close remaining troves
		man.m_Args["action"] = "trove_modify";
		man.m_Args["tok"] = "0";
		man.m_Args["col"] = "0";
		for (uint32_t i = 0; i < s_Users; i++)
		{
			man.m_pPKdf = ppKdf[i];

			Shaders::Nephrite::Method::TroveClose args;
			if (man.RunGuarded_T(args))
			{
				PubKey pkDummy;
				ZeroObject(pkDummy);
				verify_test(lc.InvokeTx(args, pkDummy, man.m_Charge));
			}
		}



		// Stress-test stab pool
		for (uint32_t iCycle = 0; iCycle < 100; iCycle++)
		{
			Amount tokTotal = 0;

			{
				Shaders::Oracle2::Method::FeedData args;
				ZeroObject(args);
				args.m_Value = 1;
				verify_test(RunGuarded_T(m_Oracle2.m_Cid, args.s_iMethod, args));
			}

			for (uint32_t i = 0; i < s_Users; i++)
			{
				if (!iCycle)
					ECC::SetRandom(ppKdf[i]);

				man.m_pPKdf = ppKdf[i];
				Amount tok = Rules::Coin * (35 + i * 5);

				man.m_Args["action"] = "trove_modify";
				man.m_Args["tok"] = std::to_string(tok);
				man.m_Args["col"] = std::to_string(tok * (i + 1002) / 500); // little over 200% ICR

				Shaders::Nephrite::Method::TroveOpen args;
				verify_test(man.RunGuarded_T(args));

				pPk[i] = args.m_pkUser;

				verify_test(lc.InvokeTxUser(args, man.m_Charge));

				man.m_Args["action"] = "upd_stab";
				man.m_Args["newVal"] = std::to_string(tok + tok / 100); // cause almost complete stabpool burn

				Shaders::Nephrite::Method::UpdStabPool args2;
				verify_test(man.RunGuarded_T(args2));

				verify_test(lc.InvokeTxUser(args2, man.m_Charge));


				lc.PrintAll();

				tokTotal += tok;
			}

			{
				Shaders::Oracle2::Method::FeedData args;
				ZeroObject(args);
				args.m_Value = 1;
				args.m_Value.m_Order--; // x2 price drop, all troves must be legit for liquidation via stability pool
				verify_test(RunGuarded_T(m_Oracle2.m_Cid, args.s_iMethod, args));
				lc.PrintAll();
			}


			{
				man.m_Args["action"] = "liquidate";
				man.m_Args["nMaxTroves"] = ""; // unlimited, should liquidate all troves

				Shaders::Nephrite::Method::Liquidate args;
				verify_test(man.RunGuarded_T(args));

				verify_test(lc.InvokeTxUser(args, man.m_Charge));

				std::cout << "Troves liquidated" << std::endl;
				lc.PrintAll();
			}

			m_Height++; // it's forbidden to upd stabpool multiple times in the same height

		}
*/

/*
		for (uint32_t i = 0; i < 2; i++)
		{
			Shaders::DaoVault::Method::UserUpdate args;
			ZeroObject(args);
			args.m_pkUser.m_X = i;
			args.m_NewStaking = 0;
			args.m_WithdrawCount = static_cast<uint32_t>(-1);

			std::cout << "profit withdraw" << std::endl;

			verify_test(RunGuarded_T(m_DaoVault.m_Cid, args.s_iMethod, args));

			lc.PrintAll();
		}
*/
	}

	void MyProcessor::TestMinter()
	{
		VERIFY_ID(Shaders::Minter::s_SID, m_Minter.m_Sid);
	}

	void MyProcessor::TestAmm()
	{
		VERIFY_ID(Shaders::Amm::s_pSID[_countof(Shaders::Amm::s_pSID) - 1], m_Amm.m_Sid);


		{
			Shaders::Amm::Method::Create args;
			ZeroObject(args);
			args.m_Upgradable.m_MinApprovers = 1;
			args.m_Settings.m_cidDaoVault = m_DaoVault.m_Cid;
			verify_test(ContractCreate_T(m_Amm.m_Cid, m_Amm.m_Code, args));
		}

		Shaders::Amm::Pool::ID pid;
		pid.m_Aid1 = 12;
		pid.m_Aid2 = 14;
		pid.m_Fees.m_Kind = 1;

		{
			Shaders::Amm::Method::PoolCreate args;
			ZeroObject(args);
			args.m_Pid = pid;
			verify_test(RunGuarded_T(m_Amm.m_Cid, args.s_iMethod, args));

			verify_test(!RunGuarded_T(m_Amm.m_Cid, args.s_iMethod, args)); // duplication

			pid.m_Fees.m_Kind = 0;
			args.m_Pid = pid;
			verify_test(RunGuarded_T(m_Amm.m_Cid, args.s_iMethod, args)); // ok
		}


		{
			Shaders::Amm::Method::AddLiquidity args;
			ZeroObject(args);
			args.m_Pid = pid;
			args.m_Amounts.m_Tok1 = Rules::Coin * 3450;
			args.m_Amounts.m_Tok2 = Rules::Coin * 170;
			verify_test(RunGuarded_T(m_Amm.m_Cid, args.s_iMethod, args));
		}

		{
			Shaders::Amm::Method::Trade args;
			ZeroObject(args);
			args.m_Pid = pid;
			std::swap(args.m_Pid.m_Aid1, args.m_Pid.m_Aid2);
			args.m_Buy1 = Rules::Coin * 100; // would be very expensive
			verify_test(RunGuarded_T(m_Amm.m_Cid, args.s_iMethod, args));
		}

		{
			Shaders::Amm::Method::Withdraw args;
			ZeroObject(args);
			args.m_Pid = pid;
			args.m_Ctl = Rules::Coin * 100;
			verify_test(RunGuarded_T(m_Amm.m_Cid, args.s_iMethod, args));
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

	template <uint32_t nWords>
	MultiWord::Number<nWords> To_MultiWord(const Shaders::MultiPrecision::UInt<nWords>& x)
	{
		MultiWord::Number<nWords> res;
		const auto* pW = x.get_AsArr();

		for (uint32_t i = 0; i < nWords; i++)
			res.m_p[i] = pW[nWords - i - 1];

		return res;
	}

	template <uint32_t wNom, uint32_t wDenom, uint32_t wQuotient>
	void TestMultiPrecisionDiv(
		const Shaders::MultiPrecision::UInt<wNom>& nom,
		const Shaders::MultiPrecision::UInt<wDenom>& denom,
		const Shaders::MultiPrecision::UInt<wNom>& resid,
		const Shaders::MultiPrecision::UInt<wQuotient>& quotient)
	{
		auto myResid = nom;
		Shaders::MultiPrecision::UInt<wQuotient> myQuotient;
		myQuotient.SetDivResid(myResid, denom);

		// in-host results should be the same
		verify_test(myResid == resid);
		verify_test(myQuotient == quotient);

		// also calculate with our impl
		auto resid_ = To_MultiWord(nom);
		auto denom_ = To_MultiWord(denom);
		MultiWord::Number<wQuotient> quotient_;
		quotient_.SetDivResid(resid_, denom_);

		verify_test(resid_ == To_MultiWord(resid));
		verify_test(quotient_ == To_MultiWord(quotient));

		if (denom.IsZero())
		{
			verify_test(resid == nom);
			
			myQuotient += Shaders::MultiPrecision::UInt<1>(1);
			verify_test(myQuotient.IsZero());
		}
		else
		{
			verify_test(resid < denom);

			auto val = resid + denom * quotient;
			verify_test(val == nom);
		}

	}

	void MyProcessor::TestDummy()
	{
		ContractID& cid = m_Dummy.m_Cid;
		Zero_ zero;
		verify_test(ContractCreate_T(cid, m_Dummy.m_Code, zero));

		m_lstUndo.Clear();

		for (uint32_t i = 0; i < 2; i++)
		{
			bool bPostHF6 = !!i;

			beam::Rules r = Rules::get(); // copy from prev
			beam::Rules::Scope scopeRules(r);

			r.pForks[6].m_Height = bPostHF6 ? Rules::get().pForks[5].m_Height : MaxHeight;


			Shaders::Dummy::TestFarCall args;
			args.m_Variant = 0; // stack, normal
			args.m_Flags = 0;
			verify_test(RunGuarded_T(cid, args.s_iMethod, args));
			args.m_Variant = 1; // stack, too high
			verify_test(!RunGuarded_T(cid, args.s_iMethod, args));
			args.m_Variant = 2; // stack, too low
			verify_test(!RunGuarded_T(cid, args.s_iMethod, args));
			args.m_Variant = 3; // heap
			verify_test(RunGuarded_T(cid, args.s_iMethod, args) == bPostHF6);
			args.m_Variant = 4; // heap, too high
			verify_test(RunGuarded_T(cid, args.s_iMethod, args) == bPostHF6);
			args.m_Variant = 5; // heap, too low
			verify_test(RunGuarded_T(cid, args.s_iMethod, args) == bPostHF6);
			args.m_Variant = 6; // global
			verify_test(RunGuarded_T(cid, args.s_iMethod, args) == bPostHF6);
			args.m_Variant = 7; // data
			verify_test(RunGuarded_T(cid, args.s_iMethod, args) == bPostHF6);

			args.m_Variant = 0;
			args.m_Flags = CallFarFlags::InheritContext;
			verify_test(RunGuarded_T(cid, args.s_iMethod, args)); // should succeed, but won't affect the callee contract

			UndoChanges();
		}

		{
			Shaders::Dummy::TestFarCallFlags args;
			ZeroObject(args);
			args.m_DepthRemaining = bvm2::Limits::FarCallDepth ;
			verify_test(!RunGuarded_T(cid, args.s_iMethod, args));

			for (args.m_TryWrite = 0; args.m_TryWrite <= 1; args.m_TryWrite++)
			{
				bool bR = !args.m_TryWrite;

				args.m_Flags = 0;
				args.m_DepthRemaining = 2;
				verify_test(RunGuarded_T(cid, args.s_iMethod, args));

				args.m_Flags = Shaders::CallFarFlags::SelfLockRO;
				args.m_DepthRemaining = 2;
				verify_test(RunGuarded_T(cid, args.s_iMethod, args) == bR);

				args.m_Flags = Shaders::CallFarFlags::GlobalLockRO;
				args.m_DepthRemaining = 2;
				verify_test(RunGuarded_T(cid, args.s_iMethod, args) == bR);

				args.m_Flags = Shaders::CallFarFlags::SelfBlock;
				args.m_DepthRemaining = 2;
				verify_test(!RunGuarded_T(cid, args.s_iMethod, args));
			}
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
			Shaders::Dummy::MathTest2 args;

			ZeroObject(args);

			args.m_Nom.set_Val<5>(0x7fffffff);
			args.m_Nom.set_Val<4>(0x80000000);
			args.m_Nom.set_Val<3>(0xe3212316);
			args.m_Nom.set_Val<2>(0xe3212316);
			args.m_Nom.set_Val<1>(0xcc212316);

			args.m_Denom.set_Val<4>(0x80000000); // tricky case, division correction vs init guess is done twice
			args.m_Denom.set_Val<3>(0xffffffff);

			verify_test(RunGuarded_T(cid, args.s_iMethod, args));
			TestMultiPrecisionDiv(args.m_Nom, args.m_Denom, args.m_Resid, args.m_Quotient);

			args.m_Denom.set_Val<3>(0x7fffffff); // msb not set, normalization would be applied
			verify_test(RunGuarded_T(cid, args.s_iMethod, args));
			TestMultiPrecisionDiv(args.m_Nom, args.m_Denom, args.m_Resid, args.m_Quotient);

			args.m_Denom.set_Val<3>(0x1fffffff); // one correction
			verify_test(RunGuarded_T(cid, args.s_iMethod, args));
			TestMultiPrecisionDiv(args.m_Nom, args.m_Denom, args.m_Resid, args.m_Quotient);

			args.m_Denom.set_Val<4>(0x12345678); // no correction
			verify_test(RunGuarded_T(cid, args.s_iMethod, args));
			TestMultiPrecisionDiv(args.m_Nom, args.m_Denom, args.m_Resid, args.m_Quotient);

			args.m_Denom.Set0(); // div by 0
			verify_test(RunGuarded_T(cid, args.s_iMethod, args));
			TestMultiPrecisionDiv(args.m_Nom, args.m_Denom, args.m_Resid, args.m_Quotient);


			args.m_Nom.Set<2>(0xb5e620f47ffc0000ull);
			args.m_Denom.Set<0>(0xb5e620f480000000ull); // tricky case: hiwords of nom and denom are equal, init guess will exceed 1 word
			verify_test(RunGuarded_T(cid, args.s_iMethod, args));
			TestMultiPrecisionDiv(args.m_Nom, args.m_Denom, args.m_Resid, args.m_Quotient);



			// some random numbers
			for (uint32_t i = 0; i < 50; i++)
			{
				ECC::GenRandom(&args.m_Nom, sizeof(args.m_Nom));
				ECC::GenRandom(&args.m_Denom, sizeof(args.m_Denom));

				if (i > 20)
				{
					args.m_Denom.set_Val<4>(0);
					if (i > 30)
					{
						args.m_Denom.set_Val<3>(0);
						if (i > 40)
							args.m_Denom.set_Val<2>(0);
					}
				}

				verify_test(RunGuarded_T(cid, args.s_iMethod, args));
				TestMultiPrecisionDiv(args.m_Nom, args.m_Denom, args.m_Resid, args.m_Quotient);
			}
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
			Rules r = Rules::get(); // make a copy
			Rules::Scope scopeRules(r);


			r.pForks[0].m_Height = 0;
			r.pForks[0].m_Hash.Scan("ed91a717313c6eb0e3f082411584d0da8f0c8af2a4ac01e5af1959e0ec4338bc");
			r.pForks[1].m_Height = 321321;
			r.pForks[1].m_Hash.Scan("622e615cfd29d0f8cdd9bdd76d3ca0b769c8661b29d7ba9c45856c96bc2ec5bc");
			r.pForks[2].m_Height = 777777;
			r.pForks[2].m_Hash.Scan("1ce8f721bf0c9fa7473795a97e365ad38bbc539aab821d6912d86f24e67720fc");

			r.pForks[3].m_Height = 999999999;
			r.pForks[3].m_Hash = Zero;

			r.DisableForksFrom(4);


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
			verify_test(args.m_Cid == m_Dummy.m_Cid);

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

		{
			Height h = Rules::get().pForks[4].m_Height + 3;
			TemporarySwap ts(h, m_Height);

			Shaders::Dummy::FindVarTest args;
			verify_test(RunGuarded_T(cid, args.s_iMethod, args));
		}

		{
			Shaders::Dummy::TestFloat1 args1;
			ZeroObject(args1);
			Shaders::Dummy::TestFloat2 args2;
			ZeroObject(args2);

			for (uint32_t i = 0; i < 100; i++)
			{
				uintBigFor<uint64_t>::Type val;
				uint64_t a, b, c;
				ECC::GenRandom(val);
				val.Export(a);
				ECC::GenRandom(val);
				val.Export(b);

				if (a < b)
					std::swap(a, b);

				if (!(i % 10))
				{
					// test zeroes too
					b = 0;

					if (i == 40)
						a = 0;
				}

				args1.m_Arg1 = a;
				args1.m_Arg2 = b;
				args1.m_Op = 1; // subtract

				verify_test(args1.m_Arg1 >= args1.m_Arg2);

				verify_test(RunGuarded_T(cid, args1.s_iMethod, args1));

				verify_test(args1.m_Arg1.RoundDown(c));
				verify_test(c == a - b);

				args1.m_Arg2 = args1.m_Arg1;
				verify_test(args1.m_Arg1 == args1.m_Arg2);
				verify_test(RunGuarded_T(cid, args1.s_iMethod, args1)); // must be 0

				verify_test(args1.m_Arg1.IsZero());
				verify_test(args1.m_Arg1.RoundDown(c));
				verify_test(!c);

				args2.m_Arg1 = a;
				args2.m_Arg2 = b;
				args2.m_Op = 1; // subtract

				verify_test(args2.m_Arg1 >= args2.m_Arg2);

				verify_test(RunGuarded_T(cid, args2.s_iMethod, args2));

				verify_test(!args2.m_Arg1.IsNaN() && !args2.m_Arg1.IsNegative());
				verify_test(args2.m_Arg1.RoundDown(c));
				verify_test(c == a - b);

				// check negative subtraction. Make sure result fits in int64_t (for comparison)
				b >>= 2;
				a >>= 2;
				// b - a
				args2.m_Arg1 = b;
				args2.m_Arg2 = a;

				verify_test(args2.m_Arg1 <= args2.m_Arg2);

				verify_test(RunGuarded_T(cid, args2.s_iMethod, args2));

				verify_test(!args2.m_Arg1.IsNaN() && !args2.m_Arg1.IsPositive());
				args2.m_Arg1.RoundDown(c);
				verify_test(!c); // underflow, truncated to 0

				int64_t d;
				verify_test(args2.m_Arg1.RoundDown(d));
				verify_test(d == static_cast<int64_t>(b - a));

				// b - (-a)
				args2.m_Arg1 = b;
				args2.m_Arg2 = a;
				args2.m_Arg2.Negate();
				verify_test(args2.m_Arg1 >= args2.m_Arg2);
				verify_test(RunGuarded_T(cid, args2.s_iMethod, args2));
				verify_test(args2.m_Arg1.RoundDown(d));
				verify_test(d == static_cast<int64_t>(b + a));

				// (-b) - (-a)
				args2.m_Arg1 = b;
				args2.m_Arg2 = a;
				args2.m_Arg1.Negate();
				args2.m_Arg2.Negate();
				verify_test(args2.m_Arg1 >= args2.m_Arg2);
				verify_test(RunGuarded_T(cid, args2.s_iMethod, args2));
				verify_test(args2.m_Arg1.RoundDown(d));
				verify_test(d == static_cast<int64_t>(a - b));

				// (-b) - a
				args2.m_Arg1 = b;
				args2.m_Arg2 = a;
				args2.m_Arg1.Negate();
				verify_test(args2.m_Arg1 <= args2.m_Arg2);
				verify_test(RunGuarded_T(cid, args2.s_iMethod, args2));
				verify_test(args2.m_Arg1.RoundDown(d));
				verify_test(d == -static_cast<int64_t>(a + b));

				// a + b
				args2.m_Op = 0; // add
				args2.m_Arg1 = a;
				args2.m_Arg2 = b;
				verify_test(RunGuarded_T(cid, args2.s_iMethod, args2));

				verify_test(args2.m_Arg1.RoundDown(c));
				verify_test(c == a + b);

				args1.m_Op = 0; // add
				args1.m_Arg1 = a;
				args1.m_Arg2 = b;
				verify_test(RunGuarded_T(cid, args1.s_iMethod, args1));

				verify_test(args1.m_Arg1.RoundDown(c));
				verify_test(c == a + b);

				// a + (-b)
				args2.m_Arg1 = a;
				args2.m_Arg2 = b;
				args2.m_Arg2.Negate();
				verify_test(args2.m_Arg1 >= args2.m_Arg2);
				verify_test(RunGuarded_T(cid, args2.s_iMethod, args2));
				verify_test(args2.m_Arg1.RoundDown(d));
				verify_test(d == static_cast<int64_t>(a - b));

				// (-a) + b
				args2.m_Arg1 = a;
				args2.m_Arg2 = b;
				args2.m_Arg1.Negate();
				verify_test(args2.m_Arg1 <= args2.m_Arg2);
				verify_test(RunGuarded_T(cid, args2.s_iMethod, args2));
				verify_test(args2.m_Arg1.RoundDown(d));
				verify_test(d == static_cast<int64_t>(b - a));

				// (-a) + (-b)
				args2.m_Arg1 = a;
				args2.m_Arg2 = b;
				args2.m_Arg1.Negate();
				args2.m_Arg2.Negate();
				verify_test(args2.m_Arg1 <= args2.m_Arg2);
				verify_test(RunGuarded_T(cid, args2.s_iMethod, args2));
				verify_test(args2.m_Arg1.RoundDown(d));
				verify_test(d == -static_cast<int64_t>(a + b));


				// Multiplication

				args2.m_Arg1 = a;
				args2.m_Arg2 = b;
				args2.m_Op = 2; // mul
				args2.m_Arg1 <<= 70;
				args2.m_Arg2 >>= 300;

				double a_ = ToDouble(args2.m_Arg1);
				double b_ = ToDouble(args2.m_Arg2);
				double c_ = a_ * b_;

				verify_test(RunGuarded_T(cid, args2.s_iMethod, args2));
				double d_ = ToDouble(args2.m_Arg1);

				AssertDoubleRelEqual(c_, d_);

				args1.m_Arg1 = a;
				args1.m_Arg2 = b;
				args1.m_Op = 2; // mul
				args1.m_Arg1 <<= 70;
				args1.m_Arg2 >>= 300;

				verify_test(RunGuarded_T(cid, args1.s_iMethod, args1));
				d_ = ToDouble(args1.m_Arg1);

				AssertDoubleRelEqual(c_, d_);

				args2.m_Arg1 = a;
				args2.m_Arg1 <<= 705;
				args2.m_Arg1.Negate();

				a_ = ToDouble(args2.m_Arg1);
				c_ = a_ * b_;

				verify_test(RunGuarded_T(cid, args2.s_iMethod, args2));
				d_ = ToDouble(args2.m_Arg1);

				AssertDoubleRelEqual(c_, d_);

				// Division
				args2.m_Arg1 = a;
				args2.m_Arg1 <<= 75;
				args2.m_Arg2.Negate();
				args2.m_Op = 3; // div

				a_ = ToDouble(args2.m_Arg1);
				b_ = ToDouble(args2.m_Arg2);

				verify_test(RunGuarded_T(cid, args2.s_iMethod, args2));

				args1.m_Arg1 = a;
				args1.m_Arg1 <<= 75;
				args1.m_Op = 3; // div

				verify_test(RunGuarded_T(cid, args1.s_iMethod, args1));

				if (b)
				{
					c_ = a_ / b_;
					d_ = ToDouble(args2.m_Arg1);
					AssertDoubleRelEqual(c_, d_);
					d_ = ToDouble(args1.m_Arg1);
					AssertDoubleRelEqual(c_, -d_);
				}
				else
				{
					verify_test(args2.m_Arg1.IsNaN());
					verify_test(args1.m_Arg1.IsZero());
				}

				args2.m_Arg1 = 0;
				verify_test(RunGuarded_T(cid, args2.s_iMethod, args2));

				if (b)
					verify_test(args2.m_Arg1.IsZero());
				else
					verify_test(args2.m_Arg1.IsNaN());

			}
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
			args.m_Rules = Rules::get().pForks[6].m_Hash;
			verify_test(ContractCreate_T(m_Sidechain.m_Cid, m_Sidechain.m_Code, args));
		}

		VERIFY_ID(Shaders::Sidechain::s_SID, m_Sidechain.m_Sid);

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

			verify_test(RunGuarded_T(m_Sidechain.m_Cid, args.s_iMethod, args));

			verify_test(!RunGuarded_T(m_Sidechain.m_Cid, args.s_iMethod, args)); // chainwork didn't grow

			args.m_Prefix.m_ChainWork.Inc(); // tamper with chainwork
			verify_test(!RunGuarded_T(m_Sidechain.m_Cid, args.s_iMethod, args));
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

			verify_test(RunGuarded_T(m_Sidechain.m_Cid, args.s_iMethod, args)); // reorg should be ok, despite the fact it's shorter
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

			verify_test(RunGuarded_T(m_Sidechain.m_Cid, args.s_iMethod, args));
			verify_test(RunGuarded_T(m_Sidechain.m_Cid, args.s_iMethod, args)); // redundant proofs is ok
		}

		{
			Shaders::Sidechain::WithdrawComission args;
			ZeroObject(args);
			args.m_Contributor.m_X = 116U;
			args.m_Amount = 400;

			verify_test(RunGuarded_T(m_Sidechain.m_Cid, args.s_iMethod, args));
			verify_test(RunGuarded_T(m_Sidechain.m_Cid, args.s_iMethod, args));
			verify_test(!RunGuarded_T(m_Sidechain.m_Cid, args.s_iMethod, args));
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
			verify_test(!ContractCreate_T(m_Oracle.m_Cid, m_Oracle.m_Code, args)); // zero providers not allowed

			args.m_Providers = nOracles;
			verify_test(ContractCreate_T(m_Oracle.m_Cid, m_Oracle.m_Code, args));
		}

		Shaders::Oracle::Get argsResult;
		argsResult.m_Value = 0;
		verify_test(RunGuarded_T(m_Oracle.m_Cid, argsResult.s_iMethod, argsResult));
		pd.TestMedian(argsResult.m_Value);

		// set rate, trigger median recalculation
		for (uint32_t i = 0; i < nOracles * 10; i++)
		{
			uint32_t iOracle = i % nOracles;

			Shaders::Oracle::Set args;
			args.m_iProvider = iOracle;

			ECC::GenRandom(&args.m_Value, sizeof(args.m_Value));
			pd.Set(iOracle, args.m_Value);

			verify_test(RunGuarded_T(m_Oracle.m_Cid, args.s_iMethod, args));

			pd.Sort();

			argsResult.m_Value = 0;
			verify_test(RunGuarded_T(m_Oracle.m_Cid, argsResult.s_iMethod, argsResult));
			pd.TestMedian(argsResult.m_Value);
		}

		Zero_ zero;
		verify_test(ContractDestroy_T(m_Oracle.m_Cid, zero));
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
			verify_test(ContractCreate_T(m_Oracle.m_Cid, m_Oracle.m_Code, args));
		}

		Shaders::StableCoin::Create<sizeof(szMyMeta) - 1> argSc;
		argSc.m_RateOracle = m_Oracle.m_Cid;
		argSc.m_nMetaData = sizeof(szMyMeta) - 1;
		memcpy(argSc.m_pMetaData, szMyMeta, argSc.m_nMetaData);
		argSc.m_CollateralizationRatio = RateFromPercents(150);

		verify_test(ContractCreate_T(m_StableCoin.m_Cid, m_StableCoin.m_Code, argSc));

		Shaders::StableCoin::UpdatePosition argUpd;
		argUpd.m_Change.m_Beam = 1000;
		argUpd.m_Change.m_Asset = 241;
		argUpd.m_Direction.m_BeamAdd = 1;
		argUpd.m_Direction.m_AssetAdd = 0;
		ZeroObject(argUpd.m_Pk);

		verify_test(!RunGuarded_T(m_StableCoin.m_Cid, argUpd.s_iMethod, argUpd)); // will fail, not enough collateral

		argUpd.m_Change.m_Asset = 239;
		verify_test(RunGuarded_T(m_StableCoin.m_Cid, argUpd.s_iMethod, argUpd)); // should work

		Zero_ zero;
		verify_test(!ContractDestroy_T(m_StableCoin.m_Cid, zero)); // asset was not fully burned

		//verify_test(ContractDestroy_T(argSc.m_RateOracle, zero));
	}

	void MyProcessor::TestPerpetual()
	{
		VERIFY_ID(Shaders::Perpetual::s_SID, m_Perpetual.m_Sid);

		{
			Shaders::Perpetual::Create arg;
			arg.m_Oracle = m_Oracle.m_Cid;
			arg.m_MarginRequirement_mp = 15 * 1000;
			verify_test(ContractCreate_T(m_Perpetual.m_Cid, m_Perpetual.m_Code, arg));
		}

		{
			Shaders::Perpetual::CreateOffer arg;
			ZeroObject(arg.m_Account);
			arg.m_AmountBeam = 1000;
			arg.m_AmountToken = 140;
			arg.m_TotalBeams = 1149;
			verify_test(!RunGuarded_T(m_Perpetual.m_Cid, arg.s_iMethod, arg)); // less than 15% collateral

			arg.m_TotalBeams = 1150;
			verify_test(RunGuarded_T(m_Perpetual.m_Cid, arg.s_iMethod, arg));
		}
	}

	void MyProcessor::TestPipe()
	{
		VERIFY_ID(Shaders::Pipe::s_SID, m_Pipe.m_Sid);

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


			verify_test(ContractCreate_T(m_Pipe.m_Cid, m_Pipe.m_Code, arg));
		}

		{
			Shaders::Pipe::SetRemote arg;
			ZeroObject(arg);
			verify_test(!RunGuarded_T(m_Pipe.m_Cid, arg.s_iMethod, arg));

			arg.m_cid.Inc();
			verify_test(RunGuarded_T(m_Pipe.m_Cid, arg.s_iMethod, arg));
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

			verify_test(RunGuarded(m_Pipe.m_Cid, arg.m_Push.s_iMethod, Blob(&arg, sizeof(arg)), nullptr));

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

			verify_test(RunGuarded(m_Pipe.m_Cid, pArg->s_iMethod, bufArg, nullptr)); // should be ok

			verify_test(!RunGuarded(m_Pipe.m_Cid, pArg->s_iMethod, bufArg, nullptr)); // should evaluate both variants, and notice they're same
		}

		{
			Shaders::Pipe::FinalyzeRemote arg;
			arg.m_DepositStake = 0;
			verify_test(!RunGuarded(m_Pipe.m_Cid, arg.s_iMethod, Blob(&arg, sizeof(arg)), nullptr)); // too early

			m_Height += 40;
			verify_test(RunGuarded(m_Pipe.m_Cid, arg.s_iMethod, Blob(&arg, sizeof(arg)), nullptr));
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

			verify_test(!RunGuarded(m_Pipe.m_Cid, arg.m_Read.s_iMethod, Blob(&arg, sizeof(arg)), nullptr)); // private msg, we can't read it

			arg.m_Read.m_iMsg = 2;
			verify_test(!RunGuarded(m_Pipe.m_Cid, arg.m_Read.s_iMethod, Blob(&arg, sizeof(arg)), nullptr)); // public, can't wipe

			arg.m_Read.m_Wipe = 0;
			verify_test(RunGuarded(m_Pipe.m_Cid, arg.m_Read.s_iMethod, Blob(&arg, sizeof(arg)), nullptr)); // ok
			verify_test(RunGuarded(m_Pipe.m_Cid, arg.m_Read.s_iMethod, Blob(&arg, sizeof(arg)), nullptr)); // ok, msg is not wiped

			verify_test(arg.m_Read.m_MsgSize == 126);
			for (uint32_t i = 0; i < arg.m_Read.m_MsgSize; i++)
				verify_test(arg.m_pMsg[i] == '2');
		}
	}

	void MyProcessor::TestMirrorCoin()
	{
		VERIFY_ID(Shaders::MirrorCoin::s_SID, m_MirrorCoin.m_Sid);

		{
			Shaders::MirrorCoin::Create0 arg;
			arg.m_Aid = 0;
			arg.m_MetadataSize = 0;
			arg.m_PipeID = m_Pipe.m_Cid;

			verify_test(ContractCreate_T(m_MirrorCoin.m_Cid, m_MirrorCoin.m_Code, arg));
		}

		{
#pragma pack (push, 1)
			struct Arg :public Shaders::MirrorCoin::Create0 {
				char m_chMeta = 'x';
			} arg;
#pragma pack (pop)
			arg.m_Aid = 0;
			arg.m_MetadataSize = sizeof(arg.m_chMeta);
			arg.m_PipeID = m_Pipe.m_Cid;

			verify_test(ContractCreate_T(m_cidMirrorCoin2, m_MirrorCoin.m_Code, arg));
		}

		{
			Shaders::MirrorCoin::SetRemote arg;
			arg.m_Cid = m_cidMirrorCoin2;
			verify_test(RunGuarded_T(m_MirrorCoin.m_Cid, arg.s_iMethod, arg));
			arg.m_Cid = m_MirrorCoin.m_Cid;
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
			const ContractID& cidSrc = iCycle ? m_cidMirrorCoin2 : m_MirrorCoin.m_Cid;
			const ContractID& cidDst = iCycle ? m_MirrorCoin.m_Cid : m_cidMirrorCoin2;

			Shaders::MirrorCoin::Send argS;
			argS.m_Amount = 450;
			ECC::SetRandom(argS.m_User.m_X);
			argS.m_User.m_Y = 0;
			
			verify_test(RunGuarded_T(cidSrc, argS.s_iMethod, argS));

			// simulate message passed
			Shaders::Env::Key_T<Shaders::Pipe::MsgHdr::KeyIn> key;
			key.m_Prefix.m_Cid = m_Pipe.m_Cid;
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

		verify_test(ContractCreate_T(m_Faucet.m_Cid, m_Faucet.m_Code, pars));
		VERIFY_ID(Shaders::Faucet::s_SID, m_Faucet.m_Sid);

		m_lstUndo.Clear();

		Shaders::Faucet::Deposit deps;
		deps.m_Aid = 10;
		deps.m_Amount = 20000;
		verify_test(RunGuarded_T(m_Faucet.m_Cid, Shaders::Faucet::Deposit::s_iMethod, deps));

		Shaders::Faucet::Withdraw wdrw;

		ECC::Scalar::Native k;
		ECC::SetRandom(k);
		ECC::Point::Native pt = ECC::Context::get().G * k;

		wdrw.m_Key.m_Account = pt;
		wdrw.m_Key.m_Aid = 10;
		wdrw.m_Amount = 150;

		verify_test(RunGuarded_T(m_Faucet.m_Cid, Shaders::Faucet::Withdraw::s_iMethod, wdrw));
		verify_test(RunGuarded_T(m_Faucet.m_Cid, Shaders::Faucet::Withdraw::s_iMethod, wdrw));
		verify_test(!RunGuarded_T(m_Faucet.m_Cid, Shaders::Faucet::Withdraw::s_iMethod, wdrw));

		m_Height += 15;
		verify_test(RunGuarded_T(m_Faucet.m_Cid, Shaders::Faucet::Withdraw::s_iMethod, wdrw));

		wdrw.m_Amount = 0;
		verify_test(RunGuarded_T(m_Faucet.m_Cid, Shaders::Faucet::Withdraw::s_iMethod, wdrw));

		m_Height += 5;
		verify_test(RunGuarded_T(m_Faucet.m_Cid, Shaders::Faucet::Withdraw::s_iMethod, wdrw));

		UndoChanges(); // up to (but not including) contract creation
	}

	void MyProcessor::TestRoulette()
	{
		Shaders::Roulette::Params pars;
		memset(reinterpret_cast<void*>(&pars.m_Dealer), 0xe1, sizeof(pars.m_Dealer));

		verify_test(ContractCreate_T(m_Roulette.m_Cid, m_Roulette.m_Code, pars));
		VERIFY_ID(Shaders::Roulette::s_SID, m_Roulette.m_Sid);

		Shaders::Roulette::Spin spin;
		verify_test(RunGuarded_T(m_Roulette.m_Cid, spin.s_iMethod, spin));

		Shaders::Roulette::Bid bid;
		bid.m_Player.m_Y = 0;

		for (uint32_t i = 0; i < Shaders::Roulette::State::s_Sectors * 2; i++)
		{
			bid.m_Player.m_X = i;
			bid.m_iSector = i % Shaders::Roulette::State::s_Sectors;
			verify_test(RunGuarded_T(m_Roulette.m_Cid, bid.s_iMethod, bid));
		}

		verify_test(!RunGuarded_T(m_Roulette.m_Cid, bid.s_iMethod, bid)); // redundant bid

		bid.m_iSector = Shaders::Roulette::State::s_Sectors + 3;
		bid.m_Player.m_X = Shaders::Roulette::State::s_Sectors * 2 + 8;
		verify_test(!RunGuarded_T(m_Roulette.m_Cid, bid.s_iMethod, bid)); // invalid sector

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
		verify_test(!RunGuarded_T(m_Roulette.m_Cid, take.s_iMethod, take)); // round isn't over

		Zero_ zero;
		verify_test(RunGuarded_T(m_Roulette.m_Cid, Shaders::Roulette::BetsOff::s_iMethod, zero));

		verify_test(RunGuarded_T(m_Roulette.m_Cid, take.s_iMethod, take)); // ok
		verify_test(!RunGuarded_T(m_Roulette.m_Cid, take.s_iMethod, take)); // already took

		UndoChanges(); // up to (but not including) contract creation
	}

	void MyProcessor::TestVoting()
	{
		Zero_ zero;
		verify_test(ContractCreate_T(m_Voting.m_Cid, m_Voting.m_Code, zero));
		VERIFY_ID(Shaders::Voting::s_CID, m_Voting.m_Cid);
		VERIFY_ID(Shaders::Voting::s_SID, m_Voting.m_Sid);

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
			verify_test(!RunGuarded_T(m_Voting.m_Cid, args.s_iMethod, args)); // too late

			args.m_Params.m_hMax = 10;
			verify_test(RunGuarded_T(m_Voting.m_Cid, args.s_iMethod, args));

			verify_test(!RunGuarded_T(m_Voting.m_Cid, args.s_iMethod, args)); // duplicated ID
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

			verify_test(!RunGuarded_T(m_Voting.m_Cid, args.s_iMethod, args));

			args.m_ID = hvProposal;
			args.m_Variant = 12; // out-of-bounds
			verify_test(!RunGuarded_T(m_Voting.m_Cid, args.s_iMethod, args));

			args.m_Variant = 11;
			m_Height = 15; // too late
			verify_test(!RunGuarded_T(m_Voting.m_Cid, args.s_iMethod, args));

			m_Height = 5; // ok
			verify_test(RunGuarded_T(m_Voting.m_Cid, args.s_iMethod, args));
		}

		{
			Shaders::Voting::Withdraw args;
			args.m_ID = hvProposal;
			args.m_Pk = pt;
			args.m_Amount = 50;

			verify_test(!RunGuarded_T(m_Voting.m_Cid, args.s_iMethod, args)); // voting isn't over yet

			m_Height = 11;
			verify_test(RunGuarded_T(m_Voting.m_Cid, args.s_iMethod, args)); // ok, withdrew half
			verify_test(RunGuarded_T(m_Voting.m_Cid, args.s_iMethod, args)); // withdrew all
			verify_test(!RunGuarded_T(m_Voting.m_Cid, args.s_iMethod, args)); // no more left
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

	void MyProcessor::TestDaoCore()
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
		verify_test(ContractCreate_T(m_DaoCore.m_Cid, m_DaoCore.m_Code, zero));
		VERIFY_ID(Shaders::DaoCore::s_SID, m_DaoCore.m_Sid);

		m_Height += Shaders::DaoCore::Preallocated::s_hLaunch;

		for (uint32_t i = 0; i < 10; i++)
		{
			Shaders::DaoCore::UpdPosFarming args;
			ZeroObject(args);

			args.m_Beam = Shaders::g_Beam2Groth * 20000 * (i + 3);
			args.m_BeamLock = 1;
			args.m_Pk.m_X = i;
			verify_test(RunGuarded_T(m_DaoCore.m_Cid, args.s_iMethod, args));

			if (i & 1)
				m_Height += 1000;
		}

		for (uint32_t i = 0; i < 10; i++)
		{
			Shaders::DaoCore::UpdPosFarming args;
			ZeroObject(args);

			args.m_Beam = Shaders::g_Beam2Groth * 20000 * (i + 3);
			args.m_Pk.m_X = i;
			verify_test(RunGuarded_T(m_DaoCore.m_Cid, args.s_iMethod, args));

			if (i & 1)
				m_Height += 1000;
		}

		// the following is disabled, since the contract in this test is standalone, not under Upgradable, hence it doesn' allocate anything in c'tor
/*
		{
			Shaders::DaoCore::GetPreallocated args;
			ZeroObject(args);
			args.m_Amount = 50;
			Cast::Reinterpret<beam::uintBig_t<33> >(args.m_Pk).Scan("8bb3375b455d9c577134b00e8b0b108a29ce2bc0fce929049306cf4fed723b7d00");
			verify_test(!RunGuarded_T(m_DaoCore.m_Cid, args.s_iMethod, args)); // wrong pk

			Cast::Reinterpret<beam::uintBig_t<33> >(args.m_Pk).Scan("8bb3375b455d9c577134b00e8b0b108a29ce2bc0fce929049306cf4fed723b7d01");
			verify_test(RunGuarded_T(m_DaoCore.m_Cid, args.s_iMethod, args)); // ok

			args.m_Amount = 31000 / 2 * Shaders::g_Beam2Groth;
			verify_test(!RunGuarded_T(m_DaoCore.m_Cid, args.s_iMethod, args)); // too much
		}
*/
	}

	void MyProcessor::TestDaoVote()
	{
		{
			Shaders::DaoVote::Method::Create args;
			ZeroObject(args);
			args.m_Upgradable.m_MinApprovers = 1;
			args.m_Cfg.m_Aid = 22;
			args.m_Cfg.m_hEpochDuration = 50;

			verify_test(ContractCreate_T(m_DaoVote.m_Cid, m_DaoVote.m_Code, args));
		}
		VERIFY_ID(Shaders::DaoVote::s_pSID[_countof(Shaders::DaoVote::s_pSID) - 1], m_DaoVote.m_Sid);

		PubKey pkModerator(Zero);
		pkModerator.m_X = 4432U;

		{
			Shaders::DaoVote::Method::SetModerator args;
			args.m_pk = pkModerator;
			args.m_Enable = 0;
			verify_test(!RunGuarded_T(m_DaoVote.m_Cid, args.s_iMethod, args));

			args.m_Enable = 1;
			verify_test(RunGuarded_T(m_DaoVote.m_Cid, args.s_iMethod, args));
			verify_test(!RunGuarded_T(m_DaoVote.m_Cid, args.s_iMethod, args));

		}

		for (uint32_t iEpoch = 1; iEpoch <= 4; iEpoch++, m_Height += 50)
		{
			const uint32_t nProposalsPerEpoch = 3;
			for (uint32_t i = 0; i < nProposalsPerEpoch; i++)
			{
				Shaders::DaoVote::Method::AddProposal args;
				args.m_pkModerator = pkModerator;
				args.m_TxtLen = 0;
				args.m_Data.m_Variants = i + 2;

				verify_test(RunGuarded_T(m_DaoVote.m_Cid, args.s_iMethod, args));
			}

			for (uint32_t i = 0; i < 4; i++)
			{
				Shaders::DaoVote::Method::AddDividend args;
				args.m_Val.m_Aid = 33 + i;
				args.m_Val.m_Amount = Rules::Coin * 40;
				verify_test(RunGuarded_T(m_DaoVote.m_Cid, args.s_iMethod, args));
			}

			for (uint32_t i = 0; i < 5; i++)
			{
				Shaders::DaoVote::Method::MoveFunds args;
				ZeroObject(args);
				args.m_Amount = 20 + i;
				args.m_Lock = 1;
				args.m_pkUser.m_X = i;
				verify_test(RunGuarded_T(m_DaoVote.m_Cid, args.s_iMethod, args));
			}

			if (iEpoch <= 1)
				continue; // no proposals yet

			for (uint32_t i = 0; i < 5; i++)
			{
#pragma pack (push, 1)
				struct MyArgs :public Shaders::DaoVote::Method::Vote {
					uint8_t m_Vote[nProposalsPerEpoch];
				};
#pragma pack (pop)

				MyArgs args;
				ZeroObject(args);
				args.m_iEpoch = iEpoch;
				args.m_pkUser.m_X = i;
				args.m_Vote[1] = 1;
				args.m_Vote[2] = 2;

				verify_test(RunGuarded_T(m_DaoVote.m_Cid, args.s_iMethod, args));
			}

		}
	}

	void MyProcessor::TestDaoAccumulator()
	{
		VERIFY_ID(Shaders::DaoAccumulator::s_pSID[_countof(Shaders::DaoAccumulator::s_pSID) - 1], m_DaoAccumulator.m_Sid);
		VERIFY_ID(Shaders::DaoAccumulator::s_pSID[2], m_DaoAccumulator_v2.m_Sid);

		{
			Shaders::DaoAccumulator::Method::Create args;
			ZeroObject(args);
			args.m_Upgradable.m_MinApprovers = 1;
			args.m_aidBeamX = Shaders::DaoAccumulator::NphAddonParams::s_aidBeamX;
			args.m_hPrePhaseEnd = m_Height;

			verify_test(ContractCreate_T(m_DaoAccumulator.m_Cid, m_DaoAccumulator_v2.m_Code, args)); // deploy v2 version
		}

		m_Height += 20;

		{
			Shaders::DaoAccumulator::Method::FarmStart args;
			ZeroObject(args);
			args.m_ApproveMask = 1u;
			args.m_FarmBeamX = Rules::Coin * 6'000'000;
			args.m_aidLpToken = 50;
			args.m_hFarmDuration = 1440 * 365 * 2;

			verify_test(RunGuarded_T(m_DaoAccumulator.m_Cid, args.s_iMethod, args));
		}

		m_Height += 100;

		for (uint32_t i = 0; i < 5; i++)
		{
			Shaders::DaoAccumulator::Method::UserLock args;
			ZeroObject(args);
			args.m_pkUser.m_X = i + 400;
			args.m_LpToken = Rules::Coin * (55 + i);
			args.m_hEnd = m_Height + Shaders::DaoAccumulator::User::s_LockPeriodBlocks * (i + 1);
			args.m_PoolType = Shaders::DaoAccumulator::Method::UserLock::Type::BeamX;

			verify_test(RunGuarded_T(m_DaoAccumulator.m_Cid, args.s_iMethod, args));

			m_Height += 10;
		}

		{
			ByteBuffer buf;
			buf.resize(sizeof(Shaders::Upgradable3::Method::Control::ScheduleUpgrade) + m_DaoAccumulator.m_Code.size());
			auto& args = *reinterpret_cast<Shaders::Upgradable3::Method::Control::ScheduleUpgrade*>(&buf.front());

			ZeroObject(args);
			args.m_Type = args.s_Type;
			args.m_ApproveMask = 1u;
			args.m_SizeShader = (uint32_t) m_DaoAccumulator.m_Code.size();
			args.m_Next.m_hTarget = m_Height + 20;

			memcpy(&args + 1, &m_DaoAccumulator.m_Code.front(), m_DaoAccumulator.m_Code.size());

			Converter<Shaders::Upgradable3::Method::Control::ScheduleUpgrade> cvt(args);
			cvt.n += (uint32_t) m_DaoAccumulator.m_Code.size();

			verify_test(RunGuarded(m_DaoAccumulator.m_Cid, Shaders::Upgradable3::Method::Control::s_iMethod, cvt, nullptr));
		}

		{
			Shaders::Upgradable3::Method::Control::ExplicitUpgrade args;
			verify_test(!RunGuarded_T(m_DaoAccumulator.m_Cid, Shaders::Upgradable3::Method::Control::s_iMethod, args)); // too early

			m_Height += 20;
			verify_test(RunGuarded_T(m_DaoAccumulator.m_Cid, Shaders::Upgradable3::Method::Control::s_iMethod, args)); // too early
		}

	}

} // namespace bvm2


} // namespace beam

void Shaders::Env::CallFarN(const ContractID& cid, uint32_t iMethod, void* pArgs, uint32_t nArgs, uint32_t nFlags)
{
	Cast::Up<beam::bvm2::MyProcessor>(g_pEnv)->CallFarN(cid, iMethod, pArgs, nArgs, nFlags);
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

		//The integer 15 = [0x0F]
		{
			Rlp::Node n(15);
			ByteStream bs;
			n.Write(bs);
			verify_test(bs.m_Buffer == ByteBuffer({ 0x0F }));
			RlpVisitor v;
			verify_test(Rlp::Decode(bs.m_Buffer.data(), (uint32_t)bs.m_Buffer.size(), v));
			verify_test(v.m_Items[0].m_Type == Rlp::Node::Type::String && v.m_Items[0].m_Buffer == ByteBuffer({ 0x0f }));
		}

		//The integer 156 = [0x819C]
		{
			Rlp::Node n(156);
			ByteStream bs;
			n.Write(bs);
			verify_test(bs.m_Buffer == ByteBuffer({ 0x81, 0x9C }));
			RlpVisitor v;
			verify_test(Rlp::Decode(bs.m_Buffer.data(), (uint32_t)bs.m_Buffer.size(), v));
			verify_test(v.m_Items[0].m_Type == Rlp::Node::Type::String && v.m_Items[0].m_Buffer == ByteBuffer({ 0x9C }));
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

		/*{
			// eth event proof
			// block 0xbf1116, TX 0x5cc6f39c9060e489ca890ca8698ecbfe46a4f6e33bc5ab256e3f2aa98941db65, TX_Index = 0xb2
			auto buff = beam::from_hex("f90a75f90131a03560d9eed3444461ddfb5d160e0fb8a219f9024712a828021495d37f174559f2a07756074051552beda92a9e740b8e745b7e00d739055f8948d593dc00fbd3f7bfa0903307d973b0142fcd73b3356f0b722a8677ea1bde388dfc1aa9684397dd2f87a0a38b1ba99bdf4139007e99539f78fbb1ffb11c77390f97a0ce498ab2732cc5eba0cbf8a39475c1d6bb50748625194c31d8fd4a99881f5b031a02d78b7ad1f6f2dda01c7e262956bff1224f243ac306a5a36167604dba1548e409cb71450f2fb91140a0e9ee479aedfffb0c89c9e374e674199222ba81dd0b0225a4bffcc3bb8896e3c8a00e6ad449bd6eb9deae455939141b17f9119c8500a7d61ae58509b62d0968088fa0b4a234a3ecb24298915e64e2a1787a1723516706232e3eaafe40590c49211a4a8080808080808080f871a0f95633e95a8558a1f500445d05c19a147a7ef922379fe464c254ff55185ae5daa02e3d1d6607511ac52bc09b00b52f92c3d878814d1177dd9813e3e2dfb2107acea0bebe2a45f302d818753d6eb2eb4958fd1d34bd94f97114ce38335e52f438a82b8080808080808080808080808080f901118080808080808080a08843fb4bf42cd300485f04d507e98a802623aca141cd61c6a73503cfd6f0177ea0ac8c061c9a3f9d2e6f34dde923d4f0b7f5e5f216934056b89925ff520b181285a0c4baa35c1094223c148fb8f8ed54cfd9b310a72fdb1988e71c59935352b93d4fa0fab71929e2082ad0b8ba4d128a6421f575a3ef2d6a9eb59f3e82eef7e3ac39aca0223e2c8f3c24c63aece14f7f2caa2886d1b5c3f6dc5c113d09f0946e2e5394b3a037f2c55ff64db846deb1134da5c9353d3c106bf70d0d3b52cfda4700442025f0a0b2f6e85b04ad9da11f7481890fc09661e5ff5a0914aef8b69998de553261a543a0e1a76f686577614dd88e8b296d95e523da906d40d9a7fd743273af56c77b7b6f80f90211a0c69f64874dbe6a51102d2ff0128ee73789cca16645fe91751baa39cea188e80fa08068020d46ee81e30e0b6c4c8efc6c499f3d569011dc1ee4c59464b863e8c16fa0c2b8aa794e3d11ac07e289227ea9e53f204b57f871b6865505e81592c3e054a1a059caca8bfcb20aefcd8e83a10d69bf5523879edd2da89b5f93bbbdd20807f5eaa0ec482b119c6aad6517ccb50e2aaef3d9ccd9088b3270e5a3ea55e503a419e38ca0d9533bc5d3a338346f40b90b50237d48afd3e3262810129c7b6171da510aaf68a03273c53d5c9e084ef9cc5ecca211ecf145827414577225e7d9453514b9b14f49a0e68d0907dbf5a984785544c62c47ca3aea5436c53d8f2d022a90343ba1b16fe6a0422e65f5e046a14d94bd2811877f676d2cca9ad7feceda07aa5ddcc4697bebd2a029765e6d40bffda57b5050939f86cbd9a288ef6c1e6a6499479214a01056c8b2a01d37fadcef7cf5124579fedcc8000d7e149feb8ea88c99a4d3cc9177fbf3b837a017e8d9b19a9af8f98e381b988520d18114ee023d450bea11138c9ecd1bc37876a0239ef3ea46ee1f7fc8ba7535acf9554a5c26967c1e196a45bbbcdde6f0355719a0e96c6c1e3f8f9b87b99595cfecf9bfc98769a08f60de7f2d49400a2596558cb0a09e29700ebdfca58740871d10040d21bc8fa86c4c63a9234d554ab3f69c33ccffa0da550e277223653019e81070ffeeda8391d4829c6c266a13b5d86d8b4a3c305d80f905a320b9059ff9059c0183884e49b9010000000002000000000000000020000000000000000000000000000040000000000000000000000000000000100000000002000000080020000000000000000000000000000000000808200008400000000000000008000000000000008000000010000000000000000000180000000000000000000000002000000010000800000000008000000000000000000000000000000001010000000000000000000000000000000000200000000000800000000000080000020000000000008000000000000002200000020000000000000000000002000000000000000000000000000000200000000000008000000000000000000000000000400000000000000000f90491f89b94a0b86991c6218b36c1d19d4a2e9eb0ce3606eb48f863a0ddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3efa00000000000000000000000006c6bc977e13df9b0de53b251522280bb72383700a0000000000000000000000000a877184a3d42bf121c8182c3363427337ec49a3ba00000000000000000000000000000000000000000000000000000000023c34600f89b946b175474e89094c44da98b954eedeac495271d0ff863a0ddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3efa000000000000000000000000060594a405d53811d3bc4766596efd80fd545a270a00000000000000000000000006c6bc977e13df9b0de53b251522280bb72383700a000000000000000000000000000000000000000000000002082e413780b5db14af87a94c02aaa39b223fe8d0a0e5c4f27ead9083c756cc2f842a0e1fffcc4923d04b559f4d29a8bfc6cda04eb5b0d3c460751c2402c5c5cc9109ca0000000000000000000000000e592427a0aece92de3edee1f18e0157c05861564a00000000000000000000000000000000000000000000000000341252927ea3fbff89b94c02aaa39b223fe8d0a0e5c4f27ead9083c756cc2f863a0ddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3efa0000000000000000000000000e592427a0aece92de3edee1f18e0157c05861564a000000000000000000000000060594a405d53811d3bc4766596efd80fd545a270a00000000000000000000000000000000000000000000000000341252927ea3fbff9011c9460594a405d53811d3bc4766596efd80fd545a270f863a0c42079f94a6350d7e6235f29174924f928cc2ac818eb64fed8004e115fbcca67a0000000000000000000000000e592427a0aece92de3edee1f18e0157c05861564a00000000000000000000000006c6bc977e13df9b0de53b251522280bb72383700b8a0ffffffffffffffffffffffffffffffffffffffffffffffdf7d1bec87f4a24eb60000000000000000000000000000000000000000000000000341252927ea3fbf000000000000000000000000000000000000000005100f011f27a5e41938a53c0000000000000000000000000000000000000000000003a215dfb35f0e0caf8efffffffffffffffffffffffffffffffffffffffffffffffffffffffffffecd7af9011c946c6bc977e13df9b0de53b251522280bb72383700f863a0c42079f94a6350d7e6235f29174924f928cc2ac818eb64fed8004e115fbcca67a0000000000000000000000000e592427a0aece92de3edee1f18e0157c05861564a0000000000000000000000000a877184a3d42bf121c8182c3363427337ec49a3bb8a000000000000000000000000000000000000000000000002082e413780b5db14affffffffffffffffffffffffffffffffffffffffffffffffffffffffdc3cba000000000000000000000000000000000000000000000010c9047004407a08eaaf000000000000000000000000000000000000000000001206bbf0ef217797f1e7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffbc8a5");
			HashValue hv;
			hv.Scan("885c604594f953fb56cdc3204f2c91d0992bd60912717c29c2601ed83b34bd45");

			uint8_t* out = nullptr;
			uint32_t outSize = 0;

			auto triePath = beam::from_hex("81b2");
			uint32_t nibblesLength = (uint32_t)(2 * triePath.size());
			auto pathInNibbles = std::make_unique<uint8_t[]>(nibblesLength);

			TriePathToNibbles(triePath.data(), (uint32_t)triePath.size(), pathInNibbles.get(), nibblesLength);

			verify_test(VerifyEthProof(pathInNibbles.get(), nibblesLength, buff.data(), (uint32_t)buff.size(), hv, &out, outSize));
			auto expected = beam::from_hex("f9059c0183884e49b9010000000002000000000000000020000000000000000000000000000040000000000000000000000000000000100000000002000000080020000000000000000000000000000000000808200008400000000000000008000000000000008000000010000000000000000000180000000000000000000000002000000010000800000000008000000000000000000000000000000001010000000000000000000000000000000000200000000000800000000000080000020000000000008000000000000002200000020000000000000000000002000000000000000000000000000000200000000000008000000000000000000000000000400000000000000000f90491f89b94a0b86991c6218b36c1d19d4a2e9eb0ce3606eb48f863a0ddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3efa00000000000000000000000006c6bc977e13df9b0de53b251522280bb72383700a0000000000000000000000000a877184a3d42bf121c8182c3363427337ec49a3ba00000000000000000000000000000000000000000000000000000000023c34600f89b946b175474e89094c44da98b954eedeac495271d0ff863a0ddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3efa000000000000000000000000060594a405d53811d3bc4766596efd80fd545a270a00000000000000000000000006c6bc977e13df9b0de53b251522280bb72383700a000000000000000000000000000000000000000000000002082e413780b5db14af87a94c02aaa39b223fe8d0a0e5c4f27ead9083c756cc2f842a0e1fffcc4923d04b559f4d29a8bfc6cda04eb5b0d3c460751c2402c5c5cc9109ca0000000000000000000000000e592427a0aece92de3edee1f18e0157c05861564a00000000000000000000000000000000000000000000000000341252927ea3fbff89b94c02aaa39b223fe8d0a0e5c4f27ead9083c756cc2f863a0ddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3efa0000000000000000000000000e592427a0aece92de3edee1f18e0157c05861564a000000000000000000000000060594a405d53811d3bc4766596efd80fd545a270a00000000000000000000000000000000000000000000000000341252927ea3fbff9011c9460594a405d53811d3bc4766596efd80fd545a270f863a0c42079f94a6350d7e6235f29174924f928cc2ac818eb64fed8004e115fbcca67a0000000000000000000000000e592427a0aece92de3edee1f18e0157c05861564a00000000000000000000000006c6bc977e13df9b0de53b251522280bb72383700b8a0ffffffffffffffffffffffffffffffffffffffffffffffdf7d1bec87f4a24eb60000000000000000000000000000000000000000000000000341252927ea3fbf000000000000000000000000000000000000000005100f011f27a5e41938a53c0000000000000000000000000000000000000000000003a215dfb35f0e0caf8efffffffffffffffffffffffffffffffffffffffffffffffffffffffffffecd7af9011c946c6bc977e13df9b0de53b251522280bb72383700f863a0c42079f94a6350d7e6235f29174924f928cc2ac818eb64fed8004e115fbcca67a0000000000000000000000000e592427a0aece92de3edee1f18e0157c05861564a0000000000000000000000000a877184a3d42bf121c8182c3363427337ec49a3bb8a000000000000000000000000000000000000000000000002082e413780b5db14affffffffffffffffffffffffffffffffffffffffffffffffffffffffdc3cba000000000000000000000000000000000000000000000010c9047004407a08eaaf000000000000000000000000000000000000000000001206bbf0ef217797f1e7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffbc8a5");
			verify_test(expected.size() == outSize);
			verify_test(!memcmp(expected.data(), out, std::min((size_t)outSize, expected.size())));
		}*/

		{
			// find bridge event
			auto buff = beam::from_hex("f9033f01829297b9010000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000002000000200000000000000000000010004008000000000000000000000000000000000000000000004000000000000000000000000000000000000000000000000010000000000000000000000000000000000000000100000000000000000000010000000002020000000000000000000000000000000000000000000000000000000000000000000002000000000000000000010000000000000000000000000000001000000010000020000020000000000100000000000000000000000001008800000000f90235f89b94d8672a4a1bf37d36bef74e36edb4f17845e76f4ef863a0ddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3efa0000000000000000000000000627306090abab3a6e1400e9345bc60c78a8bef57a0000000000000000000000000fa21e79ca2dfb3ab15469796069622903919159ca000000000000000000000000000000000000000000000000000000000006acfc0f89b94d8672a4a1bf37d36bef74e36edb4f17845e76f4ef863a08c5be1e5ebec7d5bd14f71427d1e84f3dd0314c0f7b2291e5b200ac8c7c3b925a0000000000000000000000000627306090abab3a6e1400e9345bc60c78a8bef57a0000000000000000000000000fa21e79ca2dfb3ab15469796069622903919159ca00000000000000000000000000000000000000000000000000000000000000000f8f994fa21e79ca2dfb3ab15469796069622903919159ce1a02ee2b75bef6a1e98db7f58994a8997fc42664e7d47f2116cec5cd82dc67233cab8c0000000000000000000000000627306090abab3a6e1400e9345bc60c78a8bef5700000000000000000000000000000000000000000000000000000000006acfc0000000000000000000000000000000000000000000000000000000000000006000000000000000000000000000000000000000000000000000000000000000214209059b49805c3317c69edb7acd181271ad8d13fbfc528224775310cf03a9620100000000000000000000000000000000000000000000000000000000000000");
			RlpVisitor v0;

			verify_test(Rlp::Decode(buff.data(), (uint32_t)buff.size(), v0));
			verify_test(v0.m_Items.size() == 1);
			verify_test(v0.m_Items.front().m_Type == Rlp::Node::Type::List);

			RlpVisitor v1;
			verify_test(Rlp::Decode(v0.m_Items.front().m_Buffer.data(), (uint32_t)v0.m_Items.front().m_Buffer.size(), v1));
			verify_test(v1.m_Items.size() == 4);
			verify_test(v1.m_Items.back().m_Type == Rlp::Node::Type::List);

			// get event list
			RlpVisitor v2;
			verify_test(Rlp::Decode(v1.m_Items.back().m_Buffer.data(), (uint32_t)v1.m_Items.back().m_Buffer.size(), v2));
			verify_test(v2.m_Items.size() > 0);
			bool isFound = false;
			const ByteBuffer expectedTopic = beam::from_hex("2ee2b75bef6a1e98db7f58994a8997fc42664e7d47f2116cec5cd82dc67233ca");
			for (auto idx = v2.m_Items.begin(); idx != v2.m_Items.end(); ++idx)
			{
				RlpVisitor eventNode;
				verify_test(Rlp::Decode(idx->m_Buffer.data(), (uint32_t)idx->m_Buffer.size(), eventNode));
				verify_test(eventNode.m_Items.size() == 3);

				RlpVisitor topics;
				verify_test(Rlp::Decode(eventNode.m_Items[1].m_Buffer.data(), (uint32_t)eventNode.m_Items[1].m_Buffer.size(), topics));

				if (topics.m_Items.size() != 1) continue;
				if (topics.m_Items.front().m_Buffer.size() != expectedTopic.size()) continue;
				if (!memcmp(topics.m_Items.front().m_Buffer.data(), expectedTopic.data(), expectedTopic.size()))
				{
					isFound = true;
					break;
				}
			}

			verify_test(isFound);
		}

		{
			// verify log data
			auto buffer = beam::from_hex("00000000000000000000000000000000000000000000000000000000000000060000000000000000000000008f0483125fcb9aaaefa9209d8e9d7b9c8b9fb90fb2ca0cefbd65320247f2b13e7b8dff4cd39652a53227a54fbaa0fe339be1b1f300000000000000000000000000000000000000000000000000000000000000800000000000000000000000000000000000000000000000000000000000000029a340e205eafbe2e23d65d807b7678945f6e85cf9b73ba98e4cf2bfab7def2dc30100000000006acfc00000000000000000000000000000000000000000000000");

			uint8_t* idx = buffer.data();
			Opaque<32> rawNumber;
			memcpy(&rawNumber, idx, 32);
			MultiPrecision::UInt<8> msgId;
			msgId.FromBE_T(rawNumber);
			MultiPrecision::UInt<8> expectedMsgId = 6u;

			verify_test(msgId == expectedMsgId);

			idx += 32;
			Opaque<20> senderAddress;
			memcpy(&senderAddress, idx + 12, 20);
			auto expectedAddress = beam::from_hex("8f0483125FCb9aaAEFA9209D8E9d7b9C8B9Fb90F");
			verify_test(!memcmp(&senderAddress, expectedAddress.data(), 20));

			idx += 32;
			Opaque<32> receiverAddress;
			memcpy(&receiverAddress, idx, 32);
			auto expectedReceiverAddress = beam::from_hex("b2ca0cefbd65320247f2b13e7b8dff4cd39652a53227a54fbaa0fe339be1b1f3");
			verify_test(!memcmp(&receiverAddress, expectedReceiverAddress.data(), 32));

			// offset
			idx += 32;
			memcpy(&rawNumber, idx, 32);
			MultiPrecision::UInt<8> offset;
			offset.FromBE_T(rawNumber);
			MultiPrecision::UInt<8> expectedOffset = 128;
			verify_test(offset == expectedOffset);

			// size
			idx += 32;
			memcpy(&rawNumber, idx, 32);
			MultiPrecision::UInt<8> size;
			size.FromBE_T(rawNumber);
			MultiPrecision::UInt<8> expectedSize = 41;
			verify_test(size == expectedSize);

			// read msg
			idx += 32;
			auto expectedData = beam::from_hex("a340e205eafbe2e23d65d807b7678945f6e85cf9b73ba98e4cf2bfab7def2dc30100000000006acfc0");
			verify_test(!memcmp(idx, expectedData.data(), 41));
		}

		{
			// verify result of proof
			auto buff = beam::from_hex("f9033f01829297b9010000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000002000000200000000000000000000010004008000000000000000000000000000000000000000000004000000000000000000000000000000000000000000000000010000000000000000000000000000000000000000100000000000000000000010000000002020000000000000000000000000000000000000000000000000000000000000000000002000000000000000000010000000000000000000000000000001000000010000020000020000000000100000000000000000000000001008800000000f90235f89b94d8672a4a1bf37d36bef74e36edb4f17845e76f4ef863a0ddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3efa0000000000000000000000000627306090abab3a6e1400e9345bc60c78a8bef57a0000000000000000000000000fa21e79ca2dfb3ab15469796069622903919159ca000000000000000000000000000000000000000000000000000000000006acfc0f89b94d8672a4a1bf37d36bef74e36edb4f17845e76f4ef863a08c5be1e5ebec7d5bd14f71427d1e84f3dd0314c0f7b2291e5b200ac8c7c3b925a0000000000000000000000000627306090abab3a6e1400e9345bc60c78a8bef57a0000000000000000000000000fa21e79ca2dfb3ab15469796069622903919159ca00000000000000000000000000000000000000000000000000000000000000000f8f994fa21e79ca2dfb3ab15469796069622903919159ce1a02ee2b75bef6a1e98db7f58994a8997fc42664e7d47f2116cec5cd82dc67233cab8c0000000000000000000000000627306090abab3a6e1400e9345bc60c78a8bef5700000000000000000000000000000000000000000000000000000000006acfc0000000000000000000000000000000000000000000000000000000000000006000000000000000000000000000000000000000000000000000000000000000214209059b49805c3317c69edb7acd181271ad8d13fbfc528224775310cf03a9620100000000000000000000000000000000000000000000000000000000000000");
			RlpVisitor v;

			verify_test(Rlp::Decode(buff.data(), (uint32_t)buff.size(), v));
			RlpVisitor v1;
			verify_test(Rlp::Decode(v.m_Items.front().m_Buffer.data(), (uint32_t)v.m_Items.front().m_Buffer.size(), v1));
			RlpVisitor v2;
			verify_test(Rlp::Decode(v1.m_Items.back().m_Buffer.data(), (uint32_t)v1.m_Items.back().m_Buffer.size(), v2));
			RlpVisitor v3;
			verify_test(Rlp::Decode(v2.m_Items.back().m_Buffer.data(), (uint32_t)v2.m_Items.back().m_Buffer.size(), v3));
			verify_test(v3.m_Items.back().m_Buffer.size() == 192);

			// check receiver address
			verify_test(v3.m_Items.front().m_Buffer.size() == 20);
			uint8_t* address = v3.m_Items.front().m_Buffer.data();
			ByteBuffer expectedAddress = beam::from_hex("fa21e79ca2dfb3ab15469796069622903919159c");
			verify_test(!memcmp(address, expectedAddress.data(), 20));

			// check topic (hash of event name)
			RlpVisitor v4;
			verify_test(Rlp::Decode(v3.m_Items[1].m_Buffer.data(), (uint32_t)v3.m_Items[1].m_Buffer.size(), v4));
			verify_test(v4.m_Items.size() == 1);
			ByteBuffer expectedTopic = beam::from_hex("2ee2b75bef6a1e98db7f58994a8997fc42664e7d47f2116cec5cd82dc67233ca");
			verify_test(v4.m_Items.front().m_Buffer.size() == expectedTopic.size());
			uint8_t* firstTopic = v4.m_Items.front().m_Buffer.data();
			verify_test(!memcmp(firstTopic, expectedTopic.data(), expectedTopic.size()));

			// check data item
			uint8_t* tmp = v3.m_Items.back().m_Buffer.data();
			tmp += 32;
			Opaque<32> rawNumber;
			memcpy(&rawNumber, tmp, 32);
			MultiPrecision::UInt<8> amount;
			amount.FromBE_T(rawNumber);
			MultiPrecision::UInt<8> amount1;
			amount1 = 7000000u;
			verify_test(!amount.cmp(amount1));

			// check offset
			tmp += 32;
			memcpy(&rawNumber, tmp, 32);
			amount.FromBE_T(rawNumber);
			amount1 = 96u;
			verify_test(!amount.cmp(amount1));

			// check size
			tmp += 32;
			memcpy(&rawNumber, tmp, 32);
			amount.FromBE_T(rawNumber);
			amount1 = 33u;
			verify_test(!amount.cmp(amount1));

			// check public key
			tmp += 32;
			PubKey pubKey;
			memcpy(reinterpret_cast<uint8_t*>(&pubKey), tmp, 33);
			PubKey expectPubKey;
			memcpy(reinterpret_cast<uint8_t*>(&expectPubKey), beam::from_hex("4209059b49805c3317c69edb7acd181271ad8d13fbfc528224775310cf03a96201").data(), 33);

			verify_test(pubKey == expectPubKey);
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
	void TestEthSeedForPoW()
	{
		{
			Shaders::Eth::Header hdr;
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

			Shaders::Dummy::Ethash::Hash512 hvExpected;
			hvExpected.Scan("fcbb65e35afc98de2ea3729c18d8fa3872e5088c82538e99e0cb58a5482cb17602a0c19b9dd0cb0b01f1db3769c9c0e48976240233855c5a11315aa9f2a1bb28");
			verify_test(!hvSeed.cmp(hvExpected));
		}
		{
			// London HF: block with additional field BaseFeePerGas
			// Block from ETH testnet Ropsten
			Shaders::Eth::Header hdr;
			hdr.m_ParentHash.Scan("b3d1cd15530aa9bd06d774ee203696ddcc52a831fb7dfc48ac890a25d53e4ca1");
			hdr.m_UncleHash.Scan("d95f5aa2ee654ed2177faf2002f16e670098d1642701d8855bf8cd7ebad021f8");
			hdr.m_Coinbase.Scan("9ffed2297c7b81293413550db675073ab46980b2");
			hdr.m_Root.Scan("5fa10298d75a783f80e25f9fd5f1a2ef0f1288ec511900c648695a933a9cec21");
			hdr.m_TxHash.Scan("e0da8cdae41fa8188600590da87866fd2782b15478914d02b0123e6870e25a1a");
			hdr.m_ReceiptHash.Scan("88be83e41f8fb35ecae55c1c2debb7d11c915c242a83db6e857eb11bfffdf3be");
			hdr.m_Bloom.Scan("0020000800000002000000000000000800008000000000000080000000000000010000000000002000080000008000000000000000100000000000000020000000001000002000080000000840000081000180000200000800000000000000080000000002000800000080000010080000000000000000000000101000000040000080000000004020000000000000000020000000000000000000010000008002000000a800000000000000000004000000001000001000000005000080020100000002000040000000000000000041001010000000000000000000000020000010100000000000080000000001000000000200000000000000000200000200");
			hdr.m_nExtra = hdr.m_Extra.Scan("d883010a05846765746888676f312e31362e35856c696e7578") / 2;
			hdr.m_Difficulty = 0x2d4c89a1;
			hdr.m_Number = 0xa2df0c; // height
			hdr.m_GasLimit = 0x7a1200;
			hdr.m_GasUsed = 0x5b7a79;
			hdr.m_Time = 0x60f6f41e;
			hdr.m_Nonce = 0xdc889a9752ae31e2;
			hdr.m_BaseFeePerGas = 0x9;

			Shaders::Dummy::Ethash::Hash512 hvSeed;
			hdr.get_SeedForPoW(hvSeed);

			Shaders::Dummy::Ethash::Hash512 hvExpected;
			hvExpected.Scan("813c4a1bc1fa0ea152eb8202f15b175a5288c62626a93efb0c285f314960091f4702f01a5c0d9a4a2cea6d7ba3ad74fba16e76352dd57a209b25e7467c59f181");
			verify_test(!hvSeed.cmp(hvExpected));
		}
	}
}

thread_local const beam::Rules* beam::Rules::s_pInstance = nullptr;

int main()
{
	beam::Rules r;
	beam::Rules::Scope scopeRules(r);

	try
	{
		ECC::PseudoRandomGenerator prg;
		ECC::PseudoRandomGenerator::Scope scope(&prg);

		using namespace beam;
		using namespace beam::bvm2;

		TestMergeSort();
		TestRLP();
		TestEthSeedForPoW();

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

		for (uint32_t i = 0; i <= 6; i++)
			r.pForks[i].m_Height = 0;

		r.DisableForksFrom(7);
		r.UpdateChecksum();

		proc.m_Height = 10;
		proc.TestAll();

		MyManager man(proc);
		man.InitMem();
		man.TestHeap();
		man.SetCode("vault/app.wasm");

		man.RunGuarded(0); // get scheme

		man.m_Args["role"] = "manager";
		man.m_Args["action"] = "view_accounts";
		man.set_ArgBlob("cid", Shaders::Vault::s_CID);

		man.RunGuarded(1);

	}
	catch (const std::exception & ex)
	{
		printf("Expression: %s\n", ex.what());
		g_TestsFailed++;
	}

	return g_TestsFailed ? -1 : 0;
}
