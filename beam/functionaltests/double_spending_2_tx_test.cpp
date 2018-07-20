#include "beam/node.h"
#include "utility/logger.h"
#include "tools/base_node_connection.h"
#include "tools/coins_checker.h"
#include "tools/tx_generator.h"

using namespace beam;

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
	TxGenerator m_Generator1;
	TxGenerator m_Generator2;
	CoinsChecker m_CoinsChecker;
};

TestNodeConnection::TestNodeConnection(int argc, char* argv[])
	: BaseTestNode(argc, argv)
	, m_IsInit(false)
	, m_IsNeedToCheckOut(false)
	, m_Counter(0)
	, m_Generator1(m_Kdf)
	, m_Generator2(m_Kdf)
	, m_CoinsChecker(argc, argv)
{
	m_Timeout = 5 * 60 * 1000;
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
	LOG_INFO() << "NewTip: " << msg.m_ID;

	if (!m_IsInit)
	{
		m_IsInit = true;

		m_Generator1.GenerateInputInTx(msg.m_ID.m_Height - 70, Rules::get().CoinbaseEmission);
		m_Generator1.GenerateOutputInTx(msg.m_ID.m_Height + 1, Rules::get().CoinbaseEmission);
		m_Generator1.GenerateKernel(msg.m_ID.m_Height + 5);
		m_Generator1.Sort();
		Send(m_Generator1.GetTransaction());

		m_Generator2.GenerateInputInTx(msg.m_ID.m_Height - 70, Rules::get().CoinbaseEmission);
		m_Generator2.GenerateOutputInTx(msg.m_ID.m_Height + 2, Rules::get().CoinbaseEmission);
		m_Generator2.GenerateKernel(msg.m_ID.m_Height + 6);
		m_Generator2.Sort();
		Send(m_Generator2.GetTransaction());
	}

	if (m_IsNeedToCheckOut)
	{
		if (++m_Counter >= 3)
		{
			m_CoinsChecker.Check(m_Generator1.GenerateInputsFromOutputs(),
				[this](bool isOk)
				{
					if (isOk)
					{
						LOG_INFO() << "Ok: utxo is valid";
					}
					else
					{
						LOG_INFO() << "Failed: utxo is not valid";
						m_Failed = true;
					}
				}
			);

			m_CoinsChecker.Check(m_Generator2.GenerateInputsFromOutputs(),
				[this](bool isOk)
				{
					if (isOk)
					{
						LOG_INFO() << "Failed: utxo is valid";
					}
					else
					{
						LOG_INFO() << "Ok: utxo is not valid";
						m_Failed = true;
					}
					io::Reactor::get_Current().stop();
				}
			);
		}
	}
}

void TestNodeConnection::OnMsg(proto::Boolean&& msg)
{
	LOG_INFO() << "Boolean: value = " << msg.m_Value;

	if (!msg.m_Value)
	{
		LOG_INFO() << "Failed: tx is invalid";
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