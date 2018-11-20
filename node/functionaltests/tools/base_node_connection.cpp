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

#include "base_node_connection.h"
#include "utility/logger.h"

#include <boost/program_options.hpp>

#include "wallet/secstring.h"

namespace po = boost::program_options;
using namespace beam;
using namespace ECC;

BaseNodeConnection::BaseNodeConnection(int argc, char* argv[])
{
	ParseCommandLine(argc, argv);
	InitKdf();
}

void BaseNodeConnection::ConnectToNode()
{
	io::Address addr;
	addr.resolve(m_VM["address"].as<std::string>().c_str());
	addr.port(m_VM["port"].as<uint16_t>());

	Connect(addr);
}


void BaseNodeConnection::ParseCommandLine(int argc, char* argv[])
{
	po::options_description options("allowed options");

	options.add_options()
		("address", po::value<std::string>()->default_value("127.0.0.1"), "ip address")
		("port", po::value<uint16_t>()->default_value(10000), "port")
		("wallet_seed", po::value<std::string>()->default_value("321"), "wallet seed");

	po::store(po::command_line_parser(argc, argv).options(options).run(), m_VM);
}

void BaseNodeConnection::InitKdf()
{
	SecString seed(m_VM["wallet_seed"].as<std::string>());
	ECC::HKdf::Create(m_pKdf, seed.hash().V);
}

BaseTestNode::BaseTestNode(int argc, char* argv[])
	: BaseNodeConnection(argc, argv)
	, m_Reactor(io::Reactor::create())
	, m_Scope(*m_Reactor)
	, m_Timer(io::Timer::create(io::Reactor::get_Current()))
	, m_Failed(false)
	, m_Timeout(5 * 1000)
{
}

int BaseTestNode::CheckOnFailed()
{
	return m_Failed;
}

void BaseTestNode::Run()
{
	ConnectToNode();

	m_Reactor->run();
}


void BaseTestNode::OnConnectedSecure()
{
	LOG_INFO() << "connection is succeded";
}

void BaseTestNode::OnDisconnect(const DisconnectReason& reason)
{
	LOG_ERROR() << "problem with connecting to node: code = " << reason;
	m_Failed = true;
	io::Reactor::get_Current().stop();
}

void BaseTestNode::OnMsg(proto::Authentication&& msg)
{
    LOG_INFO() << "proto::Authentication";
    proto::NodeConnection::OnMsg(std::move(msg));

    if (proto::IDType::Node == msg.m_IDType)
		ProveKdfObscured(*m_pKdf, proto::IDType::Owner);

    if (m_Timeout > 0)
    {
        m_Timer->start(m_Timeout, false, [this]()
        {
            LOG_INFO() << "Timeout";
            io::Reactor::get_Current().stop();
            m_Failed = true;
        });
    }

    GenerateTests();
    m_Index = 0;
    RunTest();
}

void BaseTestNode::RunTest()
{
	if (m_Index < m_Tests.size())
		m_Tests[m_Index]();
}

void BaseTestNode::GenerateTests()
{
}
