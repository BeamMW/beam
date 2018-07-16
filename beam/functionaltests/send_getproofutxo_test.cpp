#include "beam/node.h"
#include "utility/logger.h"
#include "tools/base_node_connection.h"

using namespace beam;

class TestNodeConnection : public BaseTestNode
{
public:
	TestNodeConnection(int argc, char* argv[]);
private:
	virtual void OnDisconnect(const DisconnectReason&) override;
	virtual void GenerateTests() override;
	virtual void OnMsg(proto::ProofUtxo&& msg) override;
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
		LOG_INFO() << "Send GetProofUtxo message";
		Send(proto::GetProofUtxo());
	});
}

void TestNodeConnection::OnMsg(proto::ProofUtxo&& msg)
{
	if (msg.m_Proofs.empty())
	{
		LOG_INFO() << "ProofUtxo: Ok - list is empty";
	}
	else
	{
		LOG_INFO() << "ProofUtxo: Failed - list is not empty";
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