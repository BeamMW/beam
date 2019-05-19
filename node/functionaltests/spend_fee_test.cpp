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
using namespace ECC;

class TestNodeConnection : public BaseTestNode
{
public:
	TestNodeConnection(int argc, char* argv[]);
private:
	void GenerateTests() override;
	void OnMsg(proto::NewTip&&) override;
	void OnMsg(proto::Status&&) override;
	void OnMsg(proto::Mined&&) override;

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
	, m_Generator(*m_pKdf)
	, m_FeeGenerator(*m_pKdf)
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
		m_ID = id;
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
				[this](bool isOk, Height)
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

			m_FeeGenerator.GenerateInputInTx(mined.m_ID.m_Height, mined.m_Fees, Key::Type::Comission);
			m_FeeGenerator.GenerateOutputInTx(mined.m_ID.m_Height + 1, mined.m_Fees);
			m_FeeGenerator.GenerateKernel(mined.m_ID.m_Height + 1);

			m_CoinsChecker.Check(CoinsChecker::Inputs{ *m_FeeGenerator.GetTransaction().m_Transaction->m_vInputs.front() }, 
				[this] (bool isOk, Height)
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
	auto logger = Logger::create(logLevel, logLevel);

	TestNodeConnection connection(argc, argv);

	connection.Run();

	return connection.CheckOnFailed();
}