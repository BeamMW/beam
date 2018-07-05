#include "base_node_connection.h"
#include "utility/logger.h"

#include <boost/program_options.hpp>

namespace po = boost::program_options;
using namespace beam;
using namespace ECC;


Initializer g_Initializer;

BaseTestNodeConnection::BaseTestNodeConnection(int argc, char* argv[])
	: m_Reactor(io::Reactor::create())
	, m_Scope(*m_Reactor)
	, m_Timer(io::Timer::create(io::Reactor::get_Current().shared_from_this()))
	, m_Failed(false)
	
{
	ParseCommandLine(argc, argv);
	InitKdf();	
}

void BaseTestNodeConnection::Run()
{
	io::Address addr;
	addr.resolve(m_VM["address"].as<std::string>().c_str());
	addr.port(m_VM["port"].as<uint16_t>());

	Connect(addr);

	m_Reactor->run();
}

void BaseTestNodeConnection::DisabledTimer()
{
	m_WillStartTimer = false;

}

int BaseTestNodeConnection::CheckOnFailed()
{
	return m_Failed;
}

void BaseTestNodeConnection::ParseCommandLine(int argc, char* argv[])
{
	po::options_description options("allowed options");

	options.add_options()
		("address", po::value<std::string>()->default_value("127.0.0.1"), "ip address")
		("port", po::value<uint16_t>()->default_value(10000), "port")
		("wallet_seed", po::value<std::string>()->default_value("321"), "wallet seed");

	po::store(po::command_line_parser(argc, argv).options(options).run(), m_VM);
}

void BaseTestNodeConnection::InitKdf()
{
	NoLeak<uintBig> walletSeed;
	Hash::Value hv;

	Hash::Processor() << m_VM["wallet_seed"].as<std::string>().c_str() >> hv;
	walletSeed.V = hv;

	m_Kdf.m_Secret = walletSeed;
}

void BaseTestNodeConnection::OnConnected()
{
	LOG_INFO() << "connection is succeded";

	if (m_WillStartTimer)
		m_Timer->start(5 * 1000, false, []() {io::Reactor::get_Current().stop(); });

	GenerateTests();
	m_Index = 0;
	RunTest();
}

void BaseTestNodeConnection::OnClosed(int errorCode)
{
	LOG_ERROR() << "problem with connecting to node: code = " << io::error_str(static_cast<io::ErrorCode>(errorCode));
	m_Failed = true;
	io::Reactor::get_Current().stop();
}

bool BaseTestNodeConnection::OnMsg2(proto::Boolean&& msg)
{
	if (msg.m_Value != m_Tests[m_Index].second)
	{
		LOG_INFO() << "Failed: node returned " << msg.m_Value;
		m_Failed = true;
	}
	else
	{
		LOG_INFO() << "Ok: node returned " << msg.m_Value;
	}

	++m_Index;

	if (m_Index >= m_Tests.size())
		io::Reactor::get_Current().stop();
	else
		RunTest();

	return true;
}

void BaseTestNodeConnection::RunTest()
{
	if (m_Index < m_Tests.size())
		m_Tests[m_Index].first();
}

void BaseTestNodeConnection::GenerateInputInTx(Height h, Amount v)
{
	ECC::Scalar::Native key;
	DeriveKey(key, m_Kdf, h, KeyType::Coinbase);

	Input::Ptr pInp(new Input);
	pInp->m_Commitment = ECC::Commitment(key, v);
	m_MsgTx.m_Transaction->m_vInputs.push_back(std::move(pInp));
	m_Offset += key;
}

void BaseTestNodeConnection::GenerateOutputInTx(Height h, Amount v)
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

void BaseTestNodeConnection::GenerateKernel(Height h, Amount fee)
{
	TxKernel::Ptr pKrn(new TxKernel);
	ECC::Scalar::Native key;

	if (fee > 0)
		pKrn->m_Fee = fee;

	DeriveKey(key, m_Kdf, h, KeyType::Kernel);
	pKrn->m_Excess = ECC::Point::Native(ECC::Context::get().G * key);

	ECC::Hash::Value hv;
	pKrn->get_HashForSigning(hv);
	pKrn->m_Signature.Sign(hv, key);
	m_MsgTx.m_Transaction->m_vKernelsOutput.push_back(std::move(pKrn));

	key = -key;
	m_Offset += key;
}

void BaseTestNodeConnection::GenerateTests()
{	
}