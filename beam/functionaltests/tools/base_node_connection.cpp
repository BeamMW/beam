#include "base_node_connection.h"
#include "utility/logger.h"

#include <boost/program_options.hpp>

namespace po = boost::program_options;
using namespace beam;
using namespace ECC;


Initializer g_Initializer;

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
	NoLeak<uintBig> walletSeed;
	Hash::Value hv;

	Hash::Processor() << m_VM["wallet_seed"].as<std::string>().c_str() >> hv;
	walletSeed.V = hv;

	m_Kdf.m_Secret = walletSeed;
}

BaseTestNode::BaseTestNode(int argc, char* argv[])
	: BaseNodeConnection(argc, argv)
	, m_Reactor(io::Reactor::create())
	, m_Scope(*m_Reactor)
	, m_Timer(io::Timer::create(io::Reactor::get_Current().shared_from_this()))
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


void BaseTestNode::OnConnected()
{
	LOG_INFO() << "connection is succeded";

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

void BaseTestNode::OnDisconnect(const DisconnectReason& reason)
{
	LOG_ERROR() << "problem with connecting to node: code = " << reason;
	m_Failed = true;
	io::Reactor::get_Current().stop();
}

void BaseTestNode::RunTest()
{
	if (m_Index < m_Tests.size())
		m_Tests[m_Index]();
}

void BaseTestNode::GenerateTests()
{	
}