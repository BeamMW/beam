#include "beam/node.h"
#include "utility/logger.h"

#include "tools/base_node_connection.h"
#include "tools/tx_generator.h"

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

		TxGenerator gen(m_Kdf);

		// Inputs
		gen.GenerateInputInTx(1, 1);

		// Outputs
		gen.GenerateOutputInTx(1, 1);

		// Kernels
		gen.GenerateKernel();
		
		gen.Sort();

		Send(gen.GetTransaction());
	}, false));

	m_Tests.push_back(std::make_pair([this]()
	{
		LOG_INFO() << "Run test with normal and empty kernels";

		TxGenerator gen(m_Kdf);

		// Inputs
		gen.GenerateInputInTx(1, 1);

		// Outputs
		gen.GenerateOutputInTx(1, 1);

		// Kernels
		// valid
		gen.GenerateKernel(1);

		// empty
		gen.GenerateKernel();
		
		gen.Sort();

		Send(gen.GetTransaction());
	}, false));

	m_Tests.push_back(std::make_pair([this]()
	{
		LOG_INFO() << "Run test with 2 empty kernels";

		TxGenerator gen(m_Kdf);

		// Inputs
		gen.GenerateInputInTx(1, 1);

		// Outputs
		gen.GenerateOutputInTx(1, 1);

		// Kernels
		gen.GenerateKernel();
		gen.GenerateKernel();
		
		gen.Sort();

		Send(gen.GetTransaction());
	}, false));

	m_Tests.push_back(std::make_pair([this]()
	{
		LOG_INFO() << "Run test without kernels";

		TxGenerator gen(m_Kdf);

		// Inputs
		gen.GenerateInputInTx(1, 1);

		// Outputs
		gen.GenerateOutputInTx(1, 1);

		gen.Sort();

		Send(gen.GetTransaction());
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