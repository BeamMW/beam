#include "beam/node.h"
#include "utility/logger.h"
#include "tools/base_node_connection.h"
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
	virtual void OnMsg(proto::Mined&&) override;

private:
	bool m_IsInit;
	bool m_IsNeedToCheckFee;
	bool m_IsNewToCheckSpending;
	unsigned int m_Counter;
	Block::SystemState::ID m_ID;
	TxGenerator m_Generator;
	TxGenerator m_FeeGenerator;
	CoinsChecker m_CoinsChecker;

	const Amount m_Fee = 100;
};

TestNodeConnection::TestNodeConnection(int argc, char* argv[])
	: BaseTestNode(argc, argv)
	, m_IsInit(false)
	, m_IsNeedToCheckFee(false)
	, m_IsNewToCheckSpending(false)
	, m_Counter(0)
	, m_Generator(m_Kdf)
	, m_FeeGenerator(m_Kdf)
	, m_CoinsChecker(argc, argv)
{
	m_Timeout = 5 * 60 * 1000;

	Rules::get().FakePoW = true;
	Rules::get().UpdateChecksum();
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
		m_ID = msg.m_ID;
		m_IsInit = true;

		m_Generator.GenerateInputInTx(m_ID.m_Height - 70, Rules::get().CoinbaseEmission);
		m_Generator.GenerateOutputInTx(m_ID.m_Height + 1, Rules::get().CoinbaseEmission - m_Fee);
		m_Generator.GenerateKernel(m_ID.m_Height + 5, m_Fee);
		m_Generator.Sort();
		m_IsNeedToCheckFee = true;
		Send(m_Generator.GetTransaction());
		return;
	}

	if (m_IsNeedToCheckFee)
	{
		if (++m_Counter >= 2)
		{
			m_IsNeedToCheckFee = false;
			Send(proto::GetMined{ m_ID.m_Height });
		}
	}

	if (m_IsNewToCheckSpending)
	{
		if(++m_Counter >= 2)
		{
			m_CoinsChecker.Check(m_Generator.GenerateInputsFromOutputs(),
				[this](bool isOk)
				{
					if (isOk)
					{
						LOG_INFO() << "OK: utxo for fee is valid";
					}
					else
					{
						LOG_INFO() << "Failed: utxo for fee is not valid";
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
}

void TestNodeConnection::OnMsg(proto::Mined&& msg)
{
	LOG_INFO() << "Mined";
	bool isHaveFee = false;

	for (const auto& mined : msg.m_Entries)
	{
		if (mined.m_Fees > 0)
		{
			isHaveFee = true;

			m_FeeGenerator.GenerateInputInTx(mined.m_ID.m_Height, mined.m_Fees, KeyType::Comission);
			m_FeeGenerator.GenerateOutputInTx(mined.m_ID.m_Height + 1, mined.m_Fees);
			m_FeeGenerator.GenerateKernel(mined.m_ID.m_Height + 1);

			m_CoinsChecker.Check(CoinsChecker::Inputs{ *m_FeeGenerator.GetTransaction().m_Transaction->m_vInputs.front() }, 
				[this] (bool isOk)
				{
					if (isOk)
					{
						m_Counter = 0;
						m_IsNewToCheckSpending = true;
						Send(m_FeeGenerator.GetTransaction());
					}
					else
					{
						LOG_INFO() << "Failed: fee is invalid";
						io::Reactor::get_Current().stop();
					}
				}
			);
			break;
		}
	}

	if (!isHaveFee)
	{
		LOG_INFO() << "Failed: fee is absent";
		io::Reactor::get_Current().stop();
	}
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