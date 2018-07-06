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
	virtual void OnConnected() override;
	virtual void OnMsg(proto::NewTip&&) override;
	virtual void OnMsg(proto::Mined&&) override;
	
private:
	bool m_IsInit;
	Block::SystemState::ID m_ID;
	bool m_IsSendWrongMsg;
};

TestNodeConnection::TestNodeConnection(int argc, char* argv[])
	: BaseTestNodeConnection(argc, argv)
	, m_IsInit(false)
	, m_IsSendWrongMsg(false)
{
}

void TestNodeConnection::OnConnected()
{
	m_Timer->start(10 * 1000, false, [this]()
	{
		m_Failed = true;
		io::Reactor::get_Current().stop();
	});
}

void TestNodeConnection::OnMsg(proto::NewTip&& msg)
{
	if (!m_IsInit)
	{
		LOG_INFO() << "NewTip: " << msg.m_ID;

		m_ID = msg.m_ID;
		m_IsInit = true;

		m_IsSendWrongMsg = true;

		LOG_INFO() << "Send wrong GetMined message";
		Send(proto::GetMined{ m_ID.m_Height + 5 });
	}
}

void TestNodeConnection::OnMsg(proto::Mined&& msg)
{
	LOG_INFO() << "Mined";
	if (m_IsSendWrongMsg)
	{
		if (msg.m_Entries.empty())
		{
			LOG_INFO() << "Ok: received empty list";
		}
		else
		{
			LOG_INFO() << "Failed: list is not empty";
			m_Failed = true;
			io::Reactor::get_Current().stop();
			return;
		}
				
		m_IsSendWrongMsg = false;

		LOG_INFO() << "Send GetMined message";
		Send(proto::GetMined{ m_ID.m_Height });

		return;
	}

	if (!msg.m_Entries.empty())
	{
		LOG_INFO() << "Ok: list is not empty";
	}
	else
	{
		LOG_INFO() << "Failed: list is empty";
		m_Failed = true;
	}
	
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