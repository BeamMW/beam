// Copyright 2018 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "node/node.h"
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
		bool m_IsProcessChecking;
	};
public:
	TestNodeConnection(int argc, char* argv[]);
private:
	void GenerateTests() override;
	void OnMsg(proto::NewTip&&) override;

private:
	bool m_IsInit;
	//bool m_IsNeedToCheckOut;
	//unsigned int m_Counter;
	Block::SystemState::ID m_ID;
	TxGenerator m_Generator;
	CoinsChecker m_CoinsChecker;
	std::vector<std::tuple<Height, uint32_t, Input>> m_List;
	std::vector<Coin> m_Coins;

	io::Timer::Ptr m_NewTimer;

	const Amount m_Amount = 500;
};

TestNodeConnection::TestNodeConnection(int argc, char* argv[])
	: BaseTestNode(argc, argv)
	, m_IsInit(false)
	//, m_IsNeedToCheckOut(false)
	//, m_Counter(0)
	, m_Generator(*m_pKdf)
	, m_CoinsChecker(argc, argv)
	, m_NewTimer(io::Timer::create(io::Reactor::get_Current()))
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
	msg.m_Description.get_ID(m_ID);
	LOG_INFO() << "NewTip: " << m_ID;

	if (!m_IsInit)
	{
		m_IsInit = true;
		m_Generator.GenerateInputInTx(m_ID.m_Height - 70, Rules::get().CoinbaseEmission);

		for (Amount i = 0; i < m_Amount; ++i)
		{
			m_Generator.GenerateOutputInTx(m_ID.m_Height, 1, Key::Type::Regular, false, static_cast<uint32_t>(i));
			const Output::Ptr& output = m_Generator.GetTransaction().m_Transaction->m_vOutputs.back();
			Input input;
			input.m_Commitment = output->m_Commitment;
			m_Coins.push_back(Coin{ m_ID.m_Height, m_ID.m_Height, static_cast<uint32_t>(i), input, false, false });
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
					TxGenerator gen(*m_pKdf);

					LOG_INFO() << "Send coin #" << coin.m_Ind << "; input = " << coin.m_Input.m_Commitment << "; h = " << coin.m_Height << "; m_ID.m_Height = " << m_ID.m_Height;

					gen.GenerateInputInTx(coin.m_Height, 1, Key::Type::Regular, coin.m_Ind);
					gen.GenerateOutputInTx(m_ID.m_Height, 1, Key::Type::Regular, false, coin.m_Ind);
					gen.GenerateKernel(m_ID.m_Height, 0, coin.m_Ind);

					const Output::Ptr& output = gen.GetTransaction().m_Transaction->m_vOutputs.back();
					Input input;
					input.m_Commitment = output->m_Commitment;

					coin.m_Height = m_ID.m_Height;
					coin.m_Input = input;
					coin.m_IsValid = false;
					coin.m_IsProcessChecking = false;

					LOG_INFO() << "Send coin #" << coin.m_Ind << "; output = " << input.m_Commitment;

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
			if(!coin.m_IsValid && !coin.m_IsProcessChecking)
			{
				LOG_INFO() << "Add to check commitment = " << coin.m_Input.m_Commitment;

				coin.m_IsProcessChecking = true;
				m_CoinsChecker.Check(CoinsChecker::Inputs{ coin.m_Input },
					[&coin](bool isOk, Height maturity)
					{
						if (isOk)
						{
							LOG_INFO() << "Coin is valid: #" << coin.m_Ind << "; input = " << coin.m_Input.m_Commitment << "; maturity = " << maturity;
							coin.m_IsValid = true;
							coin.m_IsProcessChecking = false;
							coin.m_Maturity = maturity;
						}
						else
						{
							coin.m_IsProcessChecking = false;
						}
					});
			}
		}
	}
}

int main(int argc, char* argv[])
{
	int logLevel = LOG_LEVEL_DEBUG;
	int fileLogLevel = LOG_LEVEL_INFO;
	auto logger = beam::Logger::create(logLevel, logLevel, fileLogLevel, "test_");

	TestNodeConnection connection(argc, argv);

	connection.Run();

	return connection.CheckOnFailed();
}
