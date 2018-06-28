#include <thread>
#include <chrono>
#include <future>

#include "beam/node.h"
#include "utility/logger.h"

using namespace std::chrono_literals;
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
	bool OnMsg2(proto::NewTip&& msg) override;
	bool OnMsg2(proto::Hdr&& msg) override;
	bool OnMsg2(proto::GetHdr&& msg) override;
	bool OnMsg2(proto::Config&& msg) override;

	void SendTransaction(Height h);
	void SendEmptyTransaction();
private:

	proto::Hdr m_Hdr;

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
	//beam::io::Reactor::get_Current().stop();

	for (int i = 20; i < 30; i++)
	{
		LOG_INFO() << "Try to send transaction";		
		SendTransaction(i);
		
		std::this_thread::sleep_for(1s);
	}

	//SendEmptyTransaction();

	beam::io::Reactor::get_Current().stop();

	/*Rules::FakePoW = true;
	proto::Config msgCfg = {};
	Rules::get_Hash(msgCfg.m_CfgChecksum);
	msgCfg.m_AutoSendHdr = true;
	Send(msgCfg);*/

	/*proto::NewTip msg;
	msg.m_ID.m_Height = 1000;
	Send(msg);

	proto::Hdr msgHdr;

	Send(msgHdr);*/

	/*proto::GetHdr hdr;
	hdr.m_ID.m_Height = 200;
	Send(hdr);*/
}

void TestNodeConnection::OnClosed(int errorCode)
{
	LOG_ERROR() << "problem with connecting to node: code = " << io::error_str(static_cast<io::ErrorCode>(errorCode));
	io::Reactor::get_Current().stop();
}

bool TestNodeConnection::OnMsg2(proto::NewTip&& msg)
{
	LOG_INFO() << "NewTip is received";
	//beam::io::Reactor::get_Current().stop();
	/*Block::SystemState::Full descr;
	proto::NewTip newMsg;

	descr.get_ID(newMsg.m_ID);*/

	/*msg.m_ID.m_Height += 10;

	Send(msg);*/

	return true;
}

bool TestNodeConnection::OnMsg2(proto::Hdr&& msg)
{
	LOG_INFO() << "Hdr is received";

	//const Height h = msg.m_Description.m_Height;
	//const Height h = 5000;
	const Height h = 200;

	proto::NewTransaction msgTx;

	msgTx.m_Transaction = std::make_shared<Transaction>();

	ECC::Kdf kdf;
	ECC::Scalar::Native key1;
	DeriveKey(key1, kdf, h, KeyType::Coinbase);

	Input::Ptr pInp(new Input);
	pInp->m_Commitment = ECC::Commitment(key1, Rules::CoinbaseEmission);
	msgTx.m_Transaction->m_vInputs.push_back(std::move(pInp));

	Output::Ptr pOut(new Output);
	ECC::Scalar::Native key2;
	
	DeriveKey(key2, kdf, h, KeyType::Regular);
	pOut->Create(key2, Rules::CoinbaseEmission + 5);
	msgTx.m_Transaction->m_vOutputs.push_back(std::move(pOut));

	TxKernel::Ptr pKrn(new TxKernel);
	ECC::Scalar::Native key3;

	pKrn->m_Fee = -1090000;

	DeriveKey(key3, kdf, h, KeyType::Kernel);
	pKrn->m_Excess = ECC::Point::Native(ECC::Context::get().G * key3);

	ECC::Hash::Value hv;
	pKrn->get_HashForSigning(hv);
	pKrn->m_Signature.Sign(hv, key3);

	msgTx.m_Transaction->m_vKernelsOutput.push_back(std::move(pKrn));

	Send(msgTx);

	//m_Hdr = std::move(msg);
	return true;
}

bool TestNodeConnection::OnMsg2(proto::GetHdr&& msg)
{
	LOG_INFO() << "Send header";
	//Send(m_Hdr);
	/*proto::DataMissing msgMiss;
	Send(msgMiss);*/
	return true;
}

bool TestNodeConnection::OnMsg2(proto::Config&& msg)
{
	LOG_INFO() << "Config is received";
	return true;
}

void TestNodeConnection::SendTransaction(Height h)
{
	//const Height h = 200;

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

	LOG_INFO() << "Invalid transaction";
}

void TestNodeConnection::SendEmptyTransaction()
{
	LOG_INFO() << "Send empty transaction";
	proto::NewTransaction msgTx;

	msgTx.m_Transaction = std::make_shared<Transaction>();
	Transaction::Context ctx;
	if (msgTx.m_Transaction->IsValid(ctx))
	{
		LOG_INFO() << "valid transaction";
	}

	Send(msgTx);
}

void ConnectToNode()
{
	LOG_INFO() << "Start new thread";
	io::Reactor::Ptr reactor(io::Reactor::create());
	io::Reactor::Scope scope(*reactor);

	TestNodeConnection connection;

	io::Address addr;
	addr.resolve("127.0.0.1");
	addr.port(10000);

	connection.Connect(addr);

	reactor->run();
}


int main()
{
	int logLevel = LOG_LEVEL_DEBUG;
#if LOG_VERBOSE_ENABLED
	logLevel = LOG_LEVEL_VERBOSE;
#endif
	auto logger = Logger::create(logLevel, logLevel);

	ConnectToNode();
	/*std::vector<std::future<void>> futures;

	for (int i = 0; i < 20; i++)
	{
		futures.push_back(std::async(ConnectToNode));
	}
	
	for (auto &f : futures)
	{
		f.get();
	}*/

	return 0;
}