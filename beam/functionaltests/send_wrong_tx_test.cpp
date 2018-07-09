#include "beam/node.h"
#include "utility/logger.h"

#include "tools/base_node_connection.h"
#include "tools/tx_generator.h"

using namespace beam;
using namespace ECC;

class TestNodeConnection : public BaseTestNodeConnection
{
public:
	TestNodeConnection(int argc, char* argv[]);
private:
	void GenerateTests() override;

	void GenerateTx();
};

TestNodeConnection::TestNodeConnection(int argc, char* argv[])
	: BaseTestNodeConnection(argc, argv)
{
}

void TestNodeConnection::GenerateTests()
{
	m_Tests.push_back(std::make_pair([this]()
	{
		LOG_INFO() << "Run test without inputs";
		TxGenerator gen(m_Kdf);
		
		// Inputs are empty
		
		// Outputs
		gen.GenerateOutputInTx(1, 1);

		// Kernels
		gen.GenerateKernel(1);
		
		gen.Sort();

		LOG_INFO() << "tx.IsValid == " << gen.IsValid();

		Send(gen.GetTransaction());
	}, false));

	m_Tests.push_back(std::make_pair([this]()
	{
		LOG_INFO() << "Run test without outputs";

		TxGenerator gen(m_Kdf);

		// Inputs
		gen.GenerateInputInTx(2, 1);

		// Outputs are empty

		// Kernels
		gen.GenerateKernel(2);
		
		gen.Sort();

		LOG_INFO() << "tx.IsValid == " << gen.IsValid();

		Send(gen.GetTransaction());
	}, false));

	m_Tests.push_back(std::make_pair([this]()
	{
		LOG_INFO() << "Run test with normal tx";

		TxGenerator gen(m_Kdf);

		// Inputs 
		gen.GenerateInputInTx(3, 1);

		// Outputs
		gen.GenerateOutputInTx(3, 1);

		// Kernels
		gen.GenerateKernel(3);

		gen.Sort();

		LOG_INFO() << "tx.IsValid == " << gen.IsValid();

		Send(gen.GetTransaction());
	}, true));

	m_Tests.push_back(std::make_pair([this]()
	{
		LOG_INFO() << "Run test with 2 inputs, 2 ouputs and 1 kernel";

		TxGenerator gen(m_Kdf);

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
	}, true));

	m_Tests.push_back(std::make_pair([this]()
	{
		LOG_INFO() << "Run test with 2 inputs, 1 ouputs and 1 kernel";

		TxGenerator gen(m_Kdf);

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
	}, true));

	m_Tests.push_back(std::make_pair([this]()
	{
		LOG_INFO() << "Run test with 1 inputs, 2 ouputs and 1 kernel";

		TxGenerator gen(m_Kdf);

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
	}, true));

	m_Tests.push_back(std::make_pair([this]()
	{
		LOG_INFO() << "Run test with input = 1 chattle, output= 2 chattles, fee=0";

		TxGenerator gen(m_Kdf);

		// Inputs 
		gen.GenerateInputInTx(8, 1);

		// Outputs
		gen.GenerateOutputInTx(8, 2);

		// Kernels
		gen.GenerateKernel(8);
		gen.Sort();

		LOG_INFO() << "tx.IsValid == " << gen.IsValid();

		Send(gen.GetTransaction());
	}, false));

	m_Tests.push_back(std::make_pair([this]()
	{
		LOG_INFO() << "Run test with input = 2 chattle, output= 1 chattles, fee=0";

		TxGenerator gen(m_Kdf);

		// Inputs 
		gen.GenerateInputInTx(9, 2);

		// Outputs
		gen.GenerateOutputInTx(9, 1);

		// Kernels
		gen.GenerateKernel(9);
		gen.Sort();

		LOG_INFO() << "tx.IsValid == " << gen.IsValid();

		Send(gen.GetTransaction());
	}, false));

	m_Tests.push_back(std::make_pair([this]()
	{
		LOG_INFO() << "Run test with input = 2 chattle, output= 3 chattles, fee=0";

		TxGenerator gen(m_Kdf);

		// Inputs 
		gen.GenerateInputInTx(10, 2);

		// Outputs
		gen.GenerateOutputInTx(10, 3);

		// Kernels
		gen.GenerateKernel(10);
		gen.Sort();

		LOG_INFO() << "tx.IsValid == " << gen.IsValid();

		Send(gen.GetTransaction());
	}, false));

	m_Tests.push_back(std::make_pair([this]()
	{
		LOG_INFO() << "Run test with input = 4 chattle, output= 2 chattle, fee = 2 chattles";

		TxGenerator gen(m_Kdf);

		// Inputs 
		gen.GenerateInputInTx(11, 4);

		// Outputs
		gen.GenerateOutputInTx(11, 2);

		// Kernels
		gen.GenerateKernel(11, 2);
		gen.Sort();

		LOG_INFO() << "tx.IsValid == " << gen.IsValid();

		Send(gen.GetTransaction());
	}, true));

	m_Tests.push_back(std::make_pair([this]()
	{
		LOG_INFO() << "Run test with input = 4 chattle, output= 2 chattle, fee= 1 chattle, fee = 1 chattle";

		TxGenerator gen(m_Kdf);

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
	}, true));

	m_Tests.push_back(std::make_pair([this]()
	{
		LOG_INFO() << "Run test with input = 4 chattle, output= 2 chattle, fee= 1 chattle, fee = 1 chattle, fee = 1 chattle";

		TxGenerator gen(m_Kdf);

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
	}, false));

	m_Tests.push_back(std::make_pair([this]()
	{
		LOG_INFO() << "Run test with input = 4 chattle, output= 2 chattle, fee= 3 chattles";

		TxGenerator gen(m_Kdf);

		// Inputs 
		gen.GenerateInputInTx(14, 4);

		// Outputs
		gen.GenerateOutputInTx(14, 2);

		// Kernels
		gen.GenerateKernel(14, 3);
		gen.Sort();

		LOG_INFO() << "tx.IsValid == " << gen.IsValid();

		Send(gen.GetTransaction());
	}, false));
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