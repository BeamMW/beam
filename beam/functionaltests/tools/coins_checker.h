#pragma once

#include "beam/node.h"
#include "base_node_connection.h"

class CoinsChecker : public BaseTestNode
{
public:
	using Inputs = std::vector<beam::Input>;
	using Callback = std::function<void(bool)>;
public:
	CoinsChecker(int argc, char* argv[]);

protected:
	virtual void OnMsg(beam::proto::Hdr&&) override;
	virtual void OnMsg(beam::proto::ProofUtxo&&) override;

	void Check(const Inputs& inputs, Callback callback);
	void InitChecker();

protected:

	bool m_IsInitChecker;
	bool m_IsOk;
	beam::Merkle::Hash m_Definition;
	Inputs m_Inputs;
	Inputs::const_iterator m_Current;
	Callback m_Callback;
};