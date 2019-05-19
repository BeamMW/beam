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
#include "tools/tx_generator.h"
#include "../core/serialization_adapters.h"

using namespace beam;
using namespace ECC;

int main(int argc, char* argv[])
{
	int logLevel = LOG_LEVEL_DEBUG;
	auto logger = Logger::create(logLevel, logLevel);
	HKdf kdf;

	NoLeak<uintBig> walletSeed;
	Hash::Value hv;

	Hash::Processor() << "321" >> hv;
	walletSeed.V = hv;

	kdf.Generate(walletSeed.V);

	TxGenerator generator(kdf);

	generator.GenerateInputInTx(1, 2);
	//generator.GenerateInputInTx(1, Rules::get().CoinbaseEmission);
	/*generator.GenerateInputInTx(1, Rules::get().CoinbaseEmission);
	generator.GenerateInputInTx(1, Rules::get().CoinbaseEmission);*/
	/*generator.GenerateOutputInTx(2, Rules::get().CoinbaseEmission);*/
	generator.GenerateOutputInTx(2, 1, Key::Type::Regular, false);
	/*generator.GenerateOutputInTx(2, Rules::get().CoinbaseEmission);
	generator.GenerateOutputInTx(2, Rules::get().CoinbaseEmission);*/
	/*generator.GenerateKernel(2, Rules::get().CoinbaseEmission);*/
	generator.GenerateKernel(2, 1);
	/*generator.GenerateKernel(2, Rules::get().CoinbaseEmission);
	generator.GenerateKernel(2, Rules::get().CoinbaseEmission);*/
	generator.Sort();

	SerializerSizeCounter ssc;
	ssc & generator.GetTransaction().m_Transaction;

	LOG_INFO() << "size = " << ssc.m_Counter.m_Value;
}