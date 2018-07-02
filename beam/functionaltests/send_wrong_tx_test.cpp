#include "beam/node.h"
#include "utility/logger.h"

#include "tools/base_node_connection.h"

using namespace beam;
using namespace ECC;

class TestNodeConnection : public BaseTestNodeConnection
{
public:
	TestNodeConnection(int argc, char* argv[]);
private:
	void GenerateTests() override;

	void GenerateTx();
};

TestNodeConnection::TestNodeConnection(int argc, char* argv[])
	: BaseTestNodeConnection(argc, argv)
{
}

void TestNodeConnection::GenerateTests()
{
	//m_Tests.push_back(std::make_pair([this]()
	//{
	//	LOG_INFO() << "Run test without inputs";

	//	m_MsgTx.m_Transaction = std::make_shared<Transaction>();
	//	m_Offset = Zero;

	//	// Inputs are empty
	//	
	//	// Outputs
	//	GenerateOutputInTx(1, 1);

	//	// Kernels
	//	GenerateKernel(1);
	//	m_MsgTx.m_Transaction->m_Offset = m_Offset;
	//	m_MsgTx.m_Transaction->Sort();

	//	Send(m_MsgTx);
	//}, false));

	//m_Tests.push_back(std::make_pair([this]()
	//{
	//	LOG_INFO() << "Run test without outputs";

	//	m_MsgTx.m_Transaction = std::make_shared<Transaction>();
	//	m_Offset = Zero;

	//	// Inputs
	//	GenerateInputInTx(1, 1);

	//	// Outputs are empty

	//	// Kernels
	//	GenerateKernel(1);
	//	m_MsgTx.m_Transaction->m_Offset = m_Offset;
	//	m_MsgTx.m_Transaction->Sort();

	//	Send(m_MsgTx);
	//}, false));

	m_Tests.push_back(std::make_pair([this]()
	{
		LOG_INFO() << "Run test with normal tx";

		m_MsgTx.m_Transaction = std::make_shared<Transaction>();
		m_Offset = Zero;

		// Inputs 
		GenerateInputInTx(1, 1);

		// Outputs
		GenerateOutputInTx(1, 1);

		// Kernels
		GenerateKernel(1);
		m_MsgTx.m_Transaction->m_Offset = m_Offset;
		m_MsgTx.m_Transaction->Sort();

		Send(m_MsgTx);
	}, true));

	//m_Tests.push_back(std::make_pair([this]()
	//{
	//	LOG_INFO() << "Run test with 2 inputs, 2 ouputs and 1 kernel";

	//	m_MsgTx.m_Transaction = std::make_shared<Transaction>();
	//	m_Offset = Zero;

	//	// Inputs 
	//	GenerateInputInTx(1, 1);
	//	GenerateInputInTx(1, 1);

	//	// Outputs
	//	GenerateOutputInTx(1, 1);
	//	GenerateOutputInTx(1, 1);

	//	// Kernels
	//	GenerateKernel(1);
	//	m_MsgTx.m_Transaction->m_Offset = m_Offset;
	//	m_MsgTx.m_Transaction->Sort();

	//	Send(m_MsgTx);
	//}, true));

	//m_Tests.push_back(std::make_pair([this]()
	//{
	//	LOG_INFO() << "Run test with 2 inputs, 1 ouputs and 1 kernel";

	//	m_MsgTx.m_Transaction = std::make_shared<Transaction>();
	//	m_Offset = Zero;

	//	// Inputs 
	//	GenerateInputInTx(1, 1);
	//	GenerateInputInTx(1, 1);

	//	// Outputs
	//	GenerateOutputInTx(1, 2);		

	//	// Kernels
	//	GenerateKernel(1);
	//	m_MsgTx.m_Transaction->m_Offset = m_Offset;
	//	m_MsgTx.m_Transaction->Sort();

	//	Send(m_MsgTx);
	//}, true));

	//m_Tests.push_back(std::make_pair([this]()
	//{
	//	LOG_INFO() << "Run test with 1 inputs, 2 ouputs and 1 kernel";

	//	m_MsgTx.m_Transaction = std::make_shared<Transaction>();
	//	m_Offset = Zero;

	//	// Inputs 
	//	GenerateInputInTx(1, 2);

	//	// Outputs
	//	GenerateOutputInTx(1, 1);
	//	GenerateOutputInTx(1, 1);

	//	// Kernels
	//	GenerateKernel(1);
	//	m_MsgTx.m_Transaction->m_Offset = m_Offset;
	//	m_MsgTx.m_Transaction->Sort();

	//	Send(m_MsgTx);
	//}, true));

	m_Tests.push_back(std::make_pair([this]()
	{
		LOG_INFO() << "Run test with input=1chattle, output=2chattle, fee=0";

		m_MsgTx.m_Transaction = std::make_shared<Transaction>();
		m_Offset = Zero;

		// Inputs 
		GenerateInputInTx(1, 1);

		// Outputs
		GenerateOutputInTx(1, 2);

		// Kernels
		GenerateKernel(1);
		m_MsgTx.m_Transaction->m_Offset = m_Offset;
		m_MsgTx.m_Transaction->Sort();

		Transaction::Context ctx;

		LOG_INFO() << "tx.IsValid == " << m_MsgTx.m_Transaction->IsValid(ctx);

		Send(m_MsgTx);
	}, false));
}

int main(int argc, char* argv[])
{
	int logLevel = LOG_LEVEL_DEBUG;
#if LOG_VERBOSE_ENABLED
	logLevel = LOG_LEVEL_VERBOSE;
#endif
	auto logger = Logger::create(logLevel, logLevel);

	TestNodeConnection connection(argc, argv);

	connection.DisabledTimer();
	connection.Run();

	return connection.CheckOnFailed();
}