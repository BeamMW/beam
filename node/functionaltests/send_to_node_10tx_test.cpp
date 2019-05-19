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

using namespace beam;

class TestNodeConnection : public NewTxConnection
{
public:
	TestNodeConnection(int argc, char* argv[]);
private:
	void GenerateTests() override;
};

TestNodeConnection::TestNodeConnection(int argc, char* argv[])
	: NewTxConnection(argc, argv)
{
}


void TestNodeConnection::GenerateTests()
{
	for (int i = 1; i <= 10; i++)
	{
		m_Tests.push_back([this, i]()
		{
			TxGenerator gen(*m_pKdf);
			
			// Inputs
			gen.GenerateInputInTx(i, 1);
			// Outputs
			gen.GenerateOutputInTx(i, 1);
			// Kernels
			gen.GenerateKernel(4);

            LOG_INFO() << "tx.IsValid == " << gen.IsValid();

			Send(gen.GetTransaction());
		});
		m_Results.push_back(true);
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
