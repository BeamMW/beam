#include "beam/node.h"
#include "utility/logger.h"
#include "tools/base_node_connection.h"

using namespace beam;
using namespace ECC;

class TestNodeConnection : public BaseTestNodeConnection
{
public:
	TestNodeConnection(int argc, char* argv[]);
private:
	virtual void GenerateTests() override;
	virtual void OnMsg(proto::NewTip&&) override;
	virtual void OnMsg(proto::Hdr&&) override;
	virtual void OnMsg(proto::ProofUtxo&&) override;
	virtual void OnMsg(proto::Boolean&&) override;

	void SendTx();

private:
	bool m_IsInit;
	bool m_IsNeedToCheckOut;
	unsigned int m_Counter;
	Block::SystemState::ID m_ID;
	Input m_Input;
	Scalar::Native m_InKey;
	Output m_Output;
	Scalar::Native m_OutKey;
	Merkle::Hash m_Definition;
};

TestNodeConnection::TestNodeConnection(int argc, char* argv[])
	: BaseTestNodeConnection(argc, argv)
	, m_IsInit(false)
	, m_IsNeedToCheckOut(false)
	, m_Counter(0)
{
	m_Timeout = 5 * 60 * 1000;

	Rules::get().FakePoW = true;
	Rules::get().UpdateChecksum();
}

void TestNodeConnection::GenerateTests()
{
	m_Tests.push_back([this] 
	{
		LOG_INFO() << "Send Config to node";

		proto::Config msg;
		msg.m_CfgChecksum = Rules::get().Checksum;
		msg.m_AutoSendHdr = true;
		Send(msg);
	});
}

void TestNodeConnection::OnMsg(proto::NewTip&& msg)
{
	LOG_INFO() << "NewTip: " << msg.m_ID;

	if (!m_IsInit)
	{
		m_ID = msg.m_ID;
		m_IsInit = true;

		DeriveKey(m_InKey, m_Kdf, m_ID.m_Height - 70, KeyType::Coinbase);

		m_Input.m_Commitment = Commitment(m_InKey, Rules::get().CoinbaseEmission);

		LOG_INFO() << "Send GetProofUtxo message";
		m_Timer->start(1 * 1000, false, [this] {Send(proto::GetProofUtxo{ m_Input, 0 }); });
		//Send(proto::GetProofUtxo{ m_Input, 0 });
	}

	if (m_IsNeedToCheckOut)
	{
		if (++m_Counter >= 4)
		{
			m_Input.m_Commitment = m_Output.m_Commitment;
			Send(proto::GetProofUtxo{ m_Input, 0 });
		}
	}
}

void TestNodeConnection::OnMsg(proto::Hdr&& msg)
{
	LOG_INFO() << "Hdr: ";
	
	m_Definition = msg.m_Description.m_Definition;	
}

void TestNodeConnection::OnMsg(proto::ProofUtxo&& msg)
{
	if (msg.m_Proofs.empty())
	{
		LOG_INFO() << "Failed: list is empty";
		m_Failed = true;
	}
	else
	{
		bool isValid = false;
		for (const auto& proof : msg.m_Proofs)
		{
			if (proof.IsValid(m_Input, m_Definition))
			{
				LOG_INFO() << "OK: utxo is valid";
				isValid = true;
				if (!m_IsNeedToCheckOut)
				{
					SendTx();
					return;
				}
				break;
			}
		}

		if (!isValid)
		{
			LOG_INFO() << "Failed: utxo is not valid";
			m_Failed = true;			
		}
	}

	io::Reactor::get_Current().stop();
}

void TestNodeConnection::OnMsg(proto::Boolean&& msg)
{
	LOG_INFO() << "Boolean: value = " << msg.m_Value;

	if (!msg.m_Value)
	{
		LOG_INFO() << "Failed: tx is invalid";
		m_Failed = true;
		io::Reactor::get_Current().stop();

		return;
	}

	m_IsNeedToCheckOut = true;
}

void TestNodeConnection::SendTx()
{
	DeriveKey(m_OutKey, m_Kdf, m_ID.m_Height + 1, KeyType::Regular);
	m_Output.Create(m_OutKey, Rules::get().CoinbaseEmission/* - 10*/);

	proto::NewTransaction msg;
	msg.m_Transaction = std::make_shared<Transaction>();

	msg.m_Transaction->m_vInputs.push_back(Input::Ptr(new Input(m_Input)));
	Output::Ptr out(new Output);
	*out = m_Output;
	msg.m_Transaction->m_vOutputs.push_back(std::move(out));

	TxKernel::Ptr pKrn(new TxKernel);
	Scalar::Native key;

	DeriveKey(key, m_Kdf, m_ID.m_Height + 5, KeyType::Kernel);
	pKrn->m_Excess = Point::Native(ECC::Context::get().G * key);

	//pKrn->m_Fee = 10;
	ECC::Hash::Value hv;
	pKrn->get_HashForSigning(hv);
	pKrn->m_Signature.Sign(hv, key);
	msg.m_Transaction->m_vKernelsOutput.push_back(std::move(pKrn));

	Scalar::Native offset = m_InKey + (-m_OutKey);
	offset += -key;
	msg.m_Transaction->m_Offset = offset;

	Send(msg);
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