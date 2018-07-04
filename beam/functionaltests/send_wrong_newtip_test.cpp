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
	
private:
	Height m_Height;
};

TestNodeConnection::TestNodeConnection(int argc, char* argv[])
	: BaseTestNodeConnection(argc, argv)
	, m_Height(0)
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

	if (m_Height == 0)
	{
		LOG_INFO() << "Send NewTip with height = " << msg.m_ID.m_Height + 5;
		proto::NewTip newMsg;

		newMsg.m_ID = msg.m_ID;
		newMsg.m_ID.m_Height += 5;
		m_Height = newMsg.m_ID.m_Height;

		Send(newMsg);

		m_Timer->start(3 * 60 * 1000, false, [this]()
		{
			m_Failed = true;
			io::Reactor::get_Current().stop();
		});
	}
	else
	{
		if (msg.m_ID.m_Height == m_Height)
		{
			LOG_INFO() << "Ok";
		}
		else
		{
			LOG_INFO() << "Failed";
		}
		io::Reactor::get_Current().stop();
	}

	return true;
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