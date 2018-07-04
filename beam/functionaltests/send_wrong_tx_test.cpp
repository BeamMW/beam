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
	m_Tests.push_back(std::make_pair([this]()
	{
		LOG_INFO() << "Run test without inputs";

		m_MsgTx.m_Transaction = std::make_shared<Transaction>();
		m_Offset = Zero;

		// Inputs are empty
		
		// Outputs
		GenerateOutputInTx(1, 1);

		// Kernels
		GenerateKernel(1);
		m_MsgTx.m_Transaction->m_Offset = m_Offset;
		m_MsgTx.m_Transaction->Sort();

		Transaction::Context ctx;

		LOG_INFO() << "tx.IsValid == " << m_MsgTx.m_Transaction->IsValid(ctx);

		Send(m_MsgTx);
	}, false));

	m_Tests.push_back(std::make_pair([this]()
	{
		LOG_INFO() << "Run test without outputs";

		m_MsgTx.m_Transaction = std::make_shared<Transaction>();
		m_Offset = Zero;

		// Inputs
		GenerateInputInTx(2, 1);

		// Outputs are empty

		// Kernels
		GenerateKernel(2);
		m_MsgTx.m_Transaction->m_Offset = m_Offset;
		m_MsgTx.m_Transaction->Sort();

		Transaction::Context ctx;

		LOG_INFO() << "tx.IsValid == " << m_MsgTx.m_Transaction->IsValid(ctx);

		Send(m_MsgTx);
	}, false));

	m_Tests.push_back(std::make_pair([this]()
	{
		LOG_INFO() << "Run test with normal tx";

		m_MsgTx.m_Transaction = std::make_shared<Transaction>();
		m_Offset = Zero;

		// Inputs 
		GenerateInputInTx(3, 1);

		// Outputs
		GenerateOutputInTx(3, 1);

		// Kernels
		GenerateKernel(3);
		m_MsgTx.m_Transaction->m_Offset = m_Offset;
		m_MsgTx.m_Transaction->Sort();

		Transaction::Context ctx;

		LOG_INFO() << "tx.IsValid == " << m_MsgTx.m_Transaction->IsValid(ctx);

		Send(m_MsgTx);
	}, true));

	m_Tests.push_back(std::make_pair([this]()
	{
		LOG_INFO() << "Run test with 2 inputs, 2 ouputs and 1 kernel";

		m_MsgTx.m_Transaction = std::make_shared<Transaction>();
		m_Offset = Zero;

		// Inputs 
		GenerateInputInTx(4, 1);
		GenerateInputInTx(4, 1);

		// Outputs
		GenerateOutputInTx(5, 1);
		GenerateOutputInTx(5, 1);

		// Kernels
		GenerateKernel(5);
		m_MsgTx.m_Transaction->m_Offset = m_Offset;
		m_MsgTx.m_Transaction->Sort();

		Transaction::Context ctx;

		LOG_INFO() << "tx.IsValid == " << m_MsgTx.m_Transaction->IsValid(ctx);

		Send(m_MsgTx);
	}, true));

	m_Tests.push_back(std::make_pair([this]()
	{
		LOG_INFO() << "Run test with 2 inputs, 1 ouputs and 1 kernel";

		m_MsgTx.m_Transaction = std::make_shared<Transaction>();
		m_Offset = Zero;

		// Inputs 
		GenerateInputInTx(6, 1);
		GenerateInputInTx(6, 1);

		// Outputs
		GenerateOutputInTx(6, 2);		

		// Kernels
		GenerateKernel(6);
		m_MsgTx.m_Transaction->m_Offset = m_Offset;
		m_MsgTx.m_Transaction->Sort();

		Transaction::Context ctx;

		LOG_INFO() << "tx.IsValid == " << m_MsgTx.m_Transaction->IsValid(ctx);

		Send(m_MsgTx);
	}, true));

	m_Tests.push_back(std::make_pair([this]()
	{
		LOG_INFO() << "Run test with 1 inputs, 2 ouputs and 1 kernel";

		m_MsgTx.m_Transaction = std::make_shared<Transaction>();
		m_Offset = Zero;

		// Inputs 
		GenerateInputInTx(7, 2);

		// Outputs
		GenerateOutputInTx(7, 1);
		GenerateOutputInTx(7, 1);

		// Kernels
		GenerateKernel(7);
		m_MsgTx.m_Transaction->m_Offset = m_Offset;
		m_MsgTx.m_Transaction->Sort();

		Transaction::Context ctx;

		LOG_INFO() << "tx.IsValid == " << m_MsgTx.m_Transaction->IsValid(ctx);

		Send(m_MsgTx);
	}, true));

	m_Tests.push_back(std::make_pair([this]()
	{
		LOG_INFO() << "Run test with input = 1 chattle, output= 2 chattles, fee=0";

		m_MsgTx.m_Transaction = std::make_shared<Transaction>();
		m_Offset = Zero;

		// Inputs 
		GenerateInputInTx(8, 1);

		// Outputs
		GenerateOutputInTx(8, 2);

		// Kernels
		GenerateKernel(8);
		m_MsgTx.m_Transaction->m_Offset = m_Offset;
		m_MsgTx.m_Transaction->Sort();

		Transaction::Context ctx;

		LOG_INFO() << "tx.IsValid == " << m_MsgTx.m_Transaction->IsValid(ctx);

		Send(m_MsgTx);
	}, false));

	m_Tests.push_back(std::make_pair([this]()
	{
		LOG_INFO() << "Run test with input = 2 chattle, output= 1 chattles, fee=0";

		m_MsgTx.m_Transaction = std::make_shared<Transaction>();
		m_Offset = Zero;

		// Inputs 
		GenerateInputInTx(9, 2);

		// Outputs
		GenerateOutputInTx(9, 1);

		// Kernels
		GenerateKernel(9);
		m_MsgTx.m_Transaction->m_Offset = m_Offset;
		m_MsgTx.m_Transaction->Sort();

		Transaction::Context ctx;

		LOG_INFO() << "tx.IsValid == " << m_MsgTx.m_Transaction->IsValid(ctx);

		Send(m_MsgTx);
	}, false));

	m_Tests.push_back(std::make_pair([this]()
	{
		LOG_INFO() << "Run test with input = 2 chattle, output= 3 chattles, fee=0";

		m_MsgTx.m_Transaction = std::make_shared<Transaction>();
		m_Offset = Zero;

		// Inputs 
		GenerateInputInTx(10, 2);

		// Outputs
		GenerateOutputInTx(10, 3);

		// Kernels
		GenerateKernel(10);
		m_MsgTx.m_Transaction->m_Offset = m_Offset;
		m_MsgTx.m_Transaction->Sort();

		Transaction::Context ctx;

		LOG_INFO() << "tx.IsValid == " << m_MsgTx.m_Transaction->IsValid(ctx);

		Send(m_MsgTx);
	}, false));

	m_Tests.push_back(std::make_pair([this]()
	{
		LOG_INFO() << "Run test with input = 4 chattle, output= 2 chattle, fee = 2 chattles";

		m_MsgTx.m_Transaction = std::make_shared<Transaction>();
		m_Offset = Zero;

		// Inputs 
		GenerateInputInTx(11, 4);

		// Outputs
		GenerateOutputInTx(11, 2);

		// Kernels
		GenerateKernel(11, 2);
		m_MsgTx.m_Transaction->m_Offset = m_Offset;
		m_MsgTx.m_Transaction->Sort();

		Transaction::Context ctx;

		LOG_INFO() << "tx.IsValid == " << m_MsgTx.m_Transaction->IsValid(ctx);

		Send(m_MsgTx);
	}, true));

	m_Tests.push_back(std::make_pair([this]()
	{
		LOG_INFO() << "Run test with input = 4 chattle, output= 2 chattle, fee= 1 chattle, fee = 1 chattle";

		m_MsgTx.m_Transaction = std::make_shared<Transaction>();
		m_Offset = Zero;

		// Inputs 
		GenerateInputInTx(12, 4);

		// Outputs
		GenerateOutputInTx(12, 2);

		// Kernels
		GenerateKernel(12, 1);
		GenerateKernel(12, 1);
		m_MsgTx.m_Transaction->m_Offset = m_Offset;
		m_MsgTx.m_Transaction->Sort();

		Transaction::Context ctx;

		LOG_INFO() << "tx.IsValid == " << m_MsgTx.m_Transaction->IsValid(ctx);

		Send(m_MsgTx);
	}, true));

	m_Tests.push_back(std::make_pair([this]()
	{
		LOG_INFO() << "Run test with input = 4 chattle, output= 2 chattle, fee= 1 chattle, fee = 1 chattle, fee = 1 chattle";

		m_MsgTx.m_Transaction = std::make_shared<Transaction>();
		m_Offset = Zero;

		// Inputs 
		GenerateInputInTx(13, 4);

		// Outputs
		GenerateOutputInTx(13, 2);

		// Kernels
		GenerateKernel(13, 1);
		GenerateKernel(13, 1);
		GenerateKernel(13, 1);
		m_MsgTx.m_Transaction->m_Offset = m_Offset;
		m_MsgTx.m_Transaction->Sort();

		Transaction::Context ctx;

		LOG_INFO() << "tx.IsValid == " << m_MsgTx.m_Transaction->IsValid(ctx);

		Send(m_MsgTx);
	}, false));

	m_Tests.push_back(std::make_pair([this]()
	{
		LOG_INFO() << "Run test with input = 4 chattle, output= 2 chattle, fee= 3 chattles";

		m_MsgTx.m_Transaction = std::make_shared<Transaction>();
		m_Offset = Zero;

		// Inputs 
		GenerateInputInTx(14, 4);

		// Outputs
		GenerateOutputInTx(14, 2);

		// Kernels
		GenerateKernel(14, 3);
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