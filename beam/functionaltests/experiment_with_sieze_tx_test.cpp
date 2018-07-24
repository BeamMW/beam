#include "beam/node.h"
#include "utility/logger.h"
#include "tools/tx_generator.h"
#include "../core/serialization_adapters.h"

using namespace beam;
using namespace ECC;

int main(int argc, char* argv[])
{
	int logLevel = LOG_LEVEL_DEBUG;
#if LOG_VERBOSE_ENABLED
	logLevel = LOG_LEVEL_VERBOSE;
#endif
	auto logger = Logger::create(logLevel, logLevel);
	Kdf kdf;

	NoLeak<uintBig> walletSeed;
	Hash::Value hv;

	Hash::Processor() << "321" >> hv;
	walletSeed.V = hv;

	kdf.m_Secret = walletSeed;

	TxGenerator generator(kdf);

	generator.GenerateInputInTx(1, 2);
	//generator.GenerateInputInTx(1, Rules::get().CoinbaseEmission);
	/*generator.GenerateInputInTx(1, Rules::get().CoinbaseEmission);
	generator.GenerateInputInTx(1, Rules::get().CoinbaseEmission);*/
	/*generator.GenerateOutputInTx(2, Rules::get().CoinbaseEmission);*/
	generator.GenerateOutputInTx(2, 1, KeyType::Regular, false);
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