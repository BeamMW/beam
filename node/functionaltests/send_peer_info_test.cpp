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

#include <vector>
#include <thread>
#include <future>

using namespace beam;
using namespace ECC;

class TestNodeConnection : public BaseTestNode
{
public:
	TestNodeConnection(int argc, char* argv[]);
private:
	void OnDisconnect(const DisconnectReason&) override;

	void OnMsg(proto::NewTip&&) override;

	void GenerateTests() override;
};

TestNodeConnection::TestNodeConnection(int argc, char* argv[])
	: BaseTestNode(argc, argv)
{
	m_Timeout = 60 * 1000;
}

void TestNodeConnection::OnDisconnect(const DisconnectReason&)
{
	LOG_INFO() << "Ok: connection is reset";
	io::Reactor::get_Current().stop();
}

void TestNodeConnection::OnMsg(proto::NewTip&&) 
{
	LOG_INFO() << "NewTip";
}

void TestNodeConnection::GenerateTests()
{
	m_Tests.push_back([this]()
	{
		LOG_INFO() << "Send PeerInfo message";

		Hash::Value hv;

		Hash::Processor()
			<< "test"
			>> hv;

		proto::PeerInfo msg;

		msg.m_LastAddr.resolve("8.8.8.8");
		msg.m_ID = hv;
		Send(msg);
	});
}

int main(int argc, char* argv[])
{
	int logLevel = LOG_LEVEL_DEBUG;
	auto logger = Logger::create(logLevel, logLevel);

	TestNodeConnection connection(argc, argv);

	connection.Run();

	return connection.CheckOnFailed();
}