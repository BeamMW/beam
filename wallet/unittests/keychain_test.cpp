#include "sqlite_keychain.hpp"
#include <assert.h>
#include "test_helpers.h"

using namespace std;
using namespace ECC;
using namespace beam;

WALLET_TEST_INIT

void TestKeychain()
{
	SqliteKeychain keychain;

	Coin coin1(5, Coin::Unspent, 0, 10);
	keychain.store(coin1);

    WALLET_CHECK(coin1.m_id == 1);

	Coin coin2(2, Coin::Unspent, 0, 10);
	keychain.store(coin2);

    WALLET_CHECK(coin2.m_id == 2);

	auto coins = keychain.getCoins(7);

    WALLET_CHECK(coins.size() == 2);

	{
		vector<Coin> localCoins;
		localCoins.push_back(coin2);
		localCoins.push_back(coin1);

		for (int i = 0; i < coins.size(); ++i)
		{
            WALLET_CHECK(localCoins[i].m_id == coins[i].m_id);
            WALLET_CHECK(localCoins[i].m_amount == coins[i].m_amount);
            WALLET_CHECK(coins[i].m_status == Coin::Locked);
		}
	}

	{
		vector<Coin> coins;
		coin2.m_status = Coin::Spent;
		coins.push_back(coin2);

		keychain.update(coins);

        WALLET_CHECK(keychain.getCoins(5).size() == 0);
	}

	{
		Block::SystemState::ID a;
		Hash::Processor() << static_cast<uint32_t>(rand()) >> a.m_Hash;
		a.m_Height = rand();

		const char* name = "SystemStateID";
		keychain.setVar(name, "dummy");
		keychain.setVar(name, a);

		Block::SystemState::ID b;
        WALLET_CHECK(keychain.getVar(name, b));

		WALLET_CHECK(a == b);
	}
}

void TestStoreCoins()
{
    SqliteKeychain keychain;

  
    Coin coin = { 5, Coin::Unspent, 1, 10, KeyType::Coinbase };
    keychain.store(coin);
    coin = { 4, Coin::Unspent, 1, 10, KeyType::Comission };
    keychain.store(coin);
    coin = { 2, Coin::Unspent, 1, 10, KeyType::Regular };
    keychain.store(coin);
    coin = { 5, Coin::Unspent, 1, 10, KeyType::Coinbase };
    keychain.store(coin);
    coin = { 1, Coin::Unspent, 1, 10, KeyType::Regular };
    keychain.store(coin);
    coin = { 5, Coin::Unspent, 1, 10, KeyType::Coinbase };
    keychain.store(coin);
    coin = { 4, Coin::Unspent, 1, 10, KeyType::Comission };
    keychain.store(coin);
    coin = { 1, Coin::Unspent, 1, 10, KeyType::Regular };
    keychain.store(coin);
    coin = { 4, Coin::Unspent, 1, 10, KeyType::Comission };
    keychain.store(coin);
    coin = { 1, Coin::Unspent, 1, 10, KeyType::Regular };
    keychain.store(coin);
    coin = { 1, Coin::Unspent, 1, 10, KeyType::Regular };
    keychain.store(coin);

    keychain.store(vector<Coin>{
        Coin{ 5, Coin::Unspent, 1, 10, KeyType::Coinbase },
            Coin{ 4, Coin::Unspent, 1, 10, KeyType::Comission },
            Coin{ 2, Coin::Unspent, 1, 10, KeyType::Regular },
            Coin{ 5, Coin::Unspent, 3, 10, KeyType::Coinbase },
            Coin{ 1, Coin::Unspent, 1, 10, KeyType::Regular },
            Coin{ 5, Coin::Unspent, 1, 10, KeyType::Coinbase },
            Coin{ 4, Coin::Unspent, 1, 10, KeyType::Comission },
            Coin{ 1, Coin::Unspent, 1, 10, KeyType::Regular },
            Coin{ 4, Coin::Unspent, 3, 10, KeyType::Comission },
            Coin{ 1, Coin::Unspent, 1, 10, KeyType::Regular },
            Coin{ 1, Coin::Unspent, 1, 10, KeyType::Regular } });


    int coinBase = 0;
    int comission = 0;
    int regular = 0;
    keychain.visit([&coinBase, &comission, &regular](const Coin& coin)->bool
    {
        if (coin.m_key_type == KeyType::Coinbase)
        {
            ++coinBase;
        }
        else if (coin.m_key_type == KeyType::Comission)
        {
            ++comission;
        }
        else if (coin.m_key_type == KeyType::Regular)
        {
            ++regular;
        }
        return true;
    });

    WALLET_CHECK(coinBase == 2);
    WALLET_CHECK(comission == 2);
    WALLET_CHECK(regular == 10);
}

int main() 
{

	ECC::InitializeContext();
	TestKeychain();
    TestStoreCoins();

    return WALLET_CHECK_RESULT;
}
