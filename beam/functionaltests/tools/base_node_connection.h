#pragma once

#include "beam/node.h"
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
	ECC::Kdf m_Kdf;
	boost::program_options::variables_map m_VM;
};

class BaseTestNode : public BaseNodeConnection
{
public:
	BaseTestNode(int argc, char* argv[]);
	void Run();
	int CheckOnFailed();

protected:
	virtual void OnConnected() override;
	virtual void OnDisconnect(const DisconnectReason&) override;

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