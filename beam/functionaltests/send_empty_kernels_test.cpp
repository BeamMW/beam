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
		m_MsgTx.m_Transaction->Sort();

		Send(m_MsgTx);
	}, false));

	m_Tests.push_back(std::make_pair([this]()
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
		m_MsgTx.m_Transaction->Sort();

		Send(m_MsgTx);
	}, false));

	m_Tests.push_back(std::make_pair([this]()
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
		m_MsgTx.m_Transaction->Sort();

		Send(m_MsgTx);
	}, false));

	m_Tests.push_back(std::make_pair([this]()
	{
		LOG_INFO() << "Run test without kernels";

		m_MsgTx.m_Transaction = std::make_shared<Transaction>();
		m_Offset = Zero;

		// Inputs
		GenerateInputInTx(1, 1);

		// Outputs
		GenerateOutputInTx(1, 1);

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

	connection.Run();

	return connection.CheckOnFailed();
}