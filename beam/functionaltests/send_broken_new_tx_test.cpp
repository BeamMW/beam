#include "beam/node.h"
#include "utility/logger.h"
#include "tools/base_node_connection.h"

#include <vector>
#include <thread>
#include <future>

using namespace beam;

class TestNodeConnection : public BaseTestNode
{
public:
	TestNodeConnection(int argc, char* argv[]);
private:
	void OnDisconnect(const DisconnectReason&) override;
	void GenerateTests() override;
};

TestNodeConnection::TestNodeConnection(int argc, char* argv[])
	: BaseTestNode(argc, argv)
{
}

void TestNodeConnection::OnDisconnect(const DisconnectReason&)
{
	LOG_INFO() << "Ok: connection is reset";
	io::Reactor::get_Current().stop();
}

void TestNodeConnection::GenerateTests()
{
	m_Tests.push_back([this]()
	{
		LOG_INFO() << "Send message NewTransaction without transaction";
		proto::NewTransaction msg;

		Send(msg);
	});
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