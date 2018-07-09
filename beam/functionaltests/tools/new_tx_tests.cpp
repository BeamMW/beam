#include "new_tx_tests.h"
#include "utility/logger.h"

using namespace beam;
using namespace ECC;

NewTxConnection::NewTxConnection(int argc, char* argv[])
	: BaseTestNodeConnection(argc, argv)
{
}

void NewTxConnection::OnMsg(proto::Boolean&& msg)
{
	if (msg.m_Value != m_Results[m_Index])
	{
		LOG_INFO() << "Failed: node returned " << msg.m_Value;
		m_Failed = true;
	}
	else
	{
		LOG_INFO() << "Ok: node returned " << msg.m_Value;
	}

	++m_Index;

	if (m_Index >= m_Tests.size())
		io::Reactor::get_Current().stop();
	else
		RunTest();
}