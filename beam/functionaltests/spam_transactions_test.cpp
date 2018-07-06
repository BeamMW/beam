#include "beam/node.h"
#include "utility/logger.h"
#include "tools/base_node_connection.h"
#include "tools/tx_generator.h"

#include <vector>
#include <thread>
#include <future>

using namespace beam;
using namespace ECC;

class TestNodeConnection : public BaseTestNodeConnection
{
public:
	TestNodeConnection(int argc, char* argv[], int h);
	void BeforeConnection(Height h);
private:

	void GenerateTests() override;

private:
	int m_H;
	proto::NewTransaction m_MsgTx;
};

TestNodeConnection::TestNodeConnection(int argc, char* argv[], int h)
	: BaseTestNodeConnection(argc, argv)
	, m_H(h)
{
}

void TestNodeConnection::BeforeConnection(Height h)
{
	TxGenerator gen(m_Kdf);

	Amount amount = 5000;

	// Inputs
	gen.GenerateInputInTx(h, amount);

	// Outputs
	for (Amount i = 0; i < amount; ++i)
	{
		gen.GenerateOutputInTx(h, 1);
	}

	// Kernels
	gen.GenerateKernel(h);

	gen.Sort();

	m_MsgTx = gen.GetTransaction();
}

void TestNodeConnection::GenerateTests()
{
	for (int i = 0; i < 3; ++i)
	{
		m_Tests.push_back(std::make_pair([this, i]()
		{
			LOG_INFO() << "Send big transaction";
			BeforeConnection(100 * (m_H + 2) + i);

			Send(m_MsgTx);
		}, true));
	}
}

void SendData(int argc, char* argv[], int h)
{
	TestNodeConnection connection(argc, argv, h);

	connection.DisabledTimer();
	connection.Run();
}

int main(int argc, char* argv[])
{
	int logLevel = LOG_LEVEL_DEBUG;
#if LOG_VERBOSE_ENABLED
	logLevel = LOG_LEVEL_VERBOSE;
#endif
	auto logger = Logger::create(logLevel, logLevel);
	std::vector<std::future<void>> futures;

	for (int i = 0; i < 10; i++)
	{
		futures.push_back(std::async(SendData, argc, argv, i));
	}

	for (auto &f : futures)
	{
		f.get();
	}
}