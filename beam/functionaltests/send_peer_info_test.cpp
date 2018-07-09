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
	virtual void OnClosed(int errorCode) override;

	virtual void OnMsg(proto::NewTip&&) override;

	virtual void GenerateTests() override;
};

TestNodeConnection::TestNodeConnection(int argc, char* argv[])
	: BaseTestNodeConnection(argc, argv)
{
	m_Timeout = 60 * 1000;
}

void TestNodeConnection::OnClosed(int errorCode)
{
	LOG_INFO() << "Ok: connection is reset";
	io::Reactor::get_Current().stop();
}

void TestNodeConnection::OnMsg(proto::NewTip&& msg) 
{
	LOG_INFO() << "NewTip";
}

void TestNodeConnection::GenerateTests()
{
	m_Tests.push_back([this]()
	{
		LOG_INFO() << "Send PeerInfo message";

		Hash::Processor hp;
		Hash::Value hv;

		hp << "test" >> hv;

		proto::PeerInfo msg;

		msg.m_LastAddr.resolve("8.8.8.8");
		msg.m_ID = hv;
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