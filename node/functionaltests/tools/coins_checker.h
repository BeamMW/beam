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
	void OnConnectedSecure() override;
    void OnMsg(beam::proto::Authentication&&) override;
	void OnDisconnect(const DisconnectReason&) override;
	void OnMsg(beam::proto::NewTip&&) override;
	void OnMsg(beam::proto::ProofUtxo&&) override;

	void StartChecking();
	

protected:

	bool m_IsInitChecker;
	bool m_IsOk;
	beam::Height m_Maturity;
	beam::Block::SystemState::Full m_Hdr;
	Inputs::const_iterator m_Current;
	std::deque<std::pair<Inputs, Callback>> m_Queue;
};