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
		LOG_INFO() << "Run test with unsorded kernels";

		GenerateTx();
		
		std::sort(m_MsgTx.m_Transaction->m_vInputs.begin(), m_MsgTx.m_Transaction->m_vInputs.end());
		std::sort(m_MsgTx.m_Transaction->m_vOutputs.begin(), m_MsgTx.m_Transaction->m_vOutputs.end());

		Send(m_MsgTx);
	}, false));

	m_Tests.push_back(std::make_pair([this]()
	{
		LOG_INFO() << "Run test with unsorted inputs";

		GenerateTx();

		std::sort(m_MsgTx.m_Transaction->m_vKernelsOutput.begin(), m_MsgTx.m_Transaction->m_vKernelsOutput.end());
		std::sort(m_MsgTx.m_Transaction->m_vOutputs.begin(), m_MsgTx.m_Transaction->m_vOutputs.end());

		Send(m_MsgTx);
	}, false));

	m_Tests.push_back(std::make_pair([this]()
	{
		LOG_INFO() << "Run test with unsorted outputs";

		GenerateTx();

		std::sort(m_MsgTx.m_Transaction->m_vInputs.begin(), m_MsgTx.m_Transaction->m_vInputs.end());
		std::sort(m_MsgTx.m_Transaction->m_vKernelsOutput.begin(), m_MsgTx.m_Transaction->m_vKernelsOutput.end());

		Send(m_MsgTx);
	}, false));

	m_Tests.push_back(std::make_pair([this]()
	{
		LOG_INFO() << "Run test with sorted inputs,outputs and kernels";

		GenerateTx();
		m_MsgTx.m_Transaction->Sort();

		Send(m_MsgTx);
	}, true));
}

void TestNodeConnection::GenerateTx()
{
	m_MsgTx.m_Transaction = std::make_shared<Transaction>();
	m_Offset = Zero;

	// Inputs
	GenerateInputInTx(4, 5);
	GenerateInputInTx(2, 2);
	GenerateInputInTx(3, 3);

	// Outputs
	GenerateOutputInTx(5, 1);
	GenerateOutputInTx(4, 4);
	GenerateOutputInTx(2, 2);
	GenerateOutputInTx(3, 3);

	// Kernels
	GenerateKernel(4);
	GenerateKernel(2);
	GenerateKernel(3);

	m_MsgTx.m_Transaction->m_Offset = m_Offset;
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