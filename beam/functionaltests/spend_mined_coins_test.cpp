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
	virtual void GenerateTests() override;
	virtual void OnMsg(proto::NewTip&&) override;
	virtual void OnMsg(proto::Hdr&&) override;
	virtual void OnMsg(proto::ProofUtxo&&) override;
	virtual void OnMsg(proto::Boolean&&) override;

private:
	bool m_IsInit;
	bool m_IsNeedToCheckOut;
	unsigned int m_Counter;
	Block::SystemState::ID m_ID;
	Input m_Input;
	Merkle::Hash m_Definition;
	TxGenerator m_Generator;
};

TestNodeConnection::TestNodeConnection(int argc, char* argv[])
	: BaseTestNodeConnection(argc, argv)
	, m_IsInit(false)
	, m_IsNeedToCheckOut(false)
	, m_Counter(0)
	, m_Generator(m_Kdf)
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

		m_Generator.GenerateInputInTx(m_ID.m_Height - 70, Rules::get().CoinbaseEmission);

		m_Input = *m_Generator.GetTransaction().m_Transaction->m_vInputs.front();
		LOG_INFO() << "Send GetProofUtxo message";
		Send(proto::GetProofUtxo{ m_Input, 0 });
	}

	if (m_IsNeedToCheckOut)
	{
		if (++m_Counter >= 2)
		{
			Send(proto::GetProofUtxo{ *m_Generator.GetTransaction().m_Transaction->m_vOutputs.front(), 0 });
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
					m_Generator.GenerateOutputInTx(m_ID.m_Height + 1, Rules::get().CoinbaseEmission);
					m_Input.m_Commitment = m_Generator.GetTransaction().m_Transaction->m_vOutputs.front()->m_Commitment;
					m_Generator.GenerateKernel(m_ID.m_Height + 5);
					m_Generator.Sort();
					Send(m_Generator.GetTransaction());
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