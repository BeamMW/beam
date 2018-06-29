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
	
	void GenerateInputInTx(beam::Height h, beam::Amount v);
	void GenerateOutputInTx(beam::Height h, beam::Amount v);
	void GenerateKernel(beam::Height h);
	
protected:
	bool m_WillStartTimer = true;
	ECC::Kdf m_Kdf;
	beam::io::Reactor::Ptr m_Reactor;
	beam::io::Reactor::Scope m_Scope;
	beam::io::Timer::Ptr m_Timer;
	bool m_Failed;
	beam::proto::NewTransaction m_MsgTx;
	ECC::Scalar::Native m_Offset;
	std::vector<std::pair<std::function<void()>, bool>> m_Tests;
	int m_Index;
	boost::program_options::variables_map m_VM;	
};