#include "beam/node.h"
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

	void SendTransaction(Height h);	
private:
	const int TxAmount = 10;
	ECC::Kdf m_Kdf;
	io::Timer::Ptr m_Timer;
	int m_Amount;
	bool m_Failed;
};

TestNodeConnection::TestNodeConnection(const std::string& seed)
	: m_Timer(io::Timer::create(io::Reactor::get_Current().shared_from_this()))
	, m_Amount(0)
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
	return m_Failed || m_Amount != TxAmount ? 1 : 0;
}

void TestNodeConnection::OnConnected()
{
	LOG_INFO() << "connection is succeded";	

	m_Timer->start(10 * 1000, false, []() {io::Reactor::get_Current().stop(); });

	for (int i = 1; i <= TxAmount; i++)
	{
		SendTransaction(i);
	}
}

void TestNodeConnection::OnClosed(int errorCode)
{
	LOG_ERROR() << "problem with connecting to node: code = " << io::error_str(static_cast<io::ErrorCode>(errorCode));
	m_Failed = true;
	io::Reactor::get_Current().stop();
}

bool TestNodeConnection::OnMsg2(proto::Boolean&& msg)
{
	LOG_INFO() << "Boolean is received: value = " << msg.m_Value;

	if (msg.m_Value)
	{
		m_Amount++;
		LOG_INFO() << "Amount = " << m_Amount;
	}

	return true;
}

void TestNodeConnection::SendTransaction(Height h)
{
	proto::NewTransaction msgTx;

	msgTx.m_Transaction = std::make_shared<Transaction>();

	// Inputs
	ECC::Scalar::Native key1;
	DeriveKey(key1, m_Kdf, h, KeyType::Coinbase);
	ECC::Scalar::Native offset = key1;

	Input::Ptr pInp(new Input);
	pInp->m_Commitment = ECC::Commitment(key1, Rules::CoinbaseEmission);
	msgTx.m_Transaction->m_vInputs.push_back(std::move(pInp));

	// Outputs
	Output::Ptr pOut(new Output);
	ECC::Scalar::Native key2;

	DeriveKey(key2, m_Kdf, h, KeyType::Regular);
	pOut->m_Incubation = 2;
	pOut->Create(key2, Rules::CoinbaseEmission, true);
	msgTx.m_Transaction->m_vOutputs.push_back(std::move(pOut));

	key2 = -key2;
	offset += key2;

	// Kernels
	TxKernel::Ptr pKrn(new TxKernel);
	ECC::Scalar::Native key3;

	DeriveKey(key3, m_Kdf, h, KeyType::Kernel);
	pKrn->m_Excess = ECC::Point::Native(ECC::Context::get().G * key3);

	ECC::Hash::Value hv;
	pKrn->get_HashForSigning(hv);
	pKrn->m_Signature.Sign(hv, key3);
	msgTx.m_Transaction->m_vKernelsOutput.push_back(std::move(pKrn));

	key3 = -key3;
	offset += key3;

	msgTx.m_Transaction->m_Offset = offset;

	Transaction::Context ctx;
	if (msgTx.m_Transaction->IsValid(ctx))
	{
		Send(msgTx);
		return;
	}
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
		("walled_seed", po::value<std::string>()->default_value("321"), "wallet seed");

	po::variables_map vm;
	
	po::store(po::command_line_parser(argc, argv).options(options).run(), vm);
	
	io::Reactor::Ptr reactor(io::Reactor::create());
	io::Reactor::Scope scope(*reactor);

	TestNodeConnection connection(vm["walled_seed"].as<std::string>());

	io::Address addr;
	addr.resolve(vm["address"].as<std::string>().c_str());
	addr.port(vm["port"].as<uint16_t>());

	connection.Connect(addr);

	reactor->run();

	return connection.CheckOnFailed();
}