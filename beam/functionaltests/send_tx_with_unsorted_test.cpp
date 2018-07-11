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

	TxGenerator GenerateTx();
};

TestNodeConnection::TestNodeConnection(int argc, char* argv[])
	: NewTxConnection(argc, argv)
{

}

void TestNodeConnection::GenerateTests()
{
	m_Tests.push_back([this]()
	{
		LOG_INFO() << "Run test with unsorded kernels";

		TxGenerator gen = GenerateTx();
		
		gen.SortInputs();
		gen.SortOutputs();

		Send(gen.GetTransaction());
	});
	m_Results.push_back(false);

	m_Tests.push_back([this]()
	{
		LOG_INFO() << "Run test with unsorted inputs";

		TxGenerator gen = GenerateTx();

		gen.SortOutputs();
		gen.SortKernels();

		Send(gen.GetTransaction());
	});
	m_Results.push_back(false);

	m_Tests.push_back([this]()
	{
		LOG_INFO() << "Run test with unsorted outputs";

		TxGenerator gen = GenerateTx();

		gen.SortInputs();
		gen.SortKernels();

		Send(gen.GetTransaction());
	});
	m_Results.push_back(false);

	m_Tests.push_back([this]()
	{
		LOG_INFO() << "Run test with sorted inputs,outputs and kernels";

		TxGenerator gen = GenerateTx();
		gen.Sort();

		Send(gen.GetTransaction());
	});
	m_Results.push_back(true);
}

TxGenerator TestNodeConnection::GenerateTx()
{
	TxGenerator gen(m_Kdf);

	// Inputs
	gen.GenerateInputInTx(4, 5);
	gen.GenerateInputInTx(2, 2);
	gen.GenerateInputInTx(3, 3);

	// Outputs
	gen.GenerateOutputInTx(5, 1);
	gen.GenerateOutputInTx(4, 4);
	gen.GenerateOutputInTx(2, 2);
	gen.GenerateOutputInTx(3, 3);

	// Kernels
	gen.GenerateKernel(4);
	gen.GenerateKernel(2);
	gen.GenerateKernel(3);
		
	return gen;
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