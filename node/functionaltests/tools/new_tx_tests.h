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

class NewTxConnection : public BaseTestNode
{
public:
	NewTxConnection(int argc, char* argv[]);
protected:
	void OnMsg(beam::proto::Status&& msg) override;

protected:
	std::vector<uint8_t> m_Results;
};