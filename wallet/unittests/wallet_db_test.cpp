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

void TestStoreTxRecord()
{
    SqliteKeychain keychain;
    beam::HistoryRecord tr;
    Uuid id = {1, 3, 4, 5 ,65};
    tr.m_txId = id;
    tr.m_amount = 34;
    tr.m_initTime = 123456;
    tr.m_flags = HistoryRecord::Flags::Sending;
    tr.m_status = HistoryRecord::Uncompleted;
    WALLET_CHECK_NO_THROW(keychain.insertHistory(tr));
    WALLET_CHECK_THROW(keychain.insertHistory(tr) == false);
    beam::HistoryRecord tr2 = tr;
    tr2.m_txId = id;
    tr2.m_amount = 43;
    tr2.m_initTime = 1234564;
    tr2.m_finishTime = 12345644;
    tr2.m_flags = HistoryRecord::Flags::Sending;
    tr2.m_status = HistoryRecord::Registered;
    WALLET_CHECK_NO_THROW(keychain.updateHistory(tr2));
    
    auto t = keychain.getHistory(0, -1);
    WALLET_CHECK(t.size() == 1);
    WALLET_CHECK(t[0].m_txId == tr.m_txId);
    WALLET_CHECK(t[0].m_amount == tr.m_amount);
    WALLET_CHECK(t[0].m_initTime == tr.m_initTime);
    WALLET_CHECK(t[0].m_finishTime == tr2.m_finishTime);
    WALLET_CHECK(t[0].m_flags == tr2.m_flags); 
    WALLET_CHECK(t[0].m_status == tr2.m_status);
    Uuid id2 = { 3,4,5 };
    WALLET_CHECK_NO_THROW(keychain.deleteHistory(id2));
    WALLET_CHECK_NO_THROW(keychain.deleteHistory(id));
    WALLET_CHECK(keychain.updateHistory(tr2) == false);
    WALLET_CHECK_NO_THROW(keychain.insertHistory(tr2));
    WALLET_CHECK_NO_THROW(keychain.deleteHistory(tr2.m_txId));
    WALLET_CHECK(keychain.getHistory(0, -1).empty());

    for (uint8_t i = 0; i < 100; ++i)
    {
        tr.m_txId[0] = i;
        WALLET_CHECK_NO_THROW(keychain.insertHistory(tr));
    }
    WALLET_CHECK(keychain.getHistory(0, -1).size() == 100);
    t = keychain.getHistory(50, 2);
    WALLET_CHECK(t.size() == 2);
    t = keychain.getHistory(99, 10);
    WALLET_CHECK(t.size() == 1);
    t = keychain.getHistory(9, 0);
    WALLET_CHECK(t.size() == 0);
    t = keychain.getHistory(50, 2);
    id[0] = 50;
    WALLET_CHECK(t[0].m_txId == id);
    id[0] = 51;
    WALLET_CHECK(t[1].m_txId == id);

    t = keychain.getHistory(0, 1);
    WALLET_CHECK(t.size() == 1 && t[0].m_txId[0] == 0);

    t = keychain.getHistory(99, 1);
    WALLET_CHECK(t.size() == 1 && t[0].m_txId[0] == 99);

    t = keychain.getHistory(100, 1);
    WALLET_CHECK(t.size() == 0);
}

int main() 
{

	ECC::InitializeContext();
	TestKeychain();
    TestStoreCoins();
    TestStoreTxRecord();

    return WALLET_CHECK_RESULT;
}
