#include "../node.h"
#include "utility/logger.h"

using namespace beam;
using namespace ECC;

Initializer g_Initializer;

class TestNodeConnection : public proto::NodeConnection
{
public:
	TestNodeConnection();
private:
	void OnConnected() override;
	void OnClosed(int errorCode) override;
	//bool OnMsg2(proto::Boolean&& msg) override;	

	void SendTransaction(Height h);	
private:

	ECC::Kdf m_Kdf;
};

TestNodeConnection::TestNodeConnection()
{
	NoLeak<uintBig> walletSeed;
	Hash::Value hv;

	Hash::Processor() << "321" >> hv;
	walletSeed.V = hv;

	m_Kdf.m_Secret = walletSeed;
}

void TestNodeConnection::OnConnected()
{
	LOG_INFO() << "connection is succeded";	

	for (int i = 1; i < 11; i++)
	{
		SendTransaction(i);
	}
}

void TestNodeConnection::OnClosed(int errorCode)
{
	LOG_ERROR() << "problem with connecting to node: code = " << io::error_str(static_cast<io::ErrorCode>(errorCode));
	io::Reactor::get_Current().stop();
}

//bool TestNodeConnection::OnMsg2(proto::Boolean&& msg)
//{
//	LOG_INFO() << "Boolean is received: value = " << msg.m_Value;
//	return true;
//}

void TestNodeConnection::SendTransaction(Height h)
{
	proto::NewTransaction msgTx;

	msgTx.m_Transaction = std::make_shared<Transaction>();

	// Inputs
	ECC::Scalar::Native key1;
	DeriveKey(key1, m_Kdf, h, KeyType::Coinbase);
	ECC::Scalar::Native offset = key1;

	Input::Ptr pInp(new Input);
	pInp->m_Commitment = ECC::Commitment(key1, Rules::CoinbaseEmission);
	msgTx.m_Transaction->m_vInputs.push_back(std::move(pInp));

	// Outputs
	Output::Ptr pOut(new Output);
	ECC::Scalar::Native key2;

	DeriveKey(key2, m_Kdf, h, KeyType::Regular);
	pOut->m_Incubation = 2;
	pOut->Create(key2, Rules::CoinbaseEmission, true);
	msgTx.m_Transaction->m_vOutputs.push_back(std::move(pOut));

	key2 = -key2;
	offset += key2;

	// Kernels
	TxKernel::Ptr pKrn(new TxKernel);
	ECC::Scalar::Native key3;

	DeriveKey(key2, m_Kdf, h, KeyType::Kernel);
	pKrn->m_Excess = ECC::Point::Native(ECC::Context::get().G * key3);

	ECC::Hash::Value hv;
	pKrn->get_HashForSigning(hv);
	pKrn->m_Signature.Sign(hv, key3);
	msgTx.m_Transaction->m_vKernelsOutput.push_back(std::move(pKrn));

	key3 = -key3;
	offset += key3;

	msgTx.m_Transaction->m_Offset = offset;

	Transaction::Context ctx;
	if (msgTx.m_Transaction->IsValid(ctx))
	{
		Send(msgTx);
		return;
	}
}

int main()
{
	int logLevel = LOG_LEVEL_DEBUG;
#if LOG_VERBOSE_ENABLED
	logLevel = LOG_LEVEL_VERBOSE;
#endif
	auto logger = Logger::create(logLevel, logLevel);
	
	io::Reactor::Ptr reactor(io::Reactor::create());
	io::Reactor::Scope scope(*reactor);

	TestNodeConnection connection;

	io::Address addr;
	addr.resolve("127.0.0.1");
	addr.port(10000);

	connection.Connect(addr);

	reactor->run();

	return 0;
}