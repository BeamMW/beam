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

#if defined(__ANDROID__) || !defined(BEAM_USE_AVX)
#include "crypto/blake/ref/blake2.h"
#else
#include "crypto/blake/sse/blake2.h"
#endif

namespace Shaders {

	namespace Merkle {
		using namespace beam::Merkle;
	}

	namespace Env {

		extern "C" {

#define PAR_DECL(type, name) type name
#define MACRO_COMMA ,

#define THE_MACRO(id, ret, name) ret name(BVMOp_##name(PAR_DECL, MACRO_COMMA));
			BVMOpsAll_Common(THE_MACRO)
			BVMOpsAll_Contract(THE_MACRO)
			BVMOpsAll_Manager(THE_MACRO)
#undef THE_MACRO

#undef MACRO_COMMA
#undef PAR_DECL

		} // extern "C"

	} // namespace Env


#ifdef _MSC_VER
#	pragma warning (disable : 4200 4702) // unreachable code
#endif // _MSC_VER

#define export

#include "../Shaders/common.h"
#include "../Shaders/Math.h"
#include "../Shaders/MergeSort.h"
#include "../Shaders/BeamHeader.h"

#include "../Shaders/vault/contract.h"
#include "../Shaders/oracle/contract.h"
#include "../Shaders/dummy/contract.h"
#include "../Shaders/StableCoin/contract.h"
#include "../Shaders/faucet/contract.h"
#include "../Shaders/roulette/contract.h"
#include "../Shaders/sidechain/contract.h"
#include "../Shaders/perpetual/contract.h"
#include "../Shaders/pipe/contract.h"

	template <bool bToShader> void Convert(Vault::Request& x) {
		ConvertOrd<bToShader>(x.m_Aid);
		ConvertOrd<bToShader>(x.m_Amount);
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
			Convert<true>(args);
			CallFarN(cid, args.s_iMethod, &args, sizeof(args));
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

		Height m_Height = 0;
		Height get_Height() override { return m_Height; }

		bool get_HdrAt(Block::SystemState::Full& s) override
		{
			Height h = s.m_Height;
			if (h > m_Height)
				return false;

			ZeroObject(s);
			s.m_Height = h;
			return true;
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

				DischargeUnits(Limits::Cost::Cycle);
				RunOnce();

				if (m_Dbg.m_pOut)
				{
					std::cout << m_Dbg.m_pOut->str();
					m_Dbg.m_pOut->str("");

					if (m_Cycles >= 100000)
						m_Dbg.m_pOut = nullptr; // in debug max num of cycles takes too long because if this
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

			m_Charge = Limits::Charge(); // default
			uint32_t nUnitsMax = m_Charge.m_Units;

			Shaders::Env::g_pEnv = this;
			m_Cycles = 0;

			CallFarN(cid, iMethod, Cast::NotConst(args.p), args.n);

			os << "Done in " << m_Cycles << " cycles, Discharge=" << (nUnitsMax - m_Charge.m_Units) << std::endl << std::endl;
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
				Shaders::Convert<true>(arg);
				p = &arg;
				n = static_cast<uint32_t>(sizeof(arg));
			}

			~Converter()
			{
				T& arg = Cast::NotConst(*reinterpret_cast<const T*>(p));
				Shaders::Convert<false>(arg);
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
			ByteBuffer m_Sidechain;
			ByteBuffer m_StableCoin;
			ByteBuffer m_Faucet;
			ByteBuffer m_Roulette;
			ByteBuffer m_Perpetual;
			ByteBuffer m_Pipe;

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
				//}
			}

			ProcessorContract::CallFar(cid, iMethod, pArgs);
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

		TestVault();
		TestFaucet();
		TestRoulette();
		TestDummy();
		TestSidechain();
		TestOracle();
		TestStableCoin();
		TestPerpetual();
		TestPipe();
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


	void MyProcessor::TestDummy()
	{
		ContractID cid;
		Zero_ zero;
		verify_test(ContractCreate_T(cid, m_Code.m_Dummy, zero));
		m_cidDummy = cid;

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
			Dbg dbg = m_Dbg;
			m_Dbg.m_Instructions = false;
			m_Dbg.m_Stack = false;
			m_Dbg.m_ExtCall = false;

			Shaders::Dummy::InfCycle args;
			args.m_Val = 12;
			verify_test(!RunGuarded_T(cid, args.s_iMethod, args));

			m_Dbg = dbg;
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

			memset(args.m_pInp, 'r', sizeof(args.m_pInp));
			verify_test(RunGuarded_T(cid, args.s_iMethod, args));

			static const uint8_t pRes[] = { 0x80,0x3c,0xa0,0xf1,0x15,0xba,0x94,0x06,0xd3,0x78,0x15,0x9f,0xe7,0x34,0x32,0x0e,0x35,0x24,0xf6,0x07,0xf6,0x1c,0x17,0xb0,0x11,0x98,0xe3,0xd3,0xc4,0xfa,0x6a,0xd1 };

			static_assert(sizeof(pRes) == sizeof(args.m_pRes));
			verify_test(!memcmp(pRes, args.m_pRes, sizeof(args.m_pRes)));
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

			Dbg dbg = m_Dbg;
			m_Dbg.m_Instructions = false;
			m_Dbg.m_Stack = false;
			m_Dbg.m_ExtCall = false;

			verify_test(RunGuarded_T(cid, args.s_iMethod, args));

			m_Dbg = dbg;

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
		verify_test(sid == Shaders::Sidechain::s_SID);

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

		//verify_test(ContractDestroy_T(argSc.m_RateOracle, zero));
	}

	void MyProcessor::TestPerpetual()
	{
		bvm2::ShaderID sid;
		bvm2::get_ShaderID(sid, m_Code.m_Perpetual);
		verify_test(sid == Shaders::Perpetual::s_SID);

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
		verify_test(sid == Shaders::Pipe::s_SID);

		{
			Shaders::Pipe::Create arg;
			ZeroObject(arg);
			arg.m_Cfg.m_Out.m_CheckpointMaxDH = 5;
			arg.m_Cfg.m_Out.m_CheckpointMaxMsgs = 3;

			verify_test(ContractCreate_T(m_cidPipe, m_Code.m_Pipe, arg));
		}

		{
			Shaders::Pipe::SetRemote arg;
			ZeroObject(arg);
			verify_test(!RunGuarded_T(m_cidPipe, arg.s_iMethod, arg));

			arg.m_cid.Inc();
			verify_test(RunGuarded_T(m_cidPipe, arg.s_iMethod, arg));
		}

		{
			Shaders::Pipe::PushLocal0 arg;
			ZeroObject(arg);

			for (uint32_t i = 0; i < 10; i++)
				verify_test(RunGuarded_T(m_cidPipe, arg.s_iMethod, arg));
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
		verify_test(sid == Shaders::Faucet::s_SID);

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
		verify_test(sid == Shaders::Roulette::s_SID);

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
			m_Dbg.m_pOut = &os;

			os << "BVM Method: " << iMethod << std::endl;

			Shaders::Env::g_pEnv = this;

			uint32_t nCycles = 0;
			uint32_t nDepth = m_LocalDepth;

			for (CallMethod(iMethod); m_LocalDepth != nDepth; nCycles++)
			{
				RunOnce();

				if (m_Dbg.m_pOut)
				{
					std::cout << m_Dbg.m_pOut->str();
					m_Dbg.m_pOut->str("");
				}
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
