#pragma once

#include "beam/node.h"
#include "base_node_connection.h"

class CoinsChecker : public BaseNodeConnection
{
public:
	using Inputs = std::vector<beam::Input>;
	using Callback = std::function<void(bool, beam::Height)>;
public:
	CoinsChecker(int argc, char* argv[]);
	void InitChecker();
	void Check(const Inputs& inputs, Callback callback);

protected:
	virtual void OnConnected() override;
	virtual void OnDisconnect(const DisconnectReason&) override;
	virtual void OnMsg(beam::proto::Hdr&&) override;
	virtual void OnMsg(beam::proto::ProofUtxo&&) override;

	void StartChecking();
	

protected:

	bool m_IsInitChecker;
	bool m_IsOk;
	beam::Height m_Maturity;
	beam::Merkle::Hash m_Definition;
	Inputs::const_iterator m_Current;
	std::deque<std::pair<Inputs, Callback>> m_Queue;
};