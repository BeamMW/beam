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

using namespace beam;

class TestNodeConnection : public BaseTestNode
{
public:
	TestNodeConnection(int argc, char* argv[]);
private:
	void GenerateTests() override;
	void OnMsg(proto::NewTip&&) override;
	void OnMsg(proto::Status&&) override;

private:
	bool m_IsInit;
	bool m_IsNeedToCheckOut;
	unsigned int m_Counter;
	TxGenerator m_Generator;
	CoinsChecker m_CoinsChecker;
};

TestNodeConnection::TestNodeConnection(int argc, char* argv[])
	: BaseTestNode(argc, argv)
	, m_IsInit(false)
	, m_IsNeedToCheckOut(false)
	, m_Counter(0)
	, m_Generator(*m_pKdf)
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
	Block::SystemState::ID id;
	msg.m_Description.get_ID(id);

	LOG_INFO() << "NewTip: " << id;

	if (!m_IsInit)
	{
		m_IsInit = true;

		m_Generator.GenerateInputInTx(msg.m_Description.m_Height - 70, Rules::get().CoinbaseEmission);
		m_Generator.GenerateInputInTx(msg.m_Description.m_Height - 71, 2 * Rules::get().CoinbaseEmission);
		m_Generator.GenerateOutputInTx(msg.m_Description.m_Height + 1, Rules::get().CoinbaseEmission);
		m_Generator.GenerateOutputInTx(msg.m_Description.m_Height + 2, 2 * Rules::get().CoinbaseEmission);
		m_Generator.GenerateKernel(msg.m_Description.m_Height + 5);
		m_Generator.Sort();
		Send(m_Generator.GetTransaction());
	}

	if (m_IsNeedToCheckOut)
	{
		if (++m_Counter >= 2)
		{
			m_CoinsChecker.Check(m_Generator.GenerateInputsFromOutputs(),
				[this](bool isOk, Height)
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

void TestNodeConnection::OnMsg(proto::Status&& msg)
{
	LOG_INFO() << "Status: value = " << static_cast<uint32_t>(msg.m_Value);

	if (proto::TxStatus::Ok != msg.m_Value)
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
	auto logger = Logger::create(logLevel, logLevel);

	TestNodeConnection connection(argc, argv);

	connection.Run();

	return connection.CheckOnFailed();
}