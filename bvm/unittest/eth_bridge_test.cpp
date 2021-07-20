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
		void CallFarN(const ContractID& cid, uint32_t iMethod, void* pArgs, uint32_t nArgs, uint8_t bInheritContext);

		template <typename T>
		void CallFar_T(const ContractID& cid, T& args, uint8_t bInheritContext = 0)
		{
			Convert<true>(args);
			CallFarN(cid, args.s_iMethod, &args, sizeof(args), bInheritContext);
			Convert<false>(args);
		}
	}

	namespace Bridge {
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

#include "contract_test_processor.h"

namespace beam::bvm2
{
    struct MyProcessor : public ContractTestProcessor
    {
		struct Code
		{
			ByteBuffer m_Bridge;
			ByteBuffer m_MirrorToken;
		} m_Code;

		ContractID m_cidBridge;
		ContractID m_cidMirrorToken;

		virtual void CallFar(const ContractID& cid, uint32_t iMethod, Wasm::Word pArgs) override
		{
			if (cid == m_cidBridge)
			{
				/*TempFrame f(*this, cid);
				switch (iMethod)
				{
				case 0: Shaders::Bridge::Ctor(nullptr); return;
				case 2: Shaders::Bridge::Method_2(CastArg<Shaders::Bridge::PushLocal>(pArgs)); return;
				}*/
			}

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