#define HOST_BUILD

#include "../../core/block_rw.h"
#include "../../core/keccak.h"
#include "../../utility/test_helpers.h"
#include "../../utility/blobmap.h"
#include "../bvm2.h"
#include "../bvm2_impl.h"

#include "../ethash_service/ethash_utils.h"

#if defined(__ANDROID__) || !defined(BEAM_USE_AVX)
#include "crypto/blake/ref/blake2.h"
#else
#include "crypto/blake/sse/blake2.h"
#endif

namespace Shaders
{
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
#include "../Shaders/BeamHeader.h"
#include "../Shaders/Eth.h"

#include "../Shaders/bridge/contract.h"
#include "../Shaders/mirrortoken/contract.h"

	/*template <bool bToShader> void Convert(Pipe::Create& x) {
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
	}*/

	//template <bool bToShader> void Convert(Bridge::Unlock& x) {
	//}

	//template <bool bToShader> void Convert(Bridge::Lock& x) {
	//}

	//template <bool bToShader> void Convert(Bridge::ImportMessage& x) {
	//	/*ConvertOrd<bToShader>(x.m_Header.m_Difficulty);
	//	ConvertOrd<bToShader>(x.m_Header.m_Number);
	//	ConvertOrd<bToShader>(x.m_Header.m_GasLimit);
	//	ConvertOrd<bToShader>(x.m_Header.m_GasUsed);
	//	ConvertOrd<bToShader>(x.m_Header.m_Time);
	//	ConvertOrd<bToShader>(x.m_Header.m_Nonce);
	//	ConvertOrd<bToShader>(x.m_DatasetCount);*/
	//}

	//template <bool bToShader> void Convert(Bridge::Finalized& x) {
	//}

	namespace Env
	{
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

		void CallFarN(const ContractID& cid, uint32_t iMethod, void* pArgs, uint32_t nArgs, uint8_t bInheritContext);

		template <typename T>
		void CallFar_T(const ContractID& cid, T& args, uint8_t bInheritContext = 0)
		{
			Convert<true>(args);
			CallFarN(cid, args.s_iMethod, &args, sizeof(args), bInheritContext);
			Convert<false>(args);
		}
	}

	namespace Pipe {
#include "../Shaders/bridge/contract.cpp"
	}

#ifdef _MSC_VER
#	pragma warning (default : 4200 4702)
#endif // _MSC_VER
} // namespace Shaders

namespace ECC 
{
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

namespace beam::bvm2
{
    struct MyProcessor : public ProcessorContract
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

		virtual uint32_t SaveVar(const VarKey& vk, const uint8_t* pVal, uint32_t nVal) override
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

		uint32_t SaveVar(const Blob& key, const uint8_t* pVal, uint32_t nVal)
		{
			auto pUndo = std::make_unique<Action_Var>();
			uint32_t nRet = SaveVar2(key, pVal, nVal, pUndo.get());

			m_lstUndo.push_back(*pUndo.release());
			return nRet;
		}

		uint32_t SaveVar2(const Blob& key, const uint8_t* pVal, uint32_t nVal, Action_Var* pAction)
		{
			auto* pE = m_Vars.Find(key);
			auto nOldSize = pE ? static_cast<uint32_t>(pE->m_Data.size()) : 0;

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

			return nOldSize;
		}

		Height m_Height = 0;
		Height get_Height() override { return m_Height; }

		uint32_t m_Cycles;

		void CallFarN(const ContractID& cid, uint32_t iMethod, void* pArgs, uint32_t nArgs, uint8_t bInheritContext)
		{
			m_Stack.AliasAlloc(nArgs);
			memcpy(m_Stack.get_AliasPtr(), pArgs, nArgs);

			size_t nFrames = m_FarCalls.m_Stack.size();

			Wasm::Word nSp = m_Stack.get_AlasSp();
			CallFar(cid, iMethod, nSp);

			if (bInheritContext)
			{
				auto it = m_FarCalls.m_Stack.rbegin();
				auto& fr0 = *it;
				auto& fr1 = *(++it);
				fr0.m_Cid = fr1.m_Cid;
			}

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
			//m_Dbg.m_pOut = &os;

			os << "BVM Method: " << cid << ":" << iMethod << std::endl;

			InitStackPlus(0);

			HeapReserveStrict(get_HeapLimit()); // this is necessary as long as we run shaders natively (not via wasm). Heap mem should not be reallocated

			m_Charge = Limits::BlockCharge; // default
			uint32_t nUnitsMax = m_Charge;

			Shaders::Env::g_pEnv = this;
			m_Cycles = 0;

			CallFarN(cid, iMethod, Cast::NotConst(args.p), args.n, 0);

			os << "Done in " << m_Cycles << " cycles, Discharge=" << (nUnitsMax - m_Charge) << std::endl << std::endl;
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
		struct Converter : public Blob
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
			ByteBuffer m_Bridge;
			ByteBuffer m_MirrorToken;
		} m_Code;

		ContractID m_cidBridge;
		ContractID m_cidMirrorToken;

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
				m_Frame.m_FarRetAddr = 0;
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
			/*if (cid == m_cidPipe)
			{
				TempFrame f(*this, cid);
				switch (iMethod)
				{
				case 0: Shaders::Pipe::Ctor(CastArg<Shaders::Pipe::Create>(pArgs)); return;
				case 2: Shaders::Pipe::Method_2(CastArg<Shaders::Pipe::SetRemote>(pArgs)); return;
				case 3: Shaders::Pipe::Method_3(CastArg<Shaders::Pipe::PushLocal0>(pArgs)); return;
				case 4: Shaders::Pipe::Method_4(CastArg<Shaders::Pipe::PushRemote0>(pArgs)); return;
				case 5: Shaders::Pipe::Method_5(CastArg<Shaders::Pipe::FinalyzeRemote>(pArgs)); return;
				case 6: Shaders::Pipe::Method_6(CastArg<Shaders::Pipe::ReadRemote0>(pArgs)); return;
				case 7: Shaders::Pipe::Method_7(CastArg<Shaders::Pipe::Withdraw>(pArgs)); return;
				}
			}*/

			ProcessorContract::CallFar(cid, iMethod, pArgs);
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

		void TestBridge()
		{		
			Zero_ zero;
			verify_test(ContractCreate_T(m_cidBridge, m_Code.m_Bridge, zero));

			bvm2::ShaderID sid;
			bvm2::get_ShaderID(sid, m_Code.m_Bridge);

			CidTxt ct;
			ct.Set(sid);

			std::cout << "TestBridge" << std::endl;
		}

		void TestMirrorToken()
		{
			Zero_ zero;
			verify_test(ContractCreate_T(m_cidMirrorToken, m_Code.m_MirrorToken, zero));

			bvm2::ShaderID sid;
			bvm2::get_ShaderID(sid, m_Code.m_MirrorToken);

			CidTxt ct;
			ct.Set(sid);

			std::cout << "TestMirrorToken" << std::endl;
		}

        void TestAll()
        {
            AddCode(m_Code.m_Bridge, "bridge/contract.wasm");
			AddCode(m_Code.m_MirrorToken, "mirrortoken/contract.wasm");

            TestBridge();
			TestMirrorToken();
        }
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
} // namespace beam::bvm2

int main()
{
    try
    {
        using namespace beam;
        using namespace beam::bvm2;

        MyProcessor myProcessor;

        myProcessor.TestAll();
    }
    catch (const std::exception& ex)
    {
		printf("Expression: %s\n", ex.what());
		g_TestsFailed++;
    }

	return g_TestsFailed ? -1 : 0;
}