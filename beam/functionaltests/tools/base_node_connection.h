#pragma once

#include "beam/node.h"
#include <boost/program_options.hpp>

class BaseTestNodeConnection : public beam::proto::NodeConnection
{
public:
	BaseTestNodeConnection(int argc, char* argv[]);

	void DisabledTimer();

	void Run();

	int CheckOnFailed();
protected:
	void ParseCommandLine(int argc, char* argv[]);
	void InitKdf();

	virtual void OnConnected() override;
	virtual void OnClosed(int errorCode) override;
	virtual bool OnMsg2(beam::proto::Boolean&& msg) override;

	virtual void GenerateTests();

	virtual void RunTest();
	
protected:
	bool m_WillStartTimer = true;
	ECC::Kdf m_Kdf;
	beam::io::Reactor::Ptr m_Reactor;
	beam::io::Reactor::Scope m_Scope;
	beam::io::Timer::Ptr m_Timer;
	bool m_Failed;
	std::vector<std::pair<std::function<void()>, bool>> m_Tests;
	size_t m_Index;
	boost::program_options::variables_map m_VM;	
};