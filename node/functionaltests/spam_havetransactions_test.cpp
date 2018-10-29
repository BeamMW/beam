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

#include "node/node.h"
#include "utility/logger.h"
#include "tools/base_node_connection.h"
#include "tools/tx_generator.h"
#include "tools/new_tx_tests.h"

#include <vector>
#include <thread>
#include <future>

using namespace beam;
using namespace ECC;

void GenerateRandom(void* p, uint32_t n)
{
	for (uint32_t i = 0; i < n; i++)
		((uint8_t*)p)[i] = (uint8_t)rand();
}

void SetRandom(uintBig& x)
{
	GenerateRandom(x.m_pData, x.nBytes);
}

void SetRandom(Scalar::Native& x)
{
	Scalar s;
	while (true)
	{
		SetRandom(s.m_Value);
		if (!x.Import(s))
			break;
	}
}

class TestNodeConnection : public BaseTestNode
{
public:
	TestNodeConnection(int argc, char* argv[], int h);	

private:
	void GenerateTests() override;

private:
	int m_Ind;	
};

TestNodeConnection::TestNodeConnection(int argc, char* argv[], int h)
	: BaseTestNode(argc, argv)
	, m_Ind(h)
{
	m_Timeout = 0;
}


void TestNodeConnection::GenerateTests()
{
	m_Tests.push_back([this]()
	{
		m_Timer->start(10, true, [this]
		{
			LOG_INFO() << "Send HaveTransaction: ind = " << m_Ind;
			Transaction::KeyType id;

			SetRandom(id);
			Send(proto::HaveTransaction{ id });
		});		
	});
}

void SendData(int argc, char* argv[], int h)
{
	TestNodeConnection connection(argc, argv, h);

	connection.Run();
}

int main(int argc, char* argv[])
{
	int logLevel = LOG_LEVEL_DEBUG;
	auto logger = Logger::create(logLevel, logLevel);
	//SendData(argc, argv, 1);
	std::vector<std::future<void>> futures;

	for (int i = 0; i < 1000; i++)
	{
		futures.push_back(std::async(SendData, argc, argv, i));
	}

	for (auto &f : futures)
	{
		f.get();
	}
}