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
#include "tools/tx_generator.h"
#include "tools/new_tx_tests.h"

#include <vector>
#include <thread>
#include <future>

using namespace beam;
using namespace ECC;

class TestNodeConnection : public NewTxConnection
{
public:
	TestNodeConnection(int argc, char* argv[], int h);
	void BeforeConnection(Height h);
private:

	void GenerateTests() override;

private:
	int m_H;
	proto::NewTransaction m_MsgTx;
};

TestNodeConnection::TestNodeConnection(int argc, char* argv[], int h)
	: NewTxConnection(argc, argv)
	, m_H(h)
{
	m_Timeout = 0;
}

void TestNodeConnection::BeforeConnection(Height h)
{
	TxGenerator gen(*m_pKdf);

	Amount amount = 18000;

	// Inputs
	gen.GenerateInputInTx(h, amount);

	// Outputs
	for (Amount i = 0; i < amount; ++i)
	{
		gen.GenerateOutputInTx(h, 1);
	}

	// Kernels
	gen.GenerateKernel(h);

	gen.Sort();

	m_MsgTx = gen.GetTransaction();
}

void TestNodeConnection::GenerateTests()
{
	for (int i = 0; i < 3; ++i)
	{
		m_Tests.push_back([this, i]()
		{
			LOG_INFO() << "Send big transaction";
			BeforeConnection(100 * (m_H + 2) + i);

			Send(m_MsgTx);
		});
		m_Results.push_back(true);
	}
}

void SendData(int argc, char* argv[], int h)
{
	TestNodeConnection connection(argc, argv, h);
		
	connection.Run();
}

int main(int argc, char* argv[])
{
	int logLevel = LOG_LEVEL_DEBUG;
	auto logger = Logger::create(logLevel, logLevel);
	std::vector<std::future<void>> futures;

	for (int i = 0; i < 10; i++)
	{
		futures.push_back(std::async(SendData, argc, argv, i));
	}

	for (auto &f : futures)
	{
		f.get();
	}
}