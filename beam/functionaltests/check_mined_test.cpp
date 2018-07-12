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
	virtual void OnMsg(proto::NewTip&&) override;
	virtual void OnMsg(proto::Hdr&&) override;
	virtual void OnMsg(proto::Mined&&) override;
	virtual void OnMsg(proto::ProofUtxo&&) override;

private:
	bool m_IsInit;
	Block::SystemState::ID m_ID;
	Input m_Input;
	Scalar::Native m_Key;
	Merkle::Hash m_Definition;
};

TestNodeConnection::TestNodeConnection(int argc, char* argv[])
	: BaseTestNodeConnection(argc, argv)
	, m_IsInit(false)
{
	m_Timeout = 10 * 1000;
}

void TestNodeConnection::OnMsg(proto::NewTip&& msg)
{
	if (!m_IsInit)
	{
		LOG_INFO() << "NewTip: " << msg.m_ID;

		m_ID = msg.m_ID;
		m_IsInit = true;		

		LOG_INFO() << "Send GetHdr message";
		Send(proto::GetHdr{ m_ID });
	}
}

void TestNodeConnection::OnMsg(proto::Hdr&& msg)
{
	m_Definition = msg.m_Description.m_Definition;

	LOG_INFO() << "Send GetMined message";
	Send(proto::GetMined{ m_ID.m_Height });
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

	DeriveKey(m_Key, m_Kdf, mined.m_ID.m_Height, KeyType::Coinbase);

	m_Input.m_Commitment = Commitment(m_Key, Rules::get().CoinbaseEmission);

	Send(proto::GetProofUtxo{ m_Input, 0 });
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