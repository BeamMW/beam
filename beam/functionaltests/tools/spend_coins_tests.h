#pragma once

#include "beam/node.h"
#include "base_node_connection.h"

class SpendCoinsConnection : public BaseTestNodeConnection
{
public:
	SpendCoinsConnection(int argc, char* argv[]);

	void Start();

protected:
	virtual void OnMsg(beam::proto::NewTip&&) override;
	virtual void OnMsg(beam::proto::Hdr&&) override;
	virtual void OnMsg(beam::proto::ProofUtxo&&) override;
	virtual void OnMsg(beam::proto::Boolean&&) override;

private:
	void StartProcessTx();
	void FinishProcessTx();

protected:
	using NewTransactions = std::vector<beam::proto::NewTransaction>;
	bool m_IsStarted;
	bool m_IsNeedToCheckOutputs;
	NewTransactions m_Transactions;
	beam::Merkle::Hash m_Definition;
	NewTransactions::iterator m_CurrentNewTx;
};