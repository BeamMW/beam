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

#include "node/node.h"
#include "utility/logger.h"
#include "tools/base_node_connection.h"

using namespace beam;

class TestNodeConnection : public BaseTestNode
{
public:
	TestNodeConnection(int argc, char* argv[]);
private:
	void OnMsg(proto::NewTip&&) override;
	void OnMsg(proto::Mined&&) override;
	
private:
	bool m_IsInit;
	Block::SystemState::ID m_ID;
	bool m_IsSendWrongMsg;
    unsigned m_Counter;
};

TestNodeConnection::TestNodeConnection(int argc, char* argv[])
	: BaseTestNode(argc, argv)
	, m_IsInit(false)
	, m_IsSendWrongMsg(false)
    , m_Counter(0)
{
	m_Timeout = 30 * 1000;
}

void TestNodeConnection::OnMsg(proto::NewTip&& msg)
{
    if (++m_Counter < 2)
	{
		return;
	}

	if (!m_IsInit)
	{
		msg.m_Description.get_ID(m_ID);
		m_IsInit = true;

		LOG_INFO() << "NewTip: " << m_ID;

		m_IsSendWrongMsg = true;

		LOG_INFO() << "Send wrong GetMined message";
		Send(proto::GetMined{ m_ID.m_Height + 5 });
	}
}

void TestNodeConnection::OnMsg(proto::Mined&& msg)
{
	LOG_INFO() << "Mined";
	if (m_IsSendWrongMsg)
	{
		if (msg.m_Entries.empty())
		{
			LOG_INFO() << "Ok: received empty list";
		}
		else
		{
			LOG_INFO() << "Failed: list is not empty";
			m_Failed = true;
			io::Reactor::get_Current().stop();
			return;
		}
				
		m_IsSendWrongMsg = false;

		LOG_INFO() << "Send GetMined message";
        proto::GetMined msgOut;
        msgOut.m_HeightMin = m_ID.m_Height - 1000;
		Send(msgOut);

		return;
	}

	if (!msg.m_Entries.empty())
	{
		LOG_INFO() << "Ok: list is not empty";
	}
	else
	{
		LOG_INFO() << "Failed: list is empty";
		m_Failed = true;
	}
	
	io::Reactor::get_Current().stop();
}

int main(int argc, char* argv[])
{
	int logLevel = LOG_LEVEL_DEBUG;
	auto logger = Logger::create(logLevel, logLevel);

	TestNodeConnection connection(argc, argv);

	connection.Run();

	return connection.CheckOnFailed();
}