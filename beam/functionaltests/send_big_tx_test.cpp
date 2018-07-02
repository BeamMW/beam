#include "beam/node.h"
#include "utility/logger.h"
#include "tools/base_node_connection.h"

#include <vector>
#include <thread>
#include <future>

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
	m_Tests.push_back(std::make_pair([this]()
	{
		LOG_INFO() << "Send big transaction";
		m_MsgTx.m_Transaction = std::make_shared<Transaction>();
		m_Offset = Zero;

		Amount amount = 20000;

		// Inputs
		GenerateInputInTx(1, amount);

		// Outputs
		for (Amount i = 0; i < amount; ++i)
		{
			GenerateOutputInTx(1, 1);
		}

		// Kernels
		GenerateKernel(1);

		m_MsgTx.m_Transaction->m_Offset = m_Offset;
		m_MsgTx.m_Transaction->Sort();

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