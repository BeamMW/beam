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
	virtual void OnConnected() override;
	virtual bool OnMsg2(proto::Boolean&& msg) override;
	virtual bool OnMsg2(proto::NewTip&& msg) override;	

	void GenerateTests() override;
};

TestNodeConnection::TestNodeConnection(int argc, char* argv[])
	: BaseTestNodeConnection(argc, argv)
{
}

void TestNodeConnection::OnConnected()
{
}

bool TestNodeConnection::OnMsg2(proto::Boolean&& msg)
{
	LOG_INFO() << "Node returned: " << msg.m_Value;
	return true;
}

bool TestNodeConnection::OnMsg2(proto::NewTip&& msg)
{
	LOG_INFO() << "NewTip: " << msg.m_ID;
	msg.m_ID.m_Height += 3;

	proto::NewTip newMsg;

	newMsg.m_ID = msg.m_ID;
	
	Send(newMsg);
	return true;
}

void TestNodeConnection::GenerateTests()
{	
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