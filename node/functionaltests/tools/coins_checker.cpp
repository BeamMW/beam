// Copyright 2018 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

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
	m_Maturity = 0;
	Send(proto::GetProofUtxo{ *m_Current, 0 });
}

void CoinsChecker::InitChecker()
{
	ConnectToNode();
}

void CoinsChecker::OnConnectedSecure()
{
	proto::Login msg;
	msg.m_CfgChecksum = Rules::get().Checksum;
	Send(msg);
}

void CoinsChecker::OnMsg(proto::Authentication&& msg)
{
    proto::NodeConnection::OnMsg(std::move(msg));

    if (proto::IDType::Node == msg.m_IDType)
		ProveKdfObscured(*m_pKdf, proto::IDType::Owner);
}

void CoinsChecker::OnDisconnect(const DisconnectReason& reason)
{
	LOG_ERROR() << "problem with connecting to node: code = " << reason;
	for (const auto& tmp : m_Queue)
	{
		tmp.second(false, m_Maturity);
	}
	m_Queue.clear();
}

void CoinsChecker::OnMsg(proto::NewTip&& msg)
{
	m_Hdr = msg.m_Description;
	if (!m_IsInitChecker)
	{
		m_IsInitChecker = true;
		if (!m_Queue.empty())
			StartChecking();
	}
}

void CoinsChecker::OnMsg(proto::ProofUtxo&& msg)
{
	if (m_Current == m_Queue.front().first.end())
	{
		LOG_INFO() << "reaceived ProofUtxo twice";
	}

	if (msg.m_Proofs.empty())
	{
		m_IsOk = false;
	}
	else
	{
		bool isValid = false;
		for (const auto& proof : msg.m_Proofs)
		{
			if (m_Hdr.IsValidProofUtxo(*m_Current, proof))
			{
				m_Maturity = std::max(m_Maturity, proof.m_State.m_Maturity);
				isValid = true;
				break;
			}
		}

		if (!isValid)
		{
			m_IsOk = false;
		}
	}

	if (m_IsOk && ++m_Current != m_Queue.front().first.end())
	{
		Send(proto::GetProofUtxo{ *m_Current, 0 });
	}
	else 
	{
		m_Queue.front().second(m_IsOk, m_Maturity);
		m_Queue.pop_front();
		if (!m_Queue.empty())
			StartChecking();
	}
}
