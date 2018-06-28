#include "../node.h"
#include "utility/logger.h"

#include <boost/program_options.hpp>

namespace po = boost::program_options;
using namespace beam;
using namespace ECC;


Initializer g_Initializer;

class TestNodeConnection : public proto::NodeConnection
{
public:
	TestNodeConnection(const std::string& seed);

	int CheckOnFailed();
private:
	void OnConnected() override;
	void OnClosed(int errorCode) override;
	bool OnMsg2(proto::Boolean&& msg) override;

	void RunTest();

	void GenerateInputInTx(Height h, Amount v);
	void GenerateOutputInTx(Height h, Amount v);
	void GenerateKernel(Height h);
	void GenerateTests();
private:
	ECC::Kdf m_Kdf;
	io::Timer::Ptr m_Timer;
	bool m_Failed;
	proto::NewTransaction m_MsgTx;
	Scalar::Native m_Offset;
	std::vector<std::function<void()>> m_Tests;
	int m_Index;
};

TestNodeConnection::TestNodeConnection(const std::string& seed)
	: m_Timer(io::Timer::create(io::Reactor::get_Current().shared_from_this()))
	, m_Failed(false)
{
	NoLeak<uintBig> walletSeed;
	Hash::Value hv;

	Hash::Processor() << seed.c_str() >> hv;
	walletSeed.V = hv;

	m_Kdf.m_Secret = walletSeed;
}

int TestNodeConnection::CheckOnFailed()
{
	return m_Failed;
}

void TestNodeConnection::OnConnected()
{
	LOG_INFO() << "connection is succeded";

	m_Timer->start(5 * 1000, false, []() {io::Reactor::get_Current().stop(); });

	GenerateTests();
	m_Index = 0;
	RunTest();
}

void TestNodeConnection::OnClosed(int errorCode)
{
	LOG_ERROR() << "problem with connecting to node: code = " << io::error_str(static_cast<io::ErrorCode>(errorCode));
	m_Failed = true;
	io::Reactor::get_Current().stop();
}

bool TestNodeConnection::OnMsg2(proto::Boolean&& msg)
{
	if (msg.m_Value)
	{
		LOG_INFO() << "Failed";
		m_Failed = true;
	}
	else
	{
		LOG_INFO() << "Ok";
	}

	if (m_Index >= m_Tests.size())
		io::Reactor::get_Current().stop();
	else
		RunTest();

	return true;
}

void TestNodeConnection::RunTest()
{
	if (m_Index < m_Tests.size())
		m_Tests[m_Index++]();
}

void TestNodeConnection::GenerateInputInTx(Height h, Amount v)
{
	ECC::Scalar::Native key;
	DeriveKey(key, m_Kdf, h, KeyType::Coinbase);

	Input::Ptr pInp(new Input);
	pInp->m_Commitment = ECC::Commitment(key, v);
	m_MsgTx.m_Transaction->m_vInputs.push_back(std::move(pInp));
	m_Offset += key;
}

void TestNodeConnection::GenerateOutputInTx(Height h, Amount v)
{
	Output::Ptr pOut(new Output);
	ECC::Scalar::Native key;

	DeriveKey(key, m_Kdf, h, KeyType::Regular);
	pOut->m_Incubation = 2;
	pOut->Create(key, v, true);
	m_MsgTx.m_Transaction->m_vOutputs.push_back(std::move(pOut));

	key = -key;
	m_Offset += key;
}

void TestNodeConnection::GenerateKernel(Height h)
{
	TxKernel::Ptr pKrn(new TxKernel);
	ECC::Scalar::Native key;

	DeriveKey(key, m_Kdf, h, KeyType::Kernel);
	pKrn->m_Excess = ECC::Point::Native(ECC::Context::get().G * key);

	ECC::Hash::Value hv;
	pKrn->get_HashForSigning(hv);
	pKrn->m_Signature.Sign(hv, key);
	m_MsgTx.m_Transaction->m_vKernelsOutput.push_back(std::move(pKrn));

	key = -key;
	m_Offset += key;
}

void TestNodeConnection::GenerateTests()
{
	m_Tests.push_back([this]() 
	{
		LOG_INFO() << "Run test with empty kernel";

		m_MsgTx.m_Transaction = std::make_shared<Transaction>();
		m_Offset = Zero;

		// Inputs
		GenerateInputInTx(1, 1);

		// Outputs
		GenerateOutputInTx(1, 1);

		// Kernels
		TxKernel::Ptr pKrn(new TxKernel);

		m_MsgTx.m_Transaction->m_vKernelsOutput.push_back(std::move(pKrn));

		m_MsgTx.m_Transaction->m_Offset = m_Offset;

		Send(m_MsgTx);
	});

	m_Tests.push_back([this]()
	{
		LOG_INFO() << "Run test with normal and empty kernels";

		m_MsgTx.m_Transaction = std::make_shared<Transaction>();
		m_Offset = Zero;

		// Inputs
		GenerateInputInTx(1, 1);

		// Outputs
		GenerateOutputInTx(1, 1);

		// Kernels

		// valid
		GenerateKernel(1);

		// empty
		TxKernel::Ptr pKrn(new TxKernel);

		m_MsgTx.m_Transaction->m_vKernelsOutput.push_back(std::move(pKrn));

		m_MsgTx.m_Transaction->m_Offset = m_Offset;

		Send(m_MsgTx);
	});

	m_Tests.push_back([this]()
	{
		LOG_INFO() << "Run test with 2 empty kernels";

		m_MsgTx.m_Transaction = std::make_shared<Transaction>();
		m_Offset = Zero;

		// Inputs
		GenerateInputInTx(1, 1);

		// Outputs
		GenerateOutputInTx(1, 1);

		// Kernels
		TxKernel::Ptr pKrn(new TxKernel);

		m_MsgTx.m_Transaction->m_vKernelsOutput.push_back(std::move(pKrn));

		pKrn = std::make_unique<TxKernel>();
		m_MsgTx.m_Transaction->m_vKernelsOutput.push_back(std::move(pKrn));

		m_MsgTx.m_Transaction->m_Offset = m_Offset;

		Send(m_MsgTx);
	});
}

int main(int argc, char* argv[])
{
	int logLevel = LOG_LEVEL_DEBUG;
#if LOG_VERBOSE_ENABLED
	logLevel = LOG_LEVEL_VERBOSE;
#endif
	auto logger = Logger::create(logLevel, logLevel);


	po::options_description options("allowed options");

	options.add_options()
		("address", po::value<std::string>()->default_value("127.0.0.1"), "ip address")
		("port", po::value<uint16_t>()->default_value(10000), "port")
		("wallet_seed", po::value<std::string>()->default_value("321"), "wallet seed");

	po::variables_map vm;

	po::store(po::command_line_parser(argc, argv).options(options).run(), vm);

	io::Reactor::Ptr reactor(io::Reactor::create());
	io::Reactor::Scope scope(*reactor);

	TestNodeConnection connection(vm["wallet_seed"].as<std::string>());

	io::Address addr;
	addr.resolve(vm["address"].as<std::string>().c_str());
	addr.port(vm["port"].as<uint16_t>());

	connection.Connect(addr);

	reactor->run();

	return connection.CheckOnFailed();
}