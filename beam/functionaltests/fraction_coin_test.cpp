#include "beam/node.h"
#include "utility/logger.h"
#include "tools/coins_checker.h"
#include "tools/tx_generator.h"


using namespace beam;
using namespace ECC;

class TestNodeConnection : public BaseTestNode
{
public:
	TestNodeConnection(int argc, char* argv[]);
private:
	virtual void GenerateTests() override;

	virtual void OnMsg(proto::NewTip&&) override;
	virtual void OnMsg(proto::Boolean&&) override;

private:
	bool m_IsInit;
	bool m_IsNeedToCheckOut;
	unsigned int m_Counter;
	Block::SystemState::ID m_ID;
	TxGenerator m_Generator;
	Input m_Input;
	CoinsChecker m_CoinsChecker;

	const Amount m_SpentAmount = 6000;
};

TestNodeConnection::TestNodeConnection(int argc, char* argv[])
	: BaseTestNode(argc, argv)
	, m_IsInit(false)
	, m_IsNeedToCheckOut(false)
	, m_Counter(0)
	, m_Generator(m_Kdf)
	, m_CoinsChecker(argc, argv)
{
	m_Timeout = 5 * 60 * 1000;

	Rules::get().FakePoW = true;
	Rules::get().UpdateChecksum();
}

void TestNodeConnection::GenerateTests()
{
	m_Tests.push_back([this]()
	{
		m_CoinsChecker.InitChecker();
	});
}

void TestNodeConnection::OnMsg(proto::NewTip&& msg)
{
	LOG_INFO() << "NewTip: " << msg.m_ID;

	if (!m_IsInit)
	{
		m_ID = msg.m_ID;
		m_IsInit = true;

		m_Generator.GenerateInputInTx(m_ID.m_Height - 70, Rules::get().CoinbaseEmission);
		for (Amount i = 0; i < m_SpentAmount; ++i)
		{
			m_Generator.GenerateOutputInTx(m_ID.m_Height - 70, 1);
		}
		m_Generator.GenerateKernel(m_ID.m_Height - 70, Rules::get().CoinbaseEmission - m_SpentAmount);

		m_Generator.Sort();

		LOG_INFO() << "Is valid = " << m_Generator.IsValid();

		Send(m_Generator.GetTransaction());
	}

	if (m_IsNeedToCheckOut)
	{
		if (++m_Counter >= 2)
		{
			m_CoinsChecker.Check(m_Generator.GenerateInputsFromOutputs(),
				[](bool isOk) 
					{
						if (isOk)
							LOG_INFO() << "Everythink is Ok";
						else
							LOG_INFO() << "Everythink is Failed";
						io::Reactor::get_Current().stop();
					}
			);
			m_IsNeedToCheckOut = false;
		}
	}
}

void TestNodeConnection::OnMsg(proto::Boolean&& msg)
{
	LOG_INFO() << "Boolean: value = " << msg.m_Value;

	if (!msg.m_Value)
	{
		LOG_INFO() << "Failed:";
		m_Failed = true;
		io::Reactor::get_Current().stop();
		return;
	}

	m_IsNeedToCheckOut = true;
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