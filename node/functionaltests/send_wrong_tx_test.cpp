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

	void GenerateTx();
};

TestNodeConnection::TestNodeConnection(int argc, char* argv[])
	: NewTxConnection(argc, argv)
{
}

void TestNodeConnection::GenerateTests()
{
	m_Tests.push_back([this]()
	{
		LOG_INFO() << "Send tx with input = empty, output = 1, fee = 0";
		TxGenerator gen(*m_pKdf);
		
		// Inputs are empty
		
		// Outputs
		gen.GenerateOutputInTx(1, 1);

		// Kernels
		gen.GenerateKernel(1);
		
		gen.Sort();

		LOG_INFO() << "tx.IsValid == " << gen.IsValid();

		Send(gen.GetTransaction());
	});
	m_Results.push_back(false);

	m_Tests.push_back([this]()
	{
		LOG_INFO() << "Send tx with input = 1, output = empty, fee = 0";

		TxGenerator gen(*m_pKdf);

		// Inputs
		gen.GenerateInputInTx(2, 1);

		// Outputs are empty

		// Kernels
		gen.GenerateKernel(2);
		
		gen.Sort();

		LOG_INFO() << "tx.IsValid == " << gen.IsValid();

		Send(gen.GetTransaction());
	});
	m_Results.push_back(false);

	m_Tests.push_back([this]()
	{
		LOG_INFO() << "Send tx with input = 1, output = 1, fee = 1";

		TxGenerator gen(*m_pKdf);

		// Inputs 
		gen.GenerateInputInTx(3, 1);

		// Outputs
		gen.GenerateOutputInTx(3, 1);

		// Kernels
		gen.GenerateKernel(3);

		gen.Sort();

		LOG_INFO() << "tx.IsValid == " << gen.IsValid();

		Send(gen.GetTransaction());
	});
	m_Results.push_back(true);

	m_Tests.push_back([this]()
	{
		LOG_INFO() << "Send tx with 2 inputs, 2 ouputs and 1 kernel";

		TxGenerator gen(*m_pKdf);

		// Inputs 
		gen.GenerateInputInTx(4, 1);
		gen.GenerateInputInTx(4, 1);

		// Outputs
		gen.GenerateOutputInTx(5, 1);
		gen.GenerateOutputInTx(5, 1);

		// Kernels
		gen.GenerateKernel(5);

		gen.Sort();

		LOG_INFO() << "tx.IsValid == " << gen.IsValid();

		Send(gen.GetTransaction());
	});
	m_Results.push_back(true);

	m_Tests.push_back([this]()
	{
		LOG_INFO() << "Send tx with 2 inputs, 1 ouputs and 1 kernel";

		TxGenerator gen(*m_pKdf);

		// Inputs 
		gen.GenerateInputInTx(6, 1);
		gen.GenerateInputInTx(6, 1);

		// Outputs
		gen.GenerateOutputInTx(6, 2);		

		// Kernels
		gen.GenerateKernel(6);
		gen.Sort();

		LOG_INFO() << "tx.IsValid == " << gen.IsValid();

		Send(gen.GetTransaction());
	});
	m_Results.push_back(true);

	m_Tests.push_back([this]()
	{
		LOG_INFO() << "Send tx with 1 inputs, 2 ouputs and 1 kernel";

		TxGenerator gen(*m_pKdf);

		// Inputs 
		gen.GenerateInputInTx(7, 2);

		// Outputs
		gen.GenerateOutputInTx(7, 1);
		gen.GenerateOutputInTx(7, 1);

		// Kernels
		gen.GenerateKernel(7);
		gen.Sort();

		LOG_INFO() << "tx.IsValid == " << gen.IsValid();

		Send(gen.GetTransaction());
	});
	m_Results.push_back(true);

	m_Tests.push_back([this]()
	{
		LOG_INFO() << "Send tx with input = 1, output = 2, fee = 0";

		TxGenerator gen(*m_pKdf);

		// Inputs 
		gen.GenerateInputInTx(8, 1);

		// Outputs
		gen.GenerateOutputInTx(8, 2);

		// Kernels
		gen.GenerateKernel(8);
		gen.Sort();

		LOG_INFO() << "tx.IsValid == " << gen.IsValid();

		Send(gen.GetTransaction());
	});
	m_Results.push_back(false);

	m_Tests.push_back([this]()
	{
		LOG_INFO() << "Send tx with input = 2, output = 1, fee = 0";

		TxGenerator gen(*m_pKdf);

		// Inputs 
		gen.GenerateInputInTx(9, 2);

		// Outputs
		gen.GenerateOutputInTx(9, 1);

		// Kernels
		gen.GenerateKernel(9);
		gen.Sort();

		LOG_INFO() << "tx.IsValid == " << gen.IsValid();

		Send(gen.GetTransaction());
	});
	m_Results.push_back(false);

	m_Tests.push_back([this]()
	{
		LOG_INFO() << "Send tx with input = 2, output = 3, fee = 0";

		TxGenerator gen(*m_pKdf);

		// Inputs 
		gen.GenerateInputInTx(10, 2);

		// Outputs
		gen.GenerateOutputInTx(10, 3);

		// Kernels
		gen.GenerateKernel(10);
		gen.Sort();

		LOG_INFO() << "tx.IsValid == " << gen.IsValid();

		Send(gen.GetTransaction());
	});
	m_Results.push_back(false);

	m_Tests.push_back([this]()
	{
		LOG_INFO() << "Send tx with input = 4, output = 2, fee = 2";

		TxGenerator gen(*m_pKdf);

		// Inputs 
		gen.GenerateInputInTx(11, 4);

		// Outputs
		gen.GenerateOutputInTx(11, 2);

		// Kernels
		gen.GenerateKernel(11, 2);
		gen.Sort();

		LOG_INFO() << "tx.IsValid == " << gen.IsValid();

		Send(gen.GetTransaction());
	});
	m_Results.push_back(true);

	m_Tests.push_back([this]()
	{
		LOG_INFO() << "Send tx with input = 4, output = 2, fee = 1, fee = 1";

		TxGenerator gen(*m_pKdf);

		// Inputs 
		gen.GenerateInputInTx(12, 4);

		// Outputs
		gen.GenerateOutputInTx(12, 2);

		// Kernels
		gen.GenerateKernel(12, 1);
		gen.GenerateKernel(12, 1);
		gen.Sort();

		LOG_INFO() << "tx.IsValid == " << gen.IsValid();

		Send(gen.GetTransaction());
	});
	m_Results.push_back(true);

	m_Tests.push_back([this]()
	{
		LOG_INFO() << "Send tx with input = 4, output = 2, fee = 1, fee = 1, fee = 1";

		TxGenerator gen(*m_pKdf);

		// Inputs 
		gen.GenerateInputInTx(13, 4);

		// Outputs
		gen.GenerateOutputInTx(13, 2);

		// Kernels
		gen.GenerateKernel(13, 1);
		gen.GenerateKernel(13, 1);
		gen.GenerateKernel(13, 1);
		gen.Sort();

		LOG_INFO() << "tx.IsValid == " << gen.IsValid();

		Send(gen.GetTransaction());
	});
	m_Results.push_back(false);

	m_Tests.push_back([this]()
	{
		LOG_INFO() << "Send tx with input = 4, output = 2, fee = 3";

		TxGenerator gen(*m_pKdf);

		// Inputs 
		gen.GenerateInputInTx(14, 4);

		// Outputs
		gen.GenerateOutputInTx(14, 2);

		// Kernels
		gen.GenerateKernel(14, 3);
		gen.Sort();

		LOG_INFO() << "tx.IsValid == " << gen.IsValid();

		Send(gen.GetTransaction());
	});
	m_Results.push_back(false);

	m_Tests.push_back([this]()
	{
		LOG_INFO() << "Send tx with input = 2, without output, fee = 2";

		TxGenerator gen(*m_pKdf);

		// Inputs 
		gen.GenerateInputInTx(15, 2);

		// Kernels
		gen.GenerateKernel(15, 2);
		gen.Sort();

		LOG_INFO() << "tx.IsValid == " << gen.IsValid();

		Send(gen.GetTransaction());
	});
	m_Results.push_back(false);

	m_Tests.push_back([this]()
	{
		LOG_INFO() << "Send tx with input = 2, output = 1, fee = 1, offset of tx = 0";

		TxGenerator gen(*m_pKdf);

		// Inputs 
		gen.GenerateInputInTx(16, 2);

		// Outputs
		gen.GenerateOutputInTx(16, 2);

		// Kernels
		gen.GenerateKernel(16, 1);

		gen.ZeroOffset();
		gen.Sort();

		LOG_INFO() << "tx.IsValid == " << gen.IsValid();

		Send(gen.GetTransaction());
	});
	m_Results.push_back(false);

	m_Tests.push_back([this]()
	{
		LOG_INFO() << "Send tx without input, output , fee ";

		TxGenerator gen(*m_pKdf);

		LOG_INFO() << "tx.IsValid == " << gen.IsValid();

		Send(gen.GetTransaction());
	});
	m_Results.push_back(false);

	m_Tests.push_back([this]()
	{
		LOG_INFO() << "Send tx with input.m_Commitment = ouput.m_Commitment, fee = 0";

		TxGenerator gen(*m_pKdf);

		// Inputs 
		gen.GenerateInputInTx(17, 2, Key::Type::Coinbase);

		// Outputs
		gen.GenerateOutputInTx(17, 2, Key::Type::Coinbase);

		// Kernels
		gen.GenerateKernel(17, 0);

		gen.Sort();

		LOG_INFO() << "tx.IsValid == " << gen.IsValid();

		Send(gen.GetTransaction());
	});
	m_Results.push_back(false);

	m_Tests.push_back([this]()
	{
		LOG_INFO() << "Send tx with input = 0, ouput = 0, fee = 0";

		TxGenerator gen(*m_pKdf);

		// Inputs 
		gen.GenerateInputInTx(18, 0);

		// Outputs
		gen.GenerateOutputInTx(18, 0, Key::Type::Regular, false);

		// Kernels
		gen.GenerateKernel(18, 0);

		gen.Sort();

		LOG_INFO() << "tx.IsValid == " << gen.IsValid();

		Send(gen.GetTransaction());
	});
	m_Results.push_back(false);

	m_Tests.push_back([this]()
	{
		LOG_INFO() << "Send tx with input = 2, ouput = 0, fee = 2";

		TxGenerator gen(*m_pKdf);

		// Inputs 
		gen.GenerateInputInTx(19, 2);

		// Outputs
		gen.GenerateOutputInTx(19, 0, Key::Type::Regular, false);

		// Kernels
		gen.GenerateKernel(19, 2);

		gen.Sort();

		LOG_INFO() << "tx.IsValid == " << gen.IsValid();

		Send(gen.GetTransaction());
	});
	m_Results.push_back(false);
}

int main(int argc, char* argv[])
{
	int logLevel = LOG_LEVEL_DEBUG;
	auto logger = Logger::create(logLevel, logLevel);

	TestNodeConnection connection(argc, argv);

	connection.Run();

	return connection.CheckOnFailed();
}