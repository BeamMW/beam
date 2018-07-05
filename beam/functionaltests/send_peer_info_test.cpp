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
	virtual void OnClosed(int errorCode) override;
};

TestNodeConnection::TestNodeConnection(int argc, char* argv[])
	: BaseTestNodeConnection(argc, argv)
{
}

void TestNodeConnection::OnConnected()
{
	LOG_INFO() << "Send PeerInfo message";

	Hash::Processor hp;
	Hash::Value hv;

	hp << "test" >> hv;

	proto::PeerInfo msg;

	msg.m_LastAddr.resolve("127.0.0.2");
	msg.m_ID = hv;
	Send(msg);

	m_Timer->start(3* 1000, false, [this]()
	{
		io::Reactor::get_Current().stop();
	});
}

void TestNodeConnection::OnClosed(int errorCode)
{
	LOG_INFO() << "Ok: connection is reset";
	io::Reactor::get_Current().stop();
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