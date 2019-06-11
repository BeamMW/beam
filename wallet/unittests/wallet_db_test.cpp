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
#include "utility/test_helpers.h"

#include "utility/logger.h"
#include <boost/filesystem.hpp>
#include <numeric>

using namespace std;
using namespace ECC;
using namespace beam;
using namespace beam::wallet;

WALLET_TEST_INIT

namespace
{
IWalletDB::Ptr createSqliteWalletDB()
{
    const char* dbName = "wallet.db";
    if (boost::filesystem::exists(dbName))
    {
        boost::filesystem::remove(dbName);
    }
    ECC::NoLeak<ECC::uintBig> seed;
    seed.V = Zero;
    auto walletDB = WalletDB::init(dbName, string("pass123"), seed, io::Reactor::get_Current().shared_from_this());
    beam::Block::SystemState::ID id = { };
    id.m_Height = 134;
    walletDB->setSystemStateID(id);
    return walletDB;
}

Coin CreateCoin(Amount amount, Height maturity = MaxHeight, Height confirmHeight = MaxHeight, Height spentHeight = MaxHeight)
{
    Coin c(amount);
    c.m_maturity = maturity;
    c.m_confirmHeight = confirmHeight;
    c.m_spentHeight = spentHeight;
    return c;
}

Coin CreateAvailCoin(Amount amount, Height maturity = 10)
{
    return CreateCoin(amount, maturity, maturity);
}

void TestWalletDataBase()
{
    cout << "Wallet database test\n";
    {
        Coin coin{ 1234 };
        coin.m_ID.m_Type = Key::Type::Regular;
        coin.m_ID.m_Idx = 132;
        WALLET_CHECK(coin.m_ID.m_Idx == 132);
        WALLET_CHECK(coin.m_ID.m_Value == 1234);
        WALLET_CHECK(coin.m_ID.m_Type == Key::Type::Regular);
    }
    auto walletDB = createSqliteWalletDB();

    Coin coin1 = CreateAvailCoin(5);
    walletDB->store(coin1);

    Coin coin2 = CreateAvailCoin(2);
    walletDB->store(coin2);

    {
        auto coins = walletDB->selectCoins(7);
        WALLET_CHECK(coins.size() == 2);

        // simulate locking
        TxID txID;
        ZeroObject(txID);

        storage::setTxParameter(*walletDB, txID, wallet::TxParameterID::Status, TxStatus::InProgress, false);
        for (Coin& c : coins)
            c.m_spentTxId = txID;
        walletDB->save(coins);

        for (size_t i = 0; i < coins.size(); ++i)
        {
            walletDB->find(coins[i]); // would refresh the status
            WALLET_CHECK(coins[i].m_status == Coin::Outgoing);
        }
    }

    WALLET_CHECK(walletDB->selectCoins(5).size() == 0);

    {
        Block::SystemState::ID a;
        Hash::Processor() << static_cast<uint32_t>(rand()) >> a.m_Hash;
        a.m_Height = rand();

        const char* name = "SystemStateID";
        storage::setVar(*walletDB, name, "dummy");
        storage::setVar(*walletDB, name, a);

        Block::SystemState::ID b;
        WALLET_CHECK(storage::getVar(*walletDB, name, b));

        WALLET_CHECK(a == b);
    }
}

void TestStoreCoins()
{
    cout << "\nWallet database coins test\n";

    {
        auto walletDB = createSqliteWalletDB();

        vector<Coin> coins;
        for (int i = 0; i < 10; ++i)
        {
            coins.push_back(CreateAvailCoin(i));
        }

        walletDB->store(coins);

        CoinIDList ids;
        for (const auto& c : coins)
        {
            ids.push_back(c.m_ID);
        }
        auto coins2 = walletDB->getCoinsByID(ids);
        WALLET_CHECK(coins2.size() == ids.size());
        WALLET_CHECK(equal(coins.begin(), coins.end(), coins2.begin()));
    }

    auto walletDB = createSqliteWalletDB();

  
    Coin coin = { 5, Key::Type::Coinbase };
    walletDB->store(coin);
    coin = { 4, Key::Type::Comission };
    walletDB->store(coin);
    coin = { 2, Key::Type::Regular };
    walletDB->store(coin);
    coin = { 5, Key::Type::Coinbase };
    walletDB->store(coin);
    coin = { 1, Key::Type::Regular };
    walletDB->store(coin);
    coin = { 5, Key::Type::Coinbase };
    walletDB->store(coin);
    coin = { 4, Key::Type::Comission };
    walletDB->store(coin);
    coin = { 1, Key::Type::Regular };
    walletDB->store(coin);
    coin = { 4, Key::Type::Comission };
    walletDB->store(coin);
    coin = { 1, Key::Type::Regular };
    walletDB->store(coin);
    coin = { 1, Key::Type::Regular };
    walletDB->store(coin);

    auto coins = vector<Coin>{
            Coin{ 5, Key::Type::Coinbase },
            Coin{ 4, Key::Type::Comission },
            Coin{ 2, Key::Type::Regular },
            Coin{ 5, Key::Type::Coinbase },
            Coin{ 1, Key::Type::Regular },
            Coin{ 5, Key::Type::Coinbase },
            Coin{ 4, Key::Type::Comission },
            Coin{ 1, Key::Type::Regular },
            Coin{ 4, Key::Type::Comission },
            Coin{ 1, Key::Type::Regular },
            Coin{ 1, Key::Type::Regular } };
    walletDB->store(coins);


    
    int coinBase = 0;
    int comission = 0;
    int regular = 0;
    walletDB->visit([&coinBase, &comission, &regular](const Coin& coin)->bool
    {
        if (coin.m_ID.m_Type == Key::Type::Coinbase)
        {
            ++coinBase;
        }
        else if (coin.m_ID.m_Type == Key::Type::Comission)
        {
            ++comission;
        }
        else if (coin.m_ID.m_Type == Key::Type::Regular)
        {
            ++regular;
        }
        return true;
    });

    WALLET_CHECK(coinBase == 6);
    WALLET_CHECK(comission == 6);
    WALLET_CHECK(regular == 10);

    coins.clear();
    walletDB->visit([&coins](const auto& coin)->bool
    {
        coins.push_back(coin);
        return false;
    });
    WALLET_CHECK(coins[0].m_confirmHeight == MaxHeight);
    coins[0].m_confirmHeight = 423;
    walletDB->save(coins[0]);
    coins.clear();
    walletDB->visit([&coins](const auto& coin)->bool
    {
        coins.push_back(coin);
        return false;
    });
    beam::Merkle::Hash t;
    t = 12345678U;
    WALLET_CHECK(coins[0].m_confirmHeight == 423);
}
using namespace beam;
using namespace beam::wallet;
void TestStoreTxRecord()
{
    cout << "\nWallet database transactions test\n";
    auto walletDB = createSqliteWalletDB();
    TxID id = {{1, 3, 4, 5 ,65}};
    TxDescription tr;
    tr.m_txId = id;
    tr.m_amount = 34;
    tr.m_peerId.m_Pk = unsigned(23);
    tr.m_peerId.m_Channel = 0U;
    tr.m_myId.m_Pk = unsigned(42);
    tr.m_myId.m_Channel = 0U;
    tr.m_createTime = 123456;
    tr.m_minHeight = 134;
    tr.m_sender = true;
    tr.m_status = TxStatus::InProgress;
    tr.m_change = 5;

    WALLET_CHECK_NO_THROW(walletDB->saveTx(tr));
    WALLET_CHECK_NO_THROW(walletDB->saveTx(tr));
    {
        wallet::TxType type = TxType::Simple;
        WALLET_CHECK(storage::getTxParameter(*walletDB, id, TxParameterID::TransactionType, type));
        WALLET_CHECK(type == TxType::Simple);
    }

    TxDescription tr2 = tr;
    tr2.m_txId = id;
    tr2.m_amount = 43;
    tr2.m_minHeight = 234;
    tr2.m_createTime = 1234564;
    tr2.m_modifyTime = 12345644;
    tr2.m_status = TxStatus::Completed;
    tr2.m_change = 5;
    WALLET_CHECK_NO_THROW(walletDB->saveTx(tr2));
    
    auto t = walletDB->getTxHistory();
    WALLET_CHECK(t.size() == 1);
    WALLET_CHECK(t[0].m_txId == tr.m_txId);
    WALLET_CHECK(t[0].m_amount == tr.m_amount);
    WALLET_CHECK(t[0].m_minHeight == tr.m_minHeight);
    WALLET_CHECK(t[0].m_peerId == tr.m_peerId);
    WALLET_CHECK(t[0].m_myId == tr.m_myId);
    WALLET_CHECK(t[0].m_createTime == tr.m_createTime);
    WALLET_CHECK(t[0].m_modifyTime == tr2.m_modifyTime);
    WALLET_CHECK(t[0].m_sender == tr.m_sender);
    WALLET_CHECK(t[0].m_status == tr2.m_status);
    WALLET_CHECK(t[0].m_change == tr.m_change);
    TxID id2 = {{ 3,4,5 }};
    WALLET_CHECK_NO_THROW(walletDB->deleteTx(id2));
    WALLET_CHECK_NO_THROW(walletDB->deleteTx(id));
    {
        wallet::TxType type = TxType::Simple;
        WALLET_CHECK(storage::getTxParameter(*walletDB, id, TxParameterID::TransactionType, type));
        WALLET_CHECK(type == TxType::Simple);
    }
    


    WALLET_CHECK_NO_THROW(walletDB->saveTx(tr2));
    WALLET_CHECK_NO_THROW(walletDB->saveTx(tr2));
    boost::optional<TxDescription> tr3;
    WALLET_CHECK_NO_THROW(tr3 = walletDB->getTx(tr2.m_txId));
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
    WALLET_CHECK_NO_THROW(walletDB->deleteTx(tr2.m_txId));
    WALLET_CHECK(walletDB->getTxHistory().empty());

    for (uint8_t i = 0; i < 100; ++i)
    {
        tr.m_txId[0] = i;
        WALLET_CHECK_NO_THROW(walletDB->saveTx(tr));
    }
    WALLET_CHECK(walletDB->getTxHistory().size() == 100);
    t = walletDB->getTxHistory(TxType::Simple, 50, 2);
    WALLET_CHECK(t.size() == 2);
    t = walletDB->getTxHistory(TxType::Simple, 99, 10);
    WALLET_CHECK(t.size() == 1);
    t = walletDB->getTxHistory(TxType::Simple, 9, 0);
    WALLET_CHECK(t.size() == 0);
    t = walletDB->getTxHistory(TxType::Simple, 50, 2);
    id[0] = 50;
    WALLET_CHECK(t[0].m_txId == id);
    id[0] = 51;
    WALLET_CHECK(t[1].m_txId == id);

    t = walletDB->getTxHistory(TxType::Simple, 0, 1);
    WALLET_CHECK(t.size() == 1 && t[0].m_txId[0] == 0);

    t = walletDB->getTxHistory(TxType::Simple, 99, 1);
    WALLET_CHECK(t.size() == 1 && t[0].m_txId[0] == 99);

    t = walletDB->getTxHistory(TxType::Simple, 100, 1);
    WALLET_CHECK(t.size() == 0);
}

void TestRollback()
{
    cout << "\nWallet database rollback test\n";
    auto db = createSqliteWalletDB();
    for (uint64_t i = 0; i < 9; ++i)
    {
        Coin coin1 = CreateCoin( 5, i + 10, Height(i + 1) );
        db->store(coin1);
    }

    for (uint64_t i = 9; i < 10; ++i)
    {
        Coin coin1 = CreateCoin( 5, 0, Height(1) );
        db->store(coin1);
    }

    // was created after branch
    {
        Coin coin1 = CreateCoin( 5, 7, Height(8) );
        db->store(coin1);
    }


    // rewards
    // should not be deleted
    {
        Coin coin1 = CreateCoin( 5, 8, Height(8) );
        db->store(coin1);
    }

    {
        Coin coin1 = CreateCoin( 5, 8, Height(8) );
        db->store(coin1);
    }

    {
        Coin coin1 = CreateCoin( 5, 9, Height(9) );
        db->store(coin1);
    }

    {
        Coin coin1 = CreateCoin( 5, 9, Height(9) );
        db->store(coin1);
    }
    // should be preserved
    {
        Coin coin1 = CreateCoin( 5, 7, Height(7), 8 );
        db->store(coin1);
    }

    {
        Coin coin1 = CreateCoin( 5, 7, Height(7), 8 );
        db->store(coin1);
    }

    {
        Coin coin1 = CreateCoin( 5, 5, Height(5), 6 );
        db->store(coin1);
    }

    {
        Coin coin1 = CreateCoin( 5, 5, Height(5), 7); // would be rolled-back
        db->store(coin1);
    }

    {
        Coin coin1 = CreateCoin( 5, 4, Height(4) );
        db->store(coin1);
    }

    {
        Coin coin1 = CreateCoin( 5, 4, Height(4) );
        db->store(coin1);
    }

    db->rollbackConfirmedUtxo(6);

    vector<Coin> coins;
    db->visit([&coins](const auto& c)->bool
    {
        coins.push_back(c);
        return true;
    });

    WALLET_CHECK(coins.size() == 21);

    for (const Coin& c : coins)
    {
        WALLET_CHECK((Coin::Unavailable == c.m_status) == (c.m_confirmHeight == MaxHeight));
    }

    for (int i = 0; i < 5; ++i)
    {
        auto& c = coins[i];
        WALLET_CHECK(c.m_status == Coin::Available);
    }

    for (int i = 6; i < 9; ++i)
    {
        auto& c = coins[i];
        WALLET_CHECK(c.m_status == Coin::Unavailable);
    }
    for (int i = 9; i < 10; ++i)
    {
        auto& c = coins[i];
        WALLET_CHECK(c.m_status == Coin::Available);
    }

    {
        // for now it is unconfirmed in future we would have to distinguish such coins
        auto& c = coins[10];
        WALLET_CHECK(c.m_status == Coin::Unavailable);
    }

    for (int i = 11; i < 17; ++i)
    {
        auto& c = coins[i];
        WALLET_CHECK(c.m_status == Coin::Unavailable);
    }
    for (int i = 17; i < 18; ++i)
    {
        auto& c = coins[i];
        WALLET_CHECK(c.m_status == Coin::Spent);
    }
    for (int i = 18; i < 19; ++i)
    {
        auto& c = coins[i];
        WALLET_CHECK(c.m_status == Coin::Available);
    }
    for (int i = 19; i < 21; ++i)
    {
        auto& c = coins[i];
        WALLET_CHECK(c.m_status == Coin::Available);
    }
}

void TestBlockRollbackWithTx()
{
    cout << "\nWallet database rollback with tx test\n";
    IWalletDB::Ptr db = createSqliteWalletDB();
    TxID txID1 = { { 1, 3, 4, 5 ,65 } };
    TxID txID2 = { {1, 3, 4} };
    TxID txID3 = { {1, 3, 4, 9} };

    Coin coin1 = CreateCoin(1, 6, 6);
    coin1.m_createTxId = txID1;

    Coin coin2 = CreateCoin(2, 4, 4, 6);
    coin2.m_createTxId = txID2;
    coin2.m_spentTxId = txID1;

    Coin coin3 = CreateCoin(3, 6, 6);

    {
        TxDescription tx;
        tx.m_txId = txID1;
        tx.m_peerId.m_Pk = unsigned(23);
        tx.m_peerId.m_Channel = 0U;
        tx.m_myId.m_Pk = unsigned(42);
        tx.m_myId.m_Channel = 0U;
        tx.m_createTime = 123456;
        tx.m_minHeight = 5;
        tx.m_status = TxStatus::Completed;
        tx.m_change = 0;
        tx.m_amount = 2;
        tx.m_sender = true;

        db->saveTx(tx);

        storage::setTxParameter(*db, txID1, wallet::TxParameterID::KernelUnconfirmedHeight, Height(5), false);
        storage::setTxParameter(*db, txID1, wallet::TxParameterID::KernelProofHeight, Height(6), false);
    }

    {
        TxDescription tx;
        tx.m_txId = txID2;
        tx.m_peerId.m_Pk = unsigned(23);
        tx.m_peerId.m_Channel = 0U;
        tx.m_myId.m_Pk = unsigned(42);
        tx.m_myId.m_Channel = 0U;
        tx.m_createTime = 123456;
        tx.m_minHeight = 3;
        tx.m_status = TxStatus::Completed;
        tx.m_change = 0;
        tx.m_amount = 2;
        tx.m_sender = true;

        db->saveTx(tx);
        storage::setTxParameter(*db, txID2, wallet::TxParameterID::KernelUnconfirmedHeight, Height(3), false);
        storage::setTxParameter(*db, txID2, wallet::TxParameterID::KernelProofHeight, Height(4), false);
    }

    {
        TxDescription tx;
        tx.m_txId = txID3;
        tx.m_peerId.m_Pk = unsigned(23);
        tx.m_peerId.m_Channel = 0U;
        tx.m_myId.m_Pk = unsigned(42);
        tx.m_myId.m_Channel = 0U;
        tx.m_createTime = 123456;
        tx.m_minHeight = 3;
        tx.m_status = TxStatus::Completed;
        tx.m_change = 0;
        tx.m_amount = 2;
        tx.m_sender = true;

        db->saveTx(tx);

        storage::setTxParameter(*db, txID3, wallet::TxParameterID::KernelProofHeight, Height(5), false);
    }
    
    db->store(coin1);
    db->store(coin2);
    db->store(coin3);
    {
        auto transactions = db->getTxHistory();
        WALLET_CHECK(transactions.size() == 3);
        {
            auto tx = db->getTx(txID1);
            WALLET_CHECK(tx->canResume() == false);
        }
        {
            auto tx = db->getTx(txID2);
            WALLET_CHECK(tx->canResume() == false);
        }
        {
            auto tx = db->getTx(txID3);
            WALLET_CHECK(tx->canResume() == false);
        }
    }
    db->rollbackConfirmedUtxo(5);
    {
        vector<Coin> coins;
        db->visit([&coins](const Coin& c)->bool
        {
            coins.push_back(c);
            return true;
        });
        WALLET_CHECK(coins.size() == 3);
        WALLET_CHECK(coins[0].m_ID.m_Value == 1);
        WALLET_CHECK(coins[0].m_confirmHeight == MaxHeight);
        WALLET_CHECK(coins[0].m_status == Coin::Incoming);

        WALLET_CHECK(coins[1].m_ID.m_Value == 2);
        WALLET_CHECK(coins[1].m_confirmHeight == 4);
        WALLET_CHECK(coins[1].m_status == Coin::Outgoing);

        WALLET_CHECK(coins[2].m_ID.m_Value == 3);
        WALLET_CHECK(coins[2].m_confirmHeight == MaxHeight);
        WALLET_CHECK(coins[2].m_status == Coin::Unavailable);

        auto transactions = db->getTxHistory();
        WALLET_CHECK(transactions.size() == 3);
        {
            auto tx = db->getTx(txID1);
            WALLET_CHECK(tx->m_status == TxStatus::Registering);
            Height h = 0;
            WALLET_CHECK(storage::getTxParameter(*db, txID1, wallet::TxParameterID::KernelProofHeight, h));
            WALLET_CHECK(h == 0);
            WALLET_CHECK(storage::getTxParameter(*db, txID1, wallet::TxParameterID::KernelUnconfirmedHeight, h));
            WALLET_CHECK(h == 0);
        }
        {
            auto tx = db->getTx(txID2);
            WALLET_CHECK(tx->m_status == TxStatus::Completed);
            Height h = 0;
            WALLET_CHECK(storage::getTxParameter(*db, txID2, wallet::TxParameterID::KernelProofHeight, h));
            WALLET_CHECK(h == 4);
            WALLET_CHECK(storage::getTxParameter(*db, txID2, wallet::TxParameterID::KernelUnconfirmedHeight, h));
            WALLET_CHECK(h == 3);
        }
        {
            auto tx = db->getTx(txID3);
            WALLET_CHECK(tx->m_status == TxStatus::Completed);
            Height h = 0;
            WALLET_CHECK(storage::getTxParameter(*db, txID3, wallet::TxParameterID::KernelProofHeight, h));
            WALLET_CHECK(h == 5);
            WALLET_CHECK(!storage::getTxParameter(*db, txID3, wallet::TxParameterID::KernelUnconfirmedHeight, h));

        }
    }

}

void TestTxRollback()
{
    cout << "\nWallet database transaction rollback test\n";

    auto walletDB = createSqliteWalletDB();
    TxID id = { { 1, 3, 4, 5 ,65 } };
    TxID id2 = { {1, 3, 4} };

    Coin coin1 = CreateCoin(5, 10, 2);
    walletDB->store(coin1);

    Coin coin2 = CreateCoin(6, 11, 2);
    coin2.m_spentTxId = id;
    walletDB->store(coin2);

    Coin coin3 = CreateCoin(3, 2);
    coin3.m_createTxId = id;
    walletDB->store(coin3);
    walletDB->save({ coin2 });

    storage::setTxParameter(*walletDB, id, wallet::TxParameterID::Status, TxStatus::Registering, true);

    vector<Coin> coins;
    walletDB->visit([&coins](const Coin& c)->bool
    {
        coins.push_back(c);
        return true;
    });

    WALLET_CHECK(coins.size() == 3);

    WALLET_CHECK(coins[1].m_status == Coin::Outgoing);
    WALLET_CHECK(coins[1].m_spentTxId == id);
    WALLET_CHECK(coins[2].m_status == Coin::Incoming);
    WALLET_CHECK(coins[2].m_createTxId == id);

    walletDB->rollbackTx(id2);

    coins.clear();
    walletDB->visit([&coins](const Coin& c)->bool
    {
        coins.push_back(c);
        return true;
    });
    WALLET_CHECK(coins.size() == 3);

    walletDB->rollbackTx(id);

    coins.clear();
    walletDB->visit([&coins](const Coin& c)->bool
    {
        coins.push_back(c);
        return true;
    });

    WALLET_CHECK(coins.size() == 2);
    WALLET_CHECK(coins[1].m_status == Coin::Available);
    WALLET_CHECK(coins[1].m_spentTxId.is_initialized() == false);
}

void TestAddresses()
{
    cout << "\nWallet database addresses test\n";
    auto db = createSqliteWalletDB();
    auto addresses = db->getAddresses(true);
    WALLET_CHECK(addresses.empty());
    addresses = db->getAddresses(false);
    WALLET_CHECK(addresses.empty());

    WalletAddress a = {};
    a.m_label = "test label";
    a.m_category = "test category";
    a.m_createTime = beam::getTimestamp();
    a.m_duration = 23;
    a.m_OwnID = 44;
    a.m_walletID = storage::generateWalletIDFromIndex(*db, a.m_OwnID);

    db->saveAddress(a);

    addresses = db->getAddresses(true);
    WALLET_CHECK(addresses.size() == 1);
    WALLET_CHECK(addresses[0].m_walletID == a.m_walletID);
    WALLET_CHECK(addresses[0].m_label == a.m_label);
    WALLET_CHECK(addresses[0].m_category == a.m_category);
    WALLET_CHECK(addresses[0].m_createTime == a.m_createTime);
    WALLET_CHECK(addresses[0].m_duration == a.m_duration);
    WALLET_CHECK(addresses[0].m_OwnID == a.m_OwnID);

    a.m_category = "cat2";

    db->saveAddress(a);

    addresses = db->getAddresses(true);
    WALLET_CHECK(addresses.size() == 1);
    WALLET_CHECK(addresses[0].m_walletID == a.m_walletID);
    WALLET_CHECK(addresses[0].m_label == a.m_label);
    WALLET_CHECK(addresses[0].m_category == a.m_category);
    WALLET_CHECK(addresses[0].m_createTime == a.m_createTime);
    WALLET_CHECK(addresses[0].m_duration == a.m_duration);
    WALLET_CHECK(addresses[0].m_OwnID == a.m_OwnID);

    auto exported = storage::ExportAddressesToJson(*db);
    WALLET_CHECK(!exported.empty());
 
    auto a2 = db->getAddress(a.m_walletID);
    WALLET_CHECK(a2.is_initialized());

    db->deleteAddress(a.m_walletID);
    WALLET_CHECK(db->getAddresses(true).empty());

    a2 = db->getAddress(a.m_walletID);
    WALLET_CHECK(!a2.is_initialized());

    WALLET_CHECK(storage::ImportAddressesFromJson(*db, &exported[0], exported.size()));
    {
        auto a3 = db->getAddress(a.m_walletID);
        WALLET_CHECK(a3.is_initialized());
    }
}

vector<Coin::ID> ExtractIDs(const vector<Coin>& src)
{
    vector<Coin::ID> res;
    res.reserve(src.size());
    for (const Coin& c : src)
        res.push_back(c.m_ID);
    return res;
}

void TestSelect()
{
    cout << "\nWallet database coin selection 1 test\n";
    auto db = createSqliteWalletDB();
    const Amount c = 10;
    for (Amount i = 1; i <= c; ++i)
    {
        Coin coin = CreateAvailCoin(i);
        db->store(coin);
    }
    for (Amount i = 1; i <= c; ++i)
    {
        auto coins = db->selectCoins(i);
        WALLET_CHECK(coins.size() == 1);
        WALLET_CHECK(coins[0].m_ID.m_Value == i);
    }
    {
        auto coins = db->selectCoins(56);
        WALLET_CHECK(coins.empty());
    }
    {
        auto coins = db->selectCoins(55);
        WALLET_CHECK(coins.size() == 10);
    }
    {
        auto coins = db->selectCoins(15);
        WALLET_CHECK(coins.size() == 2);
    }
    for (Amount i = c + 1; i <= 55; ++i)
    {
        auto coins = db->selectCoins(i);
        WALLET_CHECK(!coins.empty());
    }
    // double coins
    for (Amount i = 1; i <= c; ++i)
    {
        Coin coin = CreateAvailCoin(i);
        db->store(coin);
    }
    for (Amount i = 1; i <= c; ++i)
    {
        auto coins = db->selectCoins(i);
        WALLET_CHECK(coins.size() == 1);
        WALLET_CHECK(coins[0].m_ID.m_Value == i);
    }
    {
        auto coins = db->selectCoins(111);
        WALLET_CHECK(coins.empty());
    }
    {
        auto coins = db->selectCoins(110);
        WALLET_CHECK(coins.size() == 20);
    }
    for (Amount i = c + 1; i <= 110; ++i)
    {
        auto coins = db->selectCoins(i);
        WALLET_CHECK(!coins.empty());
        auto sum = accumulate(coins.begin(), coins.end(), Amount(0), [](const auto& left, const auto& right) {return left + right.m_ID.m_Value; });
        WALLET_CHECK(sum == i);
    }

    {
    db->remove(ExtractIDs(db->selectCoins(110)));
    vector<Coin> coins = {
        CreateAvailCoin(2),
        CreateAvailCoin(1),
        CreateAvailCoin(9) };

    db->store(coins);
    coins = db->selectCoins(6);
    WALLET_CHECK(coins.size() == 1);
    WALLET_CHECK(coins[0].m_ID.m_Value == 9);
    }
    {
        db->remove(ExtractIDs(db->selectCoins(12)));
        vector<Coin> coins = {
            CreateAvailCoin(2),
            CreateAvailCoin(4),
            CreateAvailCoin(4),
            CreateAvailCoin(4),
            CreateAvailCoin(4) };
        db->store(coins);
        coins = db->selectCoins(5);
        WALLET_CHECK(coins.size() == 2);
        WALLET_CHECK(coins.back().m_ID.m_Value == 2);
    }
    {
        db->remove(ExtractIDs(db->selectCoins(18)));
        vector<Coin> coins = {
            CreateAvailCoin(4),
            CreateAvailCoin(4),
            CreateAvailCoin(4),
            CreateAvailCoin(4) };
        db->store(coins);
        coins = db->selectCoins(1);
        WALLET_CHECK(coins.size() == 1);
        WALLET_CHECK(coins[0].m_ID.m_Value == 4);
    }
    {
        db->remove(ExtractIDs(db->selectCoins(16)));
        vector<Coin> coins = {
            CreateAvailCoin(3),
            CreateAvailCoin(4),
            CreateAvailCoin(5),
            CreateAvailCoin(7) };

        db->store(coins);
        coins = db->selectCoins(6);
        WALLET_CHECK(coins.size() == 1);
        WALLET_CHECK(coins[0].m_ID.m_Value == 7);
    }
    {
        db->remove(ExtractIDs(db->selectCoins(19)));
        vector<Coin> coins = {
            CreateAvailCoin(1),
            CreateAvailCoin(2),
            CreateAvailCoin(3),
            CreateAvailCoin(4) };

        db->store(coins);
        coins = db->selectCoins(4);
        WALLET_CHECK(coins.size() == 1);
        WALLET_CHECK(coins[0].m_ID.m_Value == 4);

        coins = db->selectCoins(7);
        WALLET_CHECK(coins.size() == 2);
        WALLET_CHECK(coins[0].m_ID.m_Value == 4);
        WALLET_CHECK(coins[1].m_ID.m_Value == 3);
    }
    {
        db->remove(ExtractIDs(db->selectCoins(10)));
        vector<Coin> coins = {
            CreateAvailCoin(2),
            CreateAvailCoin(5),
            CreateAvailCoin(7) };

        db->store(coins);
        coins = db->selectCoins(6);
        WALLET_CHECK(coins.size() == 1);
        WALLET_CHECK(coins[0].m_ID.m_Value == 7);
    }
    {
        db->remove(ExtractIDs(db->selectCoins(14)));
        vector<Coin> coins = {
            CreateAvailCoin(235689),
            CreateAvailCoin(2999057),
            CreateAvailCoin(500000),
            CreateAvailCoin(5000000),
            CreateAvailCoin(40000000),
            CreateAvailCoin(40000000),
            CreateAvailCoin(40000000),
        };

        db->store(coins);
        coins = db->selectCoins(41000000);
        WALLET_CHECK(coins.size() == 2);
        WALLET_CHECK(coins[1].m_ID.m_Value == 2999057);
        WALLET_CHECK(coins[0].m_ID.m_Value == 40000000);
        coins = db->selectCoins(4000000);
        WALLET_CHECK(coins.size() == 1);
        WALLET_CHECK(coins[0].m_ID.m_Value == 5000000);
    }
}

vector<Coin> SelectCoins(IWalletDB::Ptr db, Amount amount, bool printCoins = true)
{
    helpers::StopWatch sw;
    cout << "Selecting " << amount << " Groths\n";
    sw.start();
    auto selectedCoins = db->selectCoins(amount);
    sw.stop();
    cout << "Elapsed time: " << sw.milliseconds() << " ms\n";

    Amount sum = 0;
    for (auto coin : selectedCoins)
    {
        sum += coin.m_ID.m_Value;
        if (printCoins)
        {
            LOG_INFO() << coin.m_ID.m_Value;
        }
    }
    LOG_INFO() << "sum = " << sum << " change = " << sum - amount;
    WALLET_CHECK(amount <= sum);
#ifdef NDEBUG
    WALLET_CHECK(sw.milliseconds() <= 1000);
#endif // !NDEBUG

    return selectedCoins;
}

void TestSelect2()
{
    cout << "\nWallet database coin selection 2 test\n";
    auto db = createSqliteWalletDB();
    const Amount c = 100000;
    vector<Coin> t;
    t.reserve(c);
    for (Amount i = 1; i <= c; ++i)
    {
        t.push_back(CreateAvailCoin(40000000));
    }
    db->store(t);
    {
        Coin coin = CreateAvailCoin(30000000);
        db->store(coin);
    }

    auto coins = SelectCoins(db, 347'000'000, false);

    WALLET_CHECK(coins.size() == 9);
    WALLET_CHECK(coins[8].m_ID.m_Value == 30000000);
}

void TestTxParameters()
{
    cout << "\nWallet database transaction parameters test\n";
    auto db = createSqliteWalletDB();
    TxID txID = { {1, 3, 5} };
    // public parameter cannot be overriten
    Amount amount = 0;
    WALLET_CHECK(!storage::getTxParameter(*db, txID, TxParameterID::Amount, amount));
    WALLET_CHECK(amount == 0);
    WALLET_CHECK(storage::setTxParameter(*db, txID, TxParameterID::Amount, 8765, false));
    WALLET_CHECK(storage::getTxParameter(*db, txID, TxParameterID::Amount, amount));
    WALLET_CHECK(amount == 8765);
    WALLET_CHECK(!storage::setTxParameter(*db, txID, TxParameterID::Amount, 786, false));
    WALLET_CHECK(storage::getTxParameter(*db, txID, TxParameterID::Amount, amount));
    WALLET_CHECK(amount == 8765);

    // private parameter can be overriten
    TxStatus status = TxStatus::Pending;
    WALLET_CHECK(!storage::getTxParameter(*db, txID, TxParameterID::Status, status));
    WALLET_CHECK(status == TxStatus::Pending);
    WALLET_CHECK(storage::setTxParameter(*db, txID, TxParameterID::Status, TxStatus::Completed, false));
    WALLET_CHECK(storage::getTxParameter(*db, txID, TxParameterID::Status, status));
    WALLET_CHECK(status == TxStatus::Completed);
    WALLET_CHECK(storage::setTxParameter(*db, txID, TxParameterID::Status, TxStatus::InProgress, false));
    WALLET_CHECK(storage::getTxParameter(*db, txID, TxParameterID::Status, status));
    WALLET_CHECK(status == TxStatus::InProgress);

    // check different types

    ByteBuffer b = { 1, 2, 3 };
    WALLET_CHECK(storage::setTxParameter(*db, txID, TxParameterID::Status, b, false));
    ByteBuffer b2;
    WALLET_CHECK(storage::getTxParameter(*db, txID, TxParameterID::Status, b2));
    WALLET_CHECK(equal(b.begin(), b.end(), b2.begin(), b2.end()));

    ECC::Scalar::Native s, s2;
    s = 123U;
    WALLET_CHECK(storage::setTxParameter(*db, txID, TxParameterID::BlindingExcess, s, false));
    WALLET_CHECK(storage::getTxParameter(*db, txID, TxParameterID::BlindingExcess, s2));
    WALLET_CHECK(s == s2);

    ECC::Point p;
    p.m_X = unsigned(143521);
    p.m_Y = 0;
    ECC::Point::Native pt, pt2;
    pt.Import(p);
    WALLET_CHECK(storage::setTxParameter(*db, txID, TxParameterID::PeerPublicNonce, pt, false));
    WALLET_CHECK(storage::getTxParameter(*db, txID, TxParameterID::PeerPublicNonce, pt2));
    WALLET_CHECK(p == pt2);
}

void TestSelect3()
{
    cout << "\nWallet database coin selection 3 test\n";
    auto db = createSqliteWalletDB();
    const uint32_t count = 2500;
    const uint32_t partCount = 100;
    Amount startAmount = 80'000'000;
    vector<Coin> coins;
    coins.reserve(count);

    for (uint32_t i = 1; i <= count; ++i)
    {
        Amount a = Amount(startAmount / pow(2, i / partCount));

        coins.push_back(CreateAvailCoin(a));
    }

    db->store(coins);
    {
        Coin coin = CreateAvailCoin(30'000'000);
        db->store(coin);
    }

    SelectCoins(db, 45'678'910);
}

void TestSelect4()
{
    cout << "\nWallet database coin selection 4 test\n";
    auto db = createSqliteWalletDB();
    vector<Coin> coins;

    coins.push_back(CreateAvailCoin(2));

    coins.push_back(CreateAvailCoin(30101));
    coins.push_back(CreateAvailCoin(30102));
    coins.push_back(CreateAvailCoin(32000));

    db->store(coins);

    SelectCoins(db, 60203);
}

void TestSelect5()
{
    cout << "\nWallet database coin selection 5 test\n";
    auto db = createSqliteWalletDB();
    const uint32_t count = 10000;
    const uint32_t partCount = 400;
    Amount startAmount = 40'000'000;
    vector<Coin> coins;
    coins.reserve(count);

    for (uint32_t i = 1; i <= count; ++i)
    {
        Amount a = Amount(startAmount / pow(2, i / partCount));

        coins.push_back(CreateAvailCoin(a));
    }

    db->store(coins);
    {
        Coin coin = CreateAvailCoin(30'000'000);
        db->store(coin);
    }

    SelectCoins(db, 45'678'910);
}

void TestSelect6()
{
    cout << "\nWallet database coin selection 6 test\n";
    const uint32_t count = 100000;
    {
        auto db = createSqliteWalletDB();
        
        vector<Coin> coins;
        coins.reserve(count);

        for (uint32_t i = 1; i <= count; ++i)
        {
            coins.push_back(CreateAvailCoin(Amount(i)));
        }

        db->store(coins);

        SelectCoins(db, 450'678'910, false);
    }

    {
        auto db = createSqliteWalletDB();

        vector<Coin> coins;
        coins.reserve(count);

        for (uint32_t i = 1; i <= count; ++i)
        {
            Amount amount = rand();
            coins.push_back(CreateAvailCoin(amount));
        }

        db->store(coins);

        SelectCoins(db, 450'678'910, false);
    }

    {
        auto db = createSqliteWalletDB();

        vector<Coin> coins;
        coins.reserve(count);

        for (uint32_t i = 1; i <= count; ++i)
        {
            Amount amount = rand() % 10000;
            coins.push_back(CreateAvailCoin(amount));
        }

        db->store(coins);

        SelectCoins(db, 450'678'910, false);
    }
    {
        auto db = createSqliteWalletDB();

        vector<Coin> coins;
        coins.reserve(count);

        for (uint32_t i = 1; i <= count; ++i)
        {
            Amount amount = 1000000 + rand() % 100000;
            coins.push_back(CreateAvailCoin(amount));
        }

        db->store(coins);

        SelectCoins(db, 450'678'910, false);
    }
}

void TestWalletMessages()
{
    auto db = createSqliteWalletDB();

    {
        auto messages = db->getWalletMessages();
        WALLET_CHECK(messages.empty());
    }

    WalletID walletID = Zero;
    WALLET_CHECK(walletID.FromHex("6b5992ffed7cb3c86bb7e408edfecafa047701eaa197ebf5b9e2df2f21b40caa4a"));
    {
        wallet::SetTxParameter msg;
        msg.m_From = walletID;
        msg.AddParameter(TxParameterID::Amount, 100);
        msg.AddParameter(TxParameterID::PeerID, walletID);
        msg.AddParameter(TxParameterID::Lifetime, 130);

        auto id = db->saveWalletMessage(WalletMessage{ 0, walletID, toByteBuffer(msg)});
        WALLET_CHECK(id == 1);
        auto messages = db->getWalletMessages();
        WALLET_CHECK(messages.size() == 1);
        WALLET_CHECK(messages[0].m_ID == 1);
        WALLET_CHECK(messages[0].m_PeerID == walletID);
        Amount amount = 0;
        SetTxParameter message;
        WALLET_CHECK(fromByteBuffer(messages[0].m_Message, message));
        WALLET_CHECK(message.GetParameter(TxParameterID::Amount, amount) && amount == 100);
        WalletID walletID2 = Zero;
        WALLET_CHECK(message.GetParameter(TxParameterID::PeerID, walletID2) && walletID2 == walletID);
        Height lifetime = 0;
        WALLET_CHECK(message.GetParameter(TxParameterID::Lifetime, lifetime) && lifetime == 130);
    }

    {
        wallet::SetTxParameter msg;
        msg.m_From = walletID;
        msg.AddParameter(TxParameterID::Amount, 200);
        msg.AddParameter(TxParameterID::PeerID, walletID);
        msg.AddParameter(TxParameterID::Lifetime, 230);

        auto id = db->saveWalletMessage(WalletMessage{ 0, walletID, toByteBuffer(msg) });
        WALLET_CHECK(id == 2);
        auto messages = db->getWalletMessages();
        WALLET_CHECK(messages.size() == 2);
        WALLET_CHECK(messages[1].m_ID == 2);
        WALLET_CHECK(messages[1].m_PeerID == walletID);
        Amount amount = 0;
        SetTxParameter message;
        WALLET_CHECK(fromByteBuffer(messages[1].m_Message, message));
        WALLET_CHECK(message.GetParameter(TxParameterID::Amount, amount) && amount == 200);
        WalletID walletID2 = Zero;
        WALLET_CHECK(message.GetParameter(TxParameterID::PeerID, walletID2) && walletID2 == walletID);
        Height lifetime = 0;
        WALLET_CHECK(message.GetParameter(TxParameterID::Lifetime, lifetime) && lifetime == 230);
    }
    {
        WALLET_CHECK_NO_THROW(db->deleteWalletMessage(0));
        WALLET_CHECK_NO_THROW(db->deleteWalletMessage(10));
        auto messages = db->getWalletMessages();
        WALLET_CHECK(messages.size() == 2);
    }
    {
        db->deleteWalletMessage(1);
        auto messages = db->getWalletMessages();
        WALLET_CHECK(messages.size() == 1);
    }

    {
        auto incoming = db->getIncomingWalletMessages();
        WALLET_CHECK(incoming.empty());
        ByteBuffer b = { 1, 45, 6 };
        auto id = db->saveIncomingWalletMessage(23, b);
        WALLET_CHECK(id == 1);
        incoming = db->getIncomingWalletMessages();
        WALLET_CHECK(incoming.size() == 1);
        WALLET_CHECK(incoming[0].m_Message == b);
        WALLET_CHECK(incoming[0].m_Channel == 23);
        db->deleteIncomingWalletMessage(1);
        incoming = db->getIncomingWalletMessages();
        WALLET_CHECK(incoming.empty());
    }
}

}

int main() 
{
    int logLevel = LOG_LEVEL_DEBUG;
#if LOG_VERBOSE_ENABLED
    logLevel = LOG_LEVEL_VERBOSE;
#endif
    auto logger = beam::Logger::create(logLevel, logLevel);
    ECC::InitializeContext();

    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    TestWalletDataBase();
    TestStoreCoins();
    TestStoreTxRecord();
    TestTxRollback();
    TestRollback();
    TestBlockRollbackWithTx();
    TestSelect();
    TestSelect2();
    TestSelect3();
    TestSelect4();
    TestSelect5();
    TestSelect6();
    TestAddresses();
    TestTxParameters();
    TestWalletMessages();


    return WALLET_CHECK_RESULT;
}
