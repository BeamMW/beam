#include "beam/node.h"
#include "utility/logger.h"
#include "tools/base_node_connection.h"
#include "tools/coins_checker.h"
#include "tools/tx_generator.h"

#include <tuple>
#include <list>

using namespace beam;
using namespace ECC;

class TestNodeConnection : public BaseTestNode
{
private:
	struct Coin
	{
		Height m_Height;
		Height m_Maturity;
		uint32_t m_Ind;
		Input m_Input;
		bool m_IsValid;
	};
public:
	TestNodeConnection(int argc, char* argv[]);
private:
	virtual void GenerateTests() override;
	virtual void OnMsg(proto::NewTip&&) override;

private:
	bool m_IsInit;
	bool m_IsNeedToCheckOut;
	unsigned int m_Counter;
	Block::SystemState::ID m_ID;
	TxGenerator m_Generator;
	CoinsChecker m_CoinsChecker;
	std::vector<std::tuple<Height, uint32_t, Input>> m_List;
	std::vector<Coin> m_Coins;

	io::Timer::Ptr m_NewTimer;

	const Amount m_Amount = 1000;
};

TestNodeConnection::TestNodeConnection(int argc, char* argv[])
	: BaseTestNode(argc, argv)
	, m_IsInit(false)
	, m_IsNeedToCheckOut(false)
	, m_Counter(0)
	, m_Generator(m_Kdf)
	, m_CoinsChecker(argc, argv)
	, m_NewTimer(io::Timer::create(io::Reactor::get_Current().shared_from_this()))
{
	m_Timeout = 0;
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

	m_ID = msg.m_ID;

	if (!m_IsInit)
	{
		m_IsInit = true;
		m_Generator.GenerateInputInTx(m_ID.m_Height - 70, Rules::get().CoinbaseEmission);

		for (Amount i = 0; i < m_Amount; ++i)
		{
			m_Generator.GenerateOutputInTx(m_ID.m_Height, 1, KeyType::Regular, false, i);
			const Output::Ptr& output = m_Generator.GetTransaction().m_Transaction->m_vOutputs.back();
			Input input;
			input.m_Commitment = output->m_Commitment;
			m_Coins.push_back(Coin{ m_ID.m_Height, m_ID.m_Height, static_cast<uint32_t>(i), input, false });
		}

		m_Generator.GenerateKernel(m_ID.m_Height, Rules::get().CoinbaseEmission - m_Amount);
		m_Generator.Sort();

		Send(m_Generator.GetTransaction());

		m_NewTimer->start(1, true, [this]
		{
			for (auto& coin : m_Coins)
			{
				if (coin.m_IsValid && coin.m_Maturity < m_ID.m_Height)
				{
					LOG_INFO() << "Send coin";
					TxGenerator gen(m_Kdf);

					gen.GenerateInputInTx(coin.m_Height, 1, KeyType::Regular, coin.m_Ind);
					gen.GenerateOutputInTx(m_ID.m_Height, 1, KeyType::Regular, false, coin.m_Ind);
					gen.GenerateKernel(m_ID.m_Height, 0, coin.m_Ind);

					const Output::Ptr& output = gen.GetTransaction().m_Transaction->m_vOutputs.back();
					Input input;
					input.m_Commitment = output->m_Commitment;

					coin.m_Height = m_ID.m_Height;
					coin.m_Input = input;
					coin.m_IsValid = false;

					Send(gen.GetTransaction());
					return;
				}
			}
		});
	}
	else
	{
		for (auto& coin : m_Coins)
		{
			if(!coin.m_IsValid)
			{
				m_CoinsChecker.Check(CoinsChecker::Inputs{ coin.m_Input },
					[this, &coin](bool isOk, Height maturity)
					{
						if (isOk)
						{
							coin.m_IsValid = true;
							coin.m_Maturity = maturity;
						}
					});
			}
		}
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