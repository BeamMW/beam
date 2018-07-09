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
};

TestNodeConnection::TestNodeConnection(int argc, char* argv[])
	: BaseTestNodeConnection(argc, argv)
{
}


void TestNodeConnection::GenerateTests()
{
	for (int i = 1; i <= 10; i++)
	{
		m_Tests.push_back(std::make_pair([this, i]()
		{
			m_MsgTx.m_Transaction = std::make_shared<Transaction>();
			m_Offset = Zero;

			// Inputs
			GenerateInputInTx(i, 1);
			// Outputs
			GenerateOutputInTx(i, 1);
			// Kernels
			GenerateKernel(4);

			m_MsgTx.m_Transaction->m_Offset = m_Offset;
			Send(m_MsgTx);
		}, true));
	}
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