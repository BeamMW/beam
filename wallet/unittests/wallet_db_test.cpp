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

#include "wallet/wallet_db.h"
#include <assert.h>
#include "test_helpers.h"
#include "core/storage.h"

#include "utility/logger.h"
#include <boost/filesystem.hpp>
#include <algorithm>

using namespace std;
using namespace ECC;
using namespace beam;

WALLET_TEST_INIT

namespace
{
    IKeyChain::Ptr createSqliteKeychain()
    {
        const char* dbName = "wallet.db";
        if (boost::filesystem::exists(dbName))
        {
            boost::filesystem::remove(dbName);
        }
        ECC::NoLeak<ECC::uintBig> seed;
        seed.V = ECC::Zero;
        auto keychain = Keychain::init(dbName, "pass123", seed);
        beam::Block::SystemState::ID id = { };
        id.m_Height = 134;
        keychain->setSystemStateID(id);
        return keychain;
    }
}

void TestKeychain()
{
	auto keychain = createSqliteKeychain();

	Coin coin1(5, Coin::Unspent, 0, 10);
	keychain->store(coin1);

    WALLET_CHECK(coin1.m_id == 1);

	Coin coin2(2, Coin::Unspent, 0, 10);
	keychain->store(coin2);

    WALLET_CHECK(coin2.m_id == 2);
    
    {
	    auto coins = keychain->selectCoins(7);
        WALLET_CHECK(coins.size() == 2);

	
		vector<Coin> localCoins;
		localCoins.push_back(coin2);
		localCoins.push_back(coin1);

		for (size_t i = 0; i < coins.size(); ++i)
		{
            WALLET_CHECK(localCoins[i].m_id == coins[i].m_id);
            WALLET_CHECK(localCoins[i].m_amount == coins[i].m_amount);
            WALLET_CHECK(coins[i].m_status == Coin::Locked);
		}
	}

	{
		coin2.m_status = Coin::Spent;
		

		keychain->update(coin2);

        WALLET_CHECK(keychain->selectCoins(5).size() == 0);
	}

	{
		Block::SystemState::ID a;
		Hash::Processor() << static_cast<uint32_t>(rand()) >> a.m_Hash;
		a.m_Height = rand();

		const char* name = "SystemStateID";
		keychain->setVar(name, "dummy");
		keychain->setVar(name, a);

		Block::SystemState::ID b;
        WALLET_CHECK(keychain->getVar(name, b));

		WALLET_CHECK(a == b);
	}
}

void TestStoreCoins()
{
    auto keychain = createSqliteKeychain();

  
    Coin coin = { 5, Coin::Unspent, 1, 10, KeyType::Coinbase };
    keychain->store(coin);
    coin = { 4, Coin::Unspent, 1, 10, KeyType::Comission };
    keychain->store(coin);
    coin = { 2, Coin::Unspent, 1, 10, KeyType::Regular };
    keychain->store(coin);
    coin = { 5, Coin::Unspent, 1, 10, KeyType::Coinbase };
    keychain->store(coin);
    coin = { 1, Coin::Unspent, 1, 10, KeyType::Regular };
    keychain->store(coin);
    coin = { 5, Coin::Unspent, 1, 10, KeyType::Coinbase };
    keychain->store(coin);
    coin = { 4, Coin::Unspent, 1, 10, KeyType::Comission };
    keychain->store(coin);
    coin = { 1, Coin::Unspent, 1, 10, KeyType::Regular };
    keychain->store(coin);
    coin = { 4, Coin::Unspent, 1, 10, KeyType::Comission };
    keychain->store(coin);
    coin = { 1, Coin::Unspent, 1, 10, KeyType::Regular };
    keychain->store(coin);
    coin = { 1, Coin::Unspent, 1, 10, KeyType::Regular };
    keychain->store(coin);

    auto coins = vector<Coin>{
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
            Coin{ 1, Coin::Unspent, 1, 10, KeyType::Regular } };
    keychain->store(coins);


    int coinBase = 0;
    int comission = 0;
    int regular = 0;
    keychain->visit([&coinBase, &comission, &regular](const Coin& coin)->bool
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

    coins.clear();
    keychain->visit([&coins](const auto& coin)->bool
    {
        coins.push_back(coin);
        return false;
    });
    WALLET_CHECK(coins[0].m_confirmHash == Zero);
    WALLET_CHECK(coins[0].m_confirmHeight == MaxHeight);
    coins[0].m_confirmHeight = 423;
    coins[0].m_confirmHash = 12345678U;
    keychain->update(coins[0]);
    coins.clear();
    keychain->visit([&coins](const auto& coin)->bool
    {
        coins.push_back(coin);
        return false;
    });
    beam::Merkle::Hash t;
    t = 12345678U;
    WALLET_CHECK(coins[0].m_confirmHash == t);
    WALLET_CHECK(coins[0].m_confirmHeight == 423);
}
using namespace beam;
using namespace beam::wallet;
void TestStoreTxRecord()
{
    auto keychain = createSqliteKeychain();
    TxID id = {{1, 3, 4, 5 ,65}};
    TxDescription tr;
    tr.m_txId = id;
    tr.m_amount = 34;
    tr.m_peerId = unsigned(23);
    tr.m_myId = unsigned(42);
    tr.m_createTime = 123456;
    tr.m_minHeight = 134;
    tr.m_sender = true;
    tr.m_status = TxDescription::InProgress;
	tr.m_change = 5;

    WALLET_CHECK_NO_THROW(keychain->saveTx(tr));
    WALLET_CHECK_NO_THROW(keychain->saveTx(tr));
    TxDescription tr2 = tr;
    tr2.m_txId = id;
    tr2.m_amount = 43;
    tr2.m_minHeight = 234;
    tr2.m_createTime = 1234564;
    tr2.m_modifyTime = 12345644;
    tr2.m_status = TxDescription::Completed;
	tr2.m_change = 5;
    WALLET_CHECK_NO_THROW(keychain->saveTx(tr2));
    
    auto t = keychain->getTxHistory();
    WALLET_CHECK(t.size() == 1);
    WALLET_CHECK(t[0].m_txId == tr.m_txId);
    WALLET_CHECK(t[0].m_amount == tr.m_amount);
    WALLET_CHECK(t[0].m_minHeight == tr2.m_minHeight);
    WALLET_CHECK(t[0].m_peerId == tr.m_peerId);
    WALLET_CHECK(t[0].m_myId == tr.m_myId);
    WALLET_CHECK(t[0].m_createTime == tr.m_createTime);
    WALLET_CHECK(t[0].m_modifyTime == tr2.m_modifyTime);
    WALLET_CHECK(t[0].m_sender == tr2.m_sender);
	WALLET_CHECK(t[0].m_status == tr2.m_status);
    WALLET_CHECK(t[0].m_change == tr2.m_change);
    TxID id2 = {{ 3,4,5 }};
    WALLET_CHECK_NO_THROW(keychain->deleteTx(id2));
    WALLET_CHECK_NO_THROW(keychain->deleteTx(id));

    WALLET_CHECK_NO_THROW(keychain->saveTx(tr2));
    WALLET_CHECK_NO_THROW(keychain->saveTx(tr2));
    boost::optional<TxDescription> tr3;
    WALLET_CHECK_NO_THROW(tr3 = keychain->getTx(tr2.m_txId));
    WALLET_CHECK(tr3.is_initialized());
    WALLET_CHECK(tr3->m_txId == tr2.m_txId);
    WALLET_CHECK(tr3->m_amount == tr2.m_amount);
    WALLET_CHECK(tr3->m_peerId == tr2.m_peerId);
    WALLET_CHECK(tr3->m_myId == tr2.m_myId);
    WALLET_CHECK(tr3->m_message == tr2.m_message);
    WALLET_CHECK(tr3->m_createTime == tr2.m_createTime);
    WALLET_CHECK(tr3->m_modifyTime == tr2.m_modifyTime);
    WALLET_CHECK(tr3->m_sender == tr2.m_sender);
    WALLET_CHECK(tr3->m_status == tr2.m_status);
    WALLET_CHECK(tr3->m_fsmState == tr2.m_fsmState);
    WALLET_CHECK_NO_THROW(keychain->deleteTx(tr2.m_txId));
    WALLET_CHECK(keychain->getTxHistory().empty());

    for (uint8_t i = 0; i < 100; ++i)
    {
        tr.m_txId[0] = i;
        WALLET_CHECK_NO_THROW(keychain->saveTx(tr));
    }
    WALLET_CHECK(keychain->getTxHistory().size() == 100);
    t = keychain->getTxHistory(50, 2);
    WALLET_CHECK(t.size() == 2);
    t = keychain->getTxHistory(99, 10);
    WALLET_CHECK(t.size() == 1);
    t = keychain->getTxHistory(9, 0);
    WALLET_CHECK(t.size() == 0);
    t = keychain->getTxHistory(50, 2);
    id[0] = 50;
    WALLET_CHECK(t[0].m_txId == id);
    id[0] = 51;
    WALLET_CHECK(t[1].m_txId == id);

    t = keychain->getTxHistory(0, 1);
    WALLET_CHECK(t.size() == 1 && t[0].m_txId[0] == 0);

    t = keychain->getTxHistory(99, 1);
    WALLET_CHECK(t.size() == 1 && t[0].m_txId[0] == 99);

    t = keychain->getTxHistory(100, 1);
    WALLET_CHECK(t.size() == 0);
}

void TestRollback()
{
    auto db = createSqliteKeychain();
    for (uint64_t i = 0; i < 9; ++i)
    {
        Coin coin1 = { 5, Coin::Unspent, i, i + 10, KeyType::Regular, Height(i) };
        db->store(coin1);
    }

    for (uint64_t i = 9; i < 10; ++i)
    {
        Coin coin1 = { 5, Coin::Spent, 0, 0, KeyType::Regular, Height(0), Height(i) };
        db->store(coin1);
    }

    db->rollbackConfirmedUtxo(5);

    vector<Coin> coins;
    db->visit([&coins](const auto& c)->bool
    {
        coins.push_back(c);
        return true;
    });

    for (int i = 0; i < 5; ++i)
    {
        auto& c = coins[i];
        WALLET_CHECK(c.m_status == Coin::Unspent);
        WALLET_CHECK(c.m_confirmHeight != MaxHeight);
        WALLET_CHECK(c.m_lockedHeight == MaxHeight);
    }

    for (int i = 6; i < 9; ++i)
    {
        auto& c = coins[i];
        WALLET_CHECK(c.m_status == Coin::Unconfirmed);
        WALLET_CHECK(c.m_confirmHeight == MaxHeight);
        WALLET_CHECK(c.m_lockedHeight == MaxHeight);
    }
    for (int i = 9; i < 10; ++i)
    {
        auto& c = coins[i];
        WALLET_CHECK(c.m_status == Coin::Unspent);
        WALLET_CHECK(c.m_confirmHeight != MaxHeight);
        WALLET_CHECK(c.m_lockedHeight == MaxHeight);
    }
}

void TestTxRollback()
{
    auto keychain = createSqliteKeychain();
    TxID id = { { 1, 3, 4, 5 ,65 } };
    TxID id2 = { {1, 3, 4} };

    Coin coin1 = { 5, Coin::Unspent, 1, 10, KeyType::Coinbase };
    keychain->store(coin1);
    Coin coin2 = { 6, Coin::Unspent, 2, 11, KeyType::Coinbase };
    keychain->store(coin2);
    
    coin2.m_spentTxId = id;
    coin2.m_status = Coin::Locked;
    
    Coin coin3 = { 3, Coin::Unconfirmed, 2 };
    coin3.m_createTxId = id;
    keychain->store(coin3);
    keychain->update({ coin2 });

    vector<Coin> coins;
    keychain->visit([&coins](const Coin& c)->bool
    {
        coins.push_back(c);
        return true;
    });

    WALLET_CHECK(coins.size() == 3);

    WALLET_CHECK(coins[1].m_status == Coin::Locked);
    WALLET_CHECK(coins[1].m_spentTxId == id);
    WALLET_CHECK(coins[2].m_status == Coin::Unconfirmed);
    WALLET_CHECK(coins[2].m_createTxId == id);

    keychain->rollbackTx(id2);

    coins.clear();
    keychain->visit([&coins](const Coin& c)->bool
    {
        coins.push_back(c);
        return true;
    });
    WALLET_CHECK(coins.size() == 3);

    keychain->rollbackTx(id);

    coins.clear();
    keychain->visit([&coins](const Coin& c)->bool
    {
        coins.push_back(c);
        return true;
    });

    WALLET_CHECK(coins.size() == 2);
    WALLET_CHECK(coins[1].m_status == Coin::Unspent);
    WALLET_CHECK(coins[1].m_spentTxId.is_initialized() == false);
}

void TestPeers()
{
    auto db = createSqliteKeychain();
    auto peers = db->getPeers();
    WALLET_CHECK(peers.empty());
    TxPeer peer = {};
    peer.m_walletID = unsigned(1234567890);
    peer.m_label = u8"test peer";
    auto p = db->getPeer(peer.m_walletID);
    WALLET_CHECK(p.is_initialized() == false);
    peer.m_address = "92.123.10.3:3255";
    db->addPeer(peer);
    p = db->getPeer(peer.m_walletID);
    WALLET_CHECK(p.is_initialized() == true);
    WALLET_CHECK(p->m_address == peer.m_address);
    WALLET_CHECK(p->m_walletID == peer.m_walletID);
    WALLET_CHECK(p->m_label == peer.m_label);
    peers = db->getPeers();
    WALLET_CHECK(peers.size() == 1);

    WALLET_CHECK(peers[0].m_address == peer.m_address);
    WALLET_CHECK(peers[0].m_walletID == peer.m_walletID);
    WALLET_CHECK(peers[0].m_label == peer.m_label);
}

void TestAddresses()
{
    auto db = createSqliteKeychain();
    auto addresses = db->getAddresses(true);
    WALLET_CHECK(addresses.empty());
    addresses = db->getAddresses(false);
    WALLET_CHECK(addresses.empty());

    WalletAddress a = {};
    a.m_walletID = unsigned(9876543);
    a.m_label = "test label";
    a.m_category = "test category";
    a.m_createTime = beam::getTimestamp();
    a.m_duration = 23;
    a.m_own = true;

    db->saveAddress(a);

    addresses = db->getAddresses(true);
    WALLET_CHECK(addresses.size() == 1);
    WALLET_CHECK(addresses[0].m_walletID == a.m_walletID);
    WALLET_CHECK(addresses[0].m_label == a.m_label);
    WALLET_CHECK(addresses[0].m_category == a.m_category);
    WALLET_CHECK(addresses[0].m_createTime == a.m_createTime);
    WALLET_CHECK(addresses[0].m_duration == a.m_duration);
    WALLET_CHECK(addresses[0].m_own == a.m_own);

    a.m_category = "cat2";

    db->saveAddress(a);

    addresses = db->getAddresses(true);
    WALLET_CHECK(addresses.size() == 1);
    WALLET_CHECK(addresses[0].m_walletID == a.m_walletID);
    WALLET_CHECK(addresses[0].m_label == a.m_label);
    WALLET_CHECK(addresses[0].m_category == a.m_category);
    WALLET_CHECK(addresses[0].m_createTime == a.m_createTime);
    WALLET_CHECK(addresses[0].m_duration == a.m_duration);
    WALLET_CHECK(addresses[0].m_own == a.m_own);


    db->deleteAddress(a.m_walletID);
    WALLET_CHECK(db->getAddresses(true).empty());

}

void TestSelect()
{
    auto db = createSqliteKeychain();
    const Amount c = 10;
    for (Amount i = 1; i <= c; ++i)
    {
        Coin c{ i, Coin::Unspent, 1, 10, KeyType::Regular };
        db->store(c);
    }
    for (Amount i = 1; i <= c; ++i)
    {
        auto coins = db->selectCoins(i, false);
        WALLET_CHECK(coins.size() == 1);
        WALLET_CHECK(coins[0].m_amount == i);
    }
    {
        auto coins = db->selectCoins(56, false);
        WALLET_CHECK(coins.empty());
    }
    {
        auto coins = db->selectCoins(55, false);
        WALLET_CHECK(coins.size() == 10);
    }
    {
        auto coins = db->selectCoins(15, false);
        WALLET_CHECK(coins.size() == 2);
    }
    for (Amount i = c + 1; i <= 55; ++i)
    {
        auto coins = db->selectCoins(i, false);
        WALLET_CHECK(!coins.empty());
    }
    // double coins
    for (Amount i = 1; i <= c; ++i)
    {
        Coin c{ i, Coin::Unspent, 1, 10, KeyType::Regular };
        db->store(c);
    }
    for (Amount i = 1; i <= c; ++i)
    {
        auto coins = db->selectCoins(i, false);
        WALLET_CHECK(coins.size() == 1);
        WALLET_CHECK(coins[0].m_amount == i);
    }
    {
        auto coins = db->selectCoins(111, false);
        WALLET_CHECK(coins.empty());
    }
    {
        auto coins = db->selectCoins(110, false);
        WALLET_CHECK(coins.size() == 20);
    }
    for (Amount i = c + 1; i <= 110; ++i)
    {
        auto coins = db->selectCoins(i, false);
        WALLET_CHECK(!coins.empty());
        auto sum = accumulate(coins.begin(), coins.end(), Amount(0), [](const auto& left, const auto& right) {return left + right.m_amount; });
        WALLET_CHECK(sum == i);
    }

    {
        db->remove(db->selectCoins(110, false));
        vector<Coin> coins = {
            Coin{ 2, Coin::Unspent, 1, 10, KeyType::Regular },
            Coin{ 1, Coin::Unspent, 1, 10, KeyType::Regular },
            Coin{ 9, Coin::Unspent, 1, 10, KeyType::Regular } };
        db->store(coins);
        coins = db->selectCoins(6, false);
        WALLET_CHECK(coins.size() == 1);
        WALLET_CHECK(coins[0].m_amount == 9);
    }
    {
        db->remove(db->selectCoins(12, false));
        vector<Coin> coins = {
            Coin{ 2, Coin::Unspent, 1, 10, KeyType::Regular },
            Coin{ 4, Coin::Unspent, 1, 10, KeyType::Regular },
            Coin{ 4, Coin::Unspent, 1, 10, KeyType::Regular },
            Coin{ 4, Coin::Unspent, 1, 10, KeyType::Regular },
            Coin{ 4, Coin::Unspent, 1, 10, KeyType::Regular } };
        db->store(coins);
        coins = db->selectCoins(5, false);
        WALLET_CHECK(coins.size() == 2);
        WALLET_CHECK(coins[0].m_amount == 2);
    }
    {
        db->remove(db->selectCoins(18, false));
        vector<Coin> coins = {
            Coin{ 4, Coin::Unspent, 1, 10, KeyType::Regular },
            Coin{ 4, Coin::Unspent, 1, 10, KeyType::Regular },
            Coin{ 4, Coin::Unspent, 1, 10, KeyType::Regular },
            Coin{ 4, Coin::Unspent, 1, 10, KeyType::Regular } };
        db->store(coins);
        coins = db->selectCoins(1, false);
        WALLET_CHECK(coins.size() == 1);
        WALLET_CHECK(coins[0].m_amount == 4);
    }
    {
        db->remove(db->selectCoins(16, false));
        vector<Coin> coins = {
            Coin{ 3, Coin::Unspent, 1, 10, KeyType::Regular },
            Coin{ 4, Coin::Unspent, 1, 10, KeyType::Regular },
            Coin{ 5, Coin::Unspent, 1, 10, KeyType::Regular },
            Coin{ 7, Coin::Unspent, 1, 10, KeyType::Regular } };
        db->store(coins);
        coins = db->selectCoins(6, false);
        WALLET_CHECK(coins.size() == 1);
        WALLET_CHECK(coins[0].m_amount == 7);
    }
    {
        db->remove(db->selectCoins(19, false));
        vector<Coin> coins = {
            Coin{ 1, Coin::Unspent, 1, 10, KeyType::Regular },
            Coin{ 2, Coin::Unspent, 1, 10, KeyType::Regular },
            Coin{ 3, Coin::Unspent, 1, 10, KeyType::Regular },
            Coin{ 4, Coin::Unspent, 1, 10, KeyType::Regular } };
        db->store(coins);
        coins = db->selectCoins(4, false);
        WALLET_CHECK(coins.size() == 1);
        WALLET_CHECK(coins[0].m_amount == 4);

        coins = db->selectCoins(7, false);
        WALLET_CHECK(coins.size() == 2);
        WALLET_CHECK(coins[0].m_amount == 3);
        WALLET_CHECK(coins[1].m_amount == 4);
    }
    {
        db->remove(db->selectCoins(10, false));
        vector<Coin> coins = {
            Coin{ 2, Coin::Unspent, 1, 10, KeyType::Regular },
            Coin{ 5, Coin::Unspent, 1, 10, KeyType::Regular },
            Coin{ 7, Coin::Unspent, 1, 10, KeyType::Regular } };
        db->store(coins);
        coins = db->selectCoins(6, false);
        WALLET_CHECK(coins.size() == 1);
        WALLET_CHECK(coins[0].m_amount == 7);
    }
    {
        db->remove(db->selectCoins(14, false));
        vector<Coin> coins = {
            Coin{ 235689, Coin::Unspent, 1, 10, KeyType::Regular },
            Coin{ 2999057, Coin::Unspent, 1, 10, KeyType::Regular },
            Coin{ 500000, Coin::Unspent, 1, 10, KeyType::Regular },
            Coin{ 5000000, Coin::Unspent, 1, 10, KeyType::Regular },
            Coin{ 40000000, Coin::Unspent, 1, 10, KeyType::Regular },
            Coin{ 40000000, Coin::Unspent, 1, 10, KeyType::Regular },
            Coin{ 40000000, Coin::Unspent, 1, 10, KeyType::Regular },
        };
        db->store(coins);
        coins = db->selectCoins(41000000, false);
        WALLET_CHECK(coins.size() == 2);
        WALLET_CHECK(coins[0].m_amount == 2999057);
        WALLET_CHECK(coins[1].m_amount == 40000000);
        coins = db->selectCoins(4000000, false);
        WALLET_CHECK(coins.size() == 1);
        WALLET_CHECK(coins[0].m_amount == 5000000);
    }
}

void TestSelect2()
{
    auto db = createSqliteKeychain();
    const Amount c = 925;
    vector<Coin> t;
    t.reserve(c);
    for (Amount i = 1; i <= c; ++i)
    {
        t.emplace_back( 40000000, Coin::Unspent, 1, 10, KeyType::Regular );
    }
    db->store(t);
    {
        Coin c{ 30000000, Coin::Unspent, 1, 10, KeyType::Regular };
        db->store(c);
    }
     auto coins = db->selectCoins(347000000, false);
     WALLET_CHECK(coins.size() == 9);
     WALLET_CHECK(coins[0].m_amount == 30000000);
}

int main() 
{
    int logLevel = LOG_LEVEL_DEBUG;
#if LOG_VERBOSE_ENABLED
    logLevel = LOG_LEVEL_VERBOSE;
#endif
    auto logger = beam::Logger::create(logLevel, logLevel);
	ECC::InitializeContext();

	TestKeychain();
    TestStoreCoins();
    TestStoreTxRecord();
    TestTxRollback();
    TestRollback();
    TestPeers();
    TestSelect();
    TestSelect2();
    TestAddresses();

    return WALLET_CHECK_RESULT;
}
