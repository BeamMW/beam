#include "beam/node.h"
#include "utility/logger.h"
#include "tools/base_node_connection.h"
#include "tools/tx_generator.h"
#include "tools/new_tx_tests.h"

#include <vector>
#include <thread>
#include <future>

using namespace beam;
using namespace ECC;

void GenerateRandom(void* p, uint32_t n)
{
	for (uint32_t i = 0; i < n; i++)
		((uint8_t*)p)[i] = (uint8_t)rand();
}

void SetRandom(uintBig& x)
{
	GenerateRandom(x.m_pData, sizeof(x.m_pData));
}

void SetRandom(Scalar::Native& x)
{
	Scalar s;
	while (true)
	{
		SetRandom(s.m_Value);
		if (!x.Import(s))
			break;
	}
}

class TestNodeConnection : public BaseTestNode
{
public:
	TestNodeConnection(int argc, char* argv[], int h);	

private:
	void GenerateTests() override;

private:
	int m_Ind;	
};

TestNodeConnection::TestNodeConnection(int argc, char* argv[], int h)
	: BaseTestNode(argc, argv)
	, m_Ind(h)
{
	m_Timeout = 0;
}


void TestNodeConnection::GenerateTests()
{
	m_Tests.push_back([this]()
	{
		m_Timer->start(10, true, [this]
		{
			LOG_INFO() << "Send HaveTransaction: ind = " << m_Ind;
			Transaction::KeyType id;

			SetRandom(id);
			Send(proto::HaveTransaction{ id });
		});		
	});
}

void SendData(int argc, char* argv[], int h)
{
	TestNodeConnection connection(argc, argv, h);

	connection.Run();
}

int main(int argc, char* argv[])
{
	int logLevel = LOG_LEVEL_DEBUG;
#if LOG_VERBOSE_ENABLED
	logLevel = LOG_LEVEL_VERBOSE;
#endif
	auto logger = Logger::create(logLevel, logLevel);
	//SendData(argc, argv, 1);
	std::vector<std::future<void>> futures;

	for (int i = 0; i < 1000; i++)
	{
		futures.push_back(std::async(SendData, argc, argv, i));
	}

	for (auto &f : futures)
	{
		f.get();
	}
}