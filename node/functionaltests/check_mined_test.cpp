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
#include "tools/coins_checker.h"
#include "tools/base_node_connection.h"

using namespace beam;
using namespace ECC;

class TestNodeConnection : public BaseTestNode
{
public:
	TestNodeConnection(int argc, char* argv[]);
private:
	void GenerateTests() override;
	void OnMsg(proto::NewTip&&) override;
	void OnMsg(proto::Mined&&) override;
	
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
		Block::SystemState::ID id;
		msg.m_Description.get_ID(id);

		LOG_INFO() << "NewTip: " << id;

		m_IsInit = true;		

		LOG_INFO() << "Send GetMined message";
		Send(proto::GetMined{ msg.m_Description.m_Height - 1000 });
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
	Input input;
	SwitchCommitment::Create(key, input.m_Commitment, *m_pKdf, Key::IDV(Rules::get().CoinbaseEmission, mined.m_ID.m_Height, Key::Type::Coinbase));

	m_CoinsChecker.Check(CoinsChecker::Inputs{ input },
		[this](bool isOk, Height)
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
	auto logger = Logger::create(logLevel, logLevel);

	TestNodeConnection connection(argc, argv);

	connection.Run();

	return connection.CheckOnFailed();
}