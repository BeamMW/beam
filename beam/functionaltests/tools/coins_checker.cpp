#include "coins_checker.h"
#include "utility/logger.h"

using namespace beam;
using namespace ECC;

CoinsChecker::CoinsChecker(int argc, char* argv[])
	: BaseNodeConnection(argc, argv)
	, m_IsInitChecker(false)
{
	Rules::get().FakePoW = true;
	Rules::get().UpdateChecksum();
}

void CoinsChecker::Check(const Inputs& inputs, Callback callback)
{
	m_Queue.push_back(std::make_pair(inputs, callback));
	if (m_IsInitChecker && m_Queue.size() == 1)
	{
		StartChecking();
	}	
}

void CoinsChecker::StartChecking()
{
	m_Current = m_Queue.front().first.begin();
	m_IsOk = true;
	Send(proto::GetProofUtxo{ *m_Current, 0 });
}

void CoinsChecker::InitChecker()
{
	ConnectToNode();	
}

void CoinsChecker::OnConnected()
{
	LOG_INFO() << "Send Config to node";

	proto::Config msg;
	msg.m_CfgChecksum = Rules::get().Checksum;
	msg.m_AutoSendHdr = true;
	Send(msg);
}

void CoinsChecker::OnDisconnect(const DisconnectReason& reason)
{
	LOG_ERROR() << "problem with connecting to node: code = " << reason;
	for (const auto& tmp : m_Queue)
	{
		tmp.second(false);
	}
	m_Queue.clear();
}

void CoinsChecker::OnMsg(proto::Hdr&& msg)
{
	m_Definition = msg.m_Description.m_Definition;
	if (!m_IsInitChecker)
	{
		m_IsInitChecker = true;
		if (!m_Queue.empty())
			StartChecking();
	}
}

void CoinsChecker::OnMsg(proto::ProofUtxo&& msg)
{
	if (msg.m_Proofs.empty())
	{
		LOG_INFO() << "Failed: list is empty";
		m_IsOk = false;
	}
	else
	{
		bool isValid = false;
		for (const auto& proof : msg.m_Proofs)
		{
			if (proof.IsValid(*m_Current, m_Definition))
			{
				isValid = true;
				break;
			}
		}

		if (!isValid)
		{
			LOG_INFO() << "Failed: utxo is not valid";
			m_IsOk = false;
		}
	}

	if (++m_Current != m_Queue.front().first.end())
	{
		Send(proto::GetProofUtxo{ *m_Current, 0 });
	}
	else 
	{
		m_Queue.front().second(m_IsOk);
		m_Queue.pop_front();
		if (!m_Queue.empty())
			StartChecking();
	}
}
