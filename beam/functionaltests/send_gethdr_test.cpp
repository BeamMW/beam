#include "beam/node.h"
#include "utility/logger.h"
#include "tools/base_node_connection.h"

using namespace beam;

class TestNodeConnection : public BaseTestNode
{
public:
	TestNodeConnection(int argc, char* argv[]);
private:
	virtual void OnMsg(proto::NewTip&&) override;
	virtual void OnMsg(proto::Hdr&&) override;
	virtual void OnMsg(proto::Body&&) override;
	virtual void OnMsg(proto::DataMissing&&) override;

private:
	bool m_IsInit;
	Block::SystemState::ID m_ID;
	bool m_IsSendWrongBody;
};

TestNodeConnection::TestNodeConnection(int argc, char* argv[])
	: BaseTestNode(argc, argv)
	, m_IsInit(false)
	, m_IsSendWrongBody(false)
{
	m_Timeout = 10 * 1000;
}

void TestNodeConnection::OnMsg(proto::NewTip&& msg)
{
	if (!m_IsInit)
	{
		LOG_INFO() << "NewTip: " << msg.m_ID;

		m_ID = msg.m_ID;
		m_IsInit = true;

		LOG_INFO() << "Send GetHdr message";
		Send(proto::GetHdr{ m_ID });
	}
}

void TestNodeConnection::OnMsg(proto::Hdr&& msg)
{
	LOG_INFO() << "Ok: Header is received: height =  " << msg.m_Description.m_Height;

	LOG_INFO() << "Send GetBody message";
	Send(proto::GetBody{ m_ID });
}

void TestNodeConnection::OnMsg(proto::Body&& )
{
	LOG_INFO() << "Ok: Body is received";
	proto::GetHdr hdrMsg;

	hdrMsg.m_ID = m_ID;
	hdrMsg.m_ID.m_Height += 2;

	LOG_INFO() << "Send GetHdr with wrong ID";
	Send(hdrMsg);
}

void TestNodeConnection::OnMsg(proto::DataMissing&& )
{
	LOG_INFO() << "Ok: DataMissing is received";

	if (!m_IsSendWrongBody)
	{
		m_IsSendWrongBody = true;

		proto::GetBody bodyMsg;

		bodyMsg.m_ID = m_ID;
		bodyMsg.m_ID.m_Height += 2;

		LOG_INFO() << "Send GetBody with wrong ID";
		Send(bodyMsg);

		return;
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