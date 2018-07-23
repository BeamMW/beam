#include "beam/node.h"
#include "utility/logger.h"
#include "tools/coins_checker.h"
#include "tools/base_node_connection.h"

using namespace beam;
using namespace ECC;

class TestNodeConnection : public BaseTestNode
{
public:
	TestNodeConnection(int argc, char* argv[]);
private:
	virtual void GenerateTests() override;
	virtual void OnMsg(proto::NewTip&&) override;
	virtual void OnMsg(proto::Mined&&) override;
	
private:
	bool m_IsInit;
	CoinsChecker m_CoinsChecker;	
};

TestNodeConnection::TestNodeConnection(int argc, char* argv[])
	: BaseTestNode(argc, argv)
	, m_IsInit(false)
	, m_CoinsChecker(argc, argv)
{
	m_Timeout = 10 * 1000;
}

void TestNodeConnection::GenerateTests()
{
	m_Tests.push_back([this]
	{
		m_CoinsChecker.InitChecker();
	});
}

void TestNodeConnection::OnMsg(proto::NewTip&& msg)
{
	if (!m_IsInit)
	{
		LOG_INFO() << "NewTip: " << msg.m_ID;

		m_IsInit = true;		

		LOG_INFO() << "Send GetMined message";
		Send(proto::GetMined{ msg.m_ID.m_Height });
	}
}

void TestNodeConnection::OnMsg(proto::Mined&& msg)
{
	LOG_INFO() << "Mined";	

	if (!msg.m_Entries.empty())
	{
		LOG_INFO() << "Ok: list is not empty";
	}
	else
	{
		LOG_INFO() << "Failed: list is empty";
		m_Failed = true;
		io::Reactor::get_Current().stop();

		return;
	}

	proto::PerMined mined = msg.m_Entries.front();
	
	Scalar::Native key;
	DeriveKey(key, m_Kdf, mined.m_ID.m_Height, KeyType::Coinbase);

	Input input;
	input.m_Commitment = Commitment(key, Rules::get().CoinbaseEmission);

	m_CoinsChecker.Check(CoinsChecker::Inputs{ input },
		[this](bool isOk, Height maturity)
		{
			if (isOk)
			{
				LOG_INFO() << "OK: utxo is valid";
			}
			else
			{
				LOG_INFO() << "Failed: utxo is not valid";
				m_Failed = true;
				
			}
			io::Reactor::get_Current().stop();
		}
	);
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