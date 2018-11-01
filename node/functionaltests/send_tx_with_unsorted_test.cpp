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

	TxGenerator GenerateTx();
};

TestNodeConnection::TestNodeConnection(int argc, char* argv[])
	: NewTxConnection(argc, argv)
{

}

void TestNodeConnection::GenerateTests()
{
	m_Tests.push_back([this]()
	{
		LOG_INFO() << "Run test with unsorded kernels";

		TxGenerator gen = GenerateTx();
		
		gen.SortInputs();
		gen.SortOutputs();

		Send(gen.GetTransaction());
	});
	m_Results.push_back(false);

	m_Tests.push_back([this]()
	{
		LOG_INFO() << "Run test with unsorted inputs";

		TxGenerator gen = GenerateTx();

		gen.SortOutputs();
		gen.SortKernels();

		Send(gen.GetTransaction());
	});
	m_Results.push_back(false);

	m_Tests.push_back([this]()
	{
		LOG_INFO() << "Run test with unsorted outputs";

		TxGenerator gen = GenerateTx();

		gen.SortInputs();
		gen.SortKernels();

		Send(gen.GetTransaction());
	});
	m_Results.push_back(false);

	m_Tests.push_back([this]()
	{
		LOG_INFO() << "Run test with sorted inputs,outputs and kernels";

		TxGenerator gen = GenerateTx();
		gen.Sort();

		Send(gen.GetTransaction());
	});
	m_Results.push_back(true);
}

TxGenerator TestNodeConnection::GenerateTx()
{
	TxGenerator gen(*m_pKdf);

	// Inputs
	gen.GenerateInputInTx(4, 5);
	gen.GenerateInputInTx(2, 2);
	gen.GenerateInputInTx(3, 3);

	// Outputs
	gen.GenerateOutputInTx(5, 1);
	gen.GenerateOutputInTx(4, 4);
	gen.GenerateOutputInTx(2, 2);
	gen.GenerateOutputInTx(3, 3);

	// Kernels
	gen.GenerateKernel(4);
	gen.GenerateKernel(2);
	gen.GenerateKernel(3);
		
	return gen;
}

int main(int argc, char* argv[])
{
	int logLevel = LOG_LEVEL_DEBUG;
	auto logger = Logger::create(logLevel, logLevel);
	TestNodeConnection connection(argc, argv);

	connection.Run();

	return connection.CheckOnFailed();
}