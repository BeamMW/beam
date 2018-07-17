#pragma once

#include "beam/node.h"
#include "base_node_connection.h"

class NewTxConnection : public BaseTestNode
{
public:
	NewTxConnection(int argc, char* argv[]);
protected:
	virtual void OnMsg(beam::proto::Boolean&& msg) override;

protected:
	std::vector<bool> m_Results;
};