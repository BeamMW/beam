#include "sqlite_keychain.hpp"
#include <assert.h>

void TestKeychain()
{
	SqliteKeychain keychain;


	beam::Coin coin1(5);
	keychain.store(coin1);

	assert(coin1.m_id == 1);

	beam::Coin coin2(2);
	keychain.store(coin2);

	assert(coin2.m_id == 2);

	auto coins = keychain.getCoins(7);

	assert(coins.size() == 2);

	{
		std::vector<beam::Coin> localCoins;
		localCoins.push_back(coin2);
		localCoins.push_back(coin1);

		for (int i = 0; i < coins.size(); ++i)
		{
			assert(localCoins[i].m_id == coins[i].m_id);
			assert(localCoins[i].m_amount == coins[i].m_amount);
			assert(coins[i].m_status == beam::Coin::Locked);
		}
	}

	{
		std::vector<beam::Coin> coins;
		coin2.m_status = beam::Coin::Spent;
		coins.push_back(coin2);

		keychain.update(coins);

		assert(keychain.getCoins(5).size() == 0);
	}

	{
		beam::Block::SystemState::ID a;
		ECC::Hash::Processor() << static_cast<uint32_t>(rand()) >> a.m_Hash;
		a.m_Height = rand();

		const char* name = "SystemStateID";
		keychain.setVar(name, "dummy");
		keychain.setVar(name, a);

		beam::Block::SystemState::ID b;
		assert(keychain.getVar(name, b));

		assert(a == b);
	}
}

int main() {

	TestKeychain();
}
