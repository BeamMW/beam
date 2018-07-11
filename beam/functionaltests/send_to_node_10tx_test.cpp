#include "beam/node.h"
#include "utility/logger.h"
#include "tools/base_node_connection.h"
#include "tools/tx_generator.h"
#include "tools/new_tx_tests.h"

using namespace beam;
using namespace ECC;	

class TestNodeConnection : public NewTxConnection
{
public:
	TestNodeConnection(int argc, char* argv[]);
private:
	void GenerateTests() override;
};

TestNodeConnection::TestNodeConnection(int argc, char* argv[])
	: NewTxConnection(argc, argv)
{
}


void TestNodeConnection::GenerateTests()
{
	for (int i = 1; i <= 10; i++)
	{
		m_Tests.push_back([this, i]()
		{
			TxGenerator gen(m_Kdf);
			
			// Inputs
			gen.GenerateInputInTx(i, 1);
			// Outputs
			gen.GenerateOutputInTx(i, 1);
			// Kernels
			gen.GenerateKernel(4);

			Send(gen.GetTransaction());
		});
		m_Results.push_back(true);
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