#include "coins_checker.h"
#include "utility/logger.h"

using namespace beam;
using namespace ECC;

CoinsChecker::CoinsChecker(int argc, char* argv[])
	: BaseTestNodeConnection(argc, argv)
	, m_IsInitChecker(false)
{
	Rules::get().FakePoW = true;
	Rules::get().UpdateChecksum();
}

void CoinsChecker::Check(const Inputs& inputs)
{
	m_Inputs = inputs;
	m_Current = m_Inputs.begin();
	m_IsOk = true;
	Send(proto::GetProofUtxo{*m_Current, 0});
}

void CoinsChecker::OnFinishCheck(bool isOk)
{
}

void CoinsChecker::InitChecker()
{
	LOG_INFO() << "Send Config to node";

	proto::Config msg;
	msg.m_CfgChecksum = Rules::get().Checksum;
	msg.m_AutoSendHdr = true;
	Send(msg);
}

void CoinsChecker::OnMsg(proto::Hdr&& msg)
{
	LOG_INFO() << "Hdr: ";

	m_Definition = msg.m_Description.m_Definition;
	m_IsInitChecker = true;
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
				LOG_INFO() << "OK: utxo is valid";
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

	if (++m_Current != m_Inputs.end())
	{
		Send(proto::GetProofUtxo{ *m_Current, 0 });
	}
	else 
	{
		OnFinishCheck(m_IsOk);
	}
}
