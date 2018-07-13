#include "new_tx_tests.h"
#include "utility/logger.h"

using namespace beam;
using namespace ECC;

SpendCoinsConnection::SpendCoinsConnection(int argc, char* argv[])
	: BaseTestNodeConnection(argc, argv)
	, m_IsStarted(false)
	, m_IsNeedToCheckOutputs(false)
{
	Rules::get().FakePoW = true;
	Rules::get().UpdateChecksum();
}

void SpendCoinsConnection::Start()
{
	m_CurrentNexTx = m_Transactions.begin();

	LOG_INFO() << "Send Config to node";

	proto::Config msg;
	msg.m_CfgChecksum = Rules::get().Checksum;
	msg.m_AutoSendHdr = true;
	Send(msg);
}

void SpendCoinsConnection::OnMsg(proto::NewTip&& msg)
{
	if (m_IsNeedToCheckOutputs)
	{
		m_IsNeedToCheckOutputs = false;
	}
}

void SpendCoinsConnection::OnMsg(proto::Hdr&& msg)
{
	LOG_INFO() << "Hdr: ";

	m_Definition = msg.m_Description.m_Definition;

	if (!m_IsStarted)
	{
		m_IsStarted = true;
	}
}

void SpendCoinsConnection::OnMsg(proto::ProofUtxo&& msg)
{

}

void SpendCoinsConnection::OnMsg(proto::Boolean&& msg)
{
	LOG_INFO() << "Boolean: value = " << msg.m_Value;

	if (!msg.m_Value)
	{
		LOG_INFO() << "Failed:";
		m_Failed = true;
		io::Reactor::get_Current().stop();
		return;
	}
}

void SpendCoinsConnection::StartProcessTx()
{
	m_IsNeedToCheckOutputs = true;
	Send(*m_CurrentNewTx);
}

void SpendCoinsConnection::FinishProcessTx()
{

}