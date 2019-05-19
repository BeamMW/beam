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

#pragma once

#include "node/node.h"
#include <boost/program_options.hpp>

class BaseNodeConnection : public beam::proto::NodeConnection
{
public:
	BaseNodeConnection(int argc, char* argv[]);

	void ConnectToNode();

protected:
	void ParseCommandLine(int argc, char* argv[]);
	void InitKdf();

protected:
	beam::Key::IKdf::Ptr m_pKdf;
	boost::program_options::variables_map m_VM;
};

class BaseTestNode : public BaseNodeConnection
{
public:
	BaseTestNode(int argc, char* argv[]);
	void Run();
	int CheckOnFailed();

protected:
	void OnConnectedSecure() override;
	void OnDisconnect(const DisconnectReason&) override;
    void OnMsg(beam::proto::Authentication&&) override;

	virtual void GenerateTests();

	virtual void RunTest();

protected:
	beam::io::Reactor::Ptr m_Reactor;
	beam::io::Reactor::Scope m_Scope;
	beam::io::Timer::Ptr m_Timer;
	bool m_Failed;
	std::vector<std::function<void()>> m_Tests;
	size_t m_Index;
	unsigned int m_Timeout;
};