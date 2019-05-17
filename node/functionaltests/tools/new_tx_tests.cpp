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

#include "new_tx_tests.h"
#include "utility/logger.h"

using namespace beam;
using namespace ECC;

NewTxConnection::NewTxConnection(int argc, char* argv[])
	: BaseTestNode(argc, argv)
{
}

void NewTxConnection::OnMsg(proto::Status&& msg)
{
	bool bOk = (proto::TxStatus::Ok == msg.m_Value);
	if (bOk != m_Results[m_Index])
	{
		LOG_INFO() << "Failed: node returned " << static_cast<uint32_t>(msg.m_Value);
		m_Failed = true;
	}
	else
	{
		LOG_INFO() << "Ok: node returned " << static_cast<uint32_t>(msg.m_Value);
	}

	++m_Index;

	if (m_Index >= m_Tests.size())
		io::Reactor::get_Current().stop();
	else
		RunTest();
}