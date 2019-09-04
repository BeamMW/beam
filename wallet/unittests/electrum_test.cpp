// Copyright 2019 The Beam Team
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

#include "utility/logger.h"
#include "utility/io/timer.h"
#include "utility/io/tcpserver.h"
#include "utility/helpers.h"
#include "nlohmann/json.hpp"

#include "wallet/bitcoin/electrum.h"
//#include "wallet/litecoin/litecoin_electrum.h"

#include "test_helpers.h"

//#include <3rdparty/libbitcoin/include/bitcoin/bitcoin.hpp>

WALLET_TEST_INIT

namespace beam::bitcoin
{
    class Provider : public IElectrumSettingsProvider
    {
    public:
        Provider(const ElectrumSettings& settings)
            : m_settings(settings)
        {
        }

        ~Provider() override
        {

        }

        ElectrumSettings GetElectrumSettings() const override
        {
            return m_settings;
        }

    private:
        ElectrumSettings m_settings;
    };
}

using namespace beam;
using json = nlohmann::json;



void testAddress()
{
    bitcoin::ElectrumSettings settings;
    settings.m_secretWords = { "child", "happy", "moment", "weird", "ten", "token", "stuff", "surface", "success", "desk", "embark", "observe" };
    settings.m_addressVersion = libbitcoin::wallet::ec_private::testnet_p2kh;
    settings.m_isMainnet = false;

    io::Address address;
    //btc
    address.resolve("testnet1.bauerj.eu");
    address.port(50002);
    //ltc
    //address.resolve("electrum.ltc.xurious.com");
    //address.port(51002);

    settings.m_address = address;
    auto provider = std::make_shared<bitcoin::Provider>(settings);

    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);
    //BitcoinOptions options;
    //options.m_seedPhrase = { "child", "happy", "moment", "weird", "ten", "token", "stuff", "surface", "success", "desk", "embark", "observe" };
    //options.m_chainType = wallet::SwapSecondSideChainType::Testnet;
    bitcoin::Electrum electrum(*mainReactor, provider);

    electrum.getRawChangeAddress([](const bitcoin::IBridge::Error&, const std::string& addr)
    {
        LOG_INFO() << "address = " << addr;
    });
}

//void testDumpPrivKey()
//{
//    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
//    io::Reactor::Scope scope(*mainReactor);
//    BitcoinOptions options;
//    options.m_seedPhrase = { "child", "happy", "moment", "weird", "ten", "token", "stuff", "surface", "success", "desk", "embark", "observe" };
//    options.m_chainType = wallet::SwapSecondSideChainType::Testnet;
//    Electrum electrum(*mainReactor, options);
//
//    electrum.dumpPrivKey("mkgTKdapn48BM8BaMTDSnd1miT1AZSjV7P", [](const IBridge::Error&, const std::string& privateKey)
//    {
//        LOG_INFO() << "private key = " << privateKey;
//    });
//}
//
//void testGetTxOut()
//{
//    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
//    io::Reactor::Scope scope(*mainReactor);
//    LitecoinOptions options;
//    options.m_seedPhrase = { "ridge", "sunny", "neutral", "address", "fossil", "gospel", "common", "brush", "cactus", "poverty", "fitness", "duty" };
//    options.m_chainType = wallet::SwapSecondSideChainType::Testnet;
//    LitecoinElectrum electrum(*mainReactor, options);
//    //const std::string txID = "d75ecb28d9289025037de08fb7ed894bda7a22a28657dd4694b947b4db22f2b6"; // for btc
//    const std::string txID = "abe587dad072c847d793a626c472e712fd7a0d297fdad8a5ddd94fadf605e17e"; // for ltc
//
//    electrum.getTxOut(txID, 1, [mainReactor](const IBridge::Error&, const std::string& hex, double value, uint16_t confirmations)
//    {
//        LOG_INFO() << "hex = " << hex;
//        LOG_INFO() << "value = " << value;
//        LOG_INFO() << "confirmations = " << confirmations;
//
//        mainReactor->stop();
//    });
//
//    mainReactor->run();
//}
//
//void testGetBlockCount()
//{
//    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
//    io::Reactor::Scope scope(*mainReactor);
//    BitcoinOptions options;
//    options.m_seedPhrase = { "child", "happy", "moment", "weird", "ten", "token", "stuff", "surface", "success", "desk", "embark", "observe" };
//    options.m_chainType = wallet::SwapSecondSideChainType::Testnet;
//    Electrum electrum(*mainReactor, options);
//
//    electrum.getBlockCount([mainReactor](const IBridge::Error& , uint64_t height)
//    {
//        LOG_INFO() << "height = " << height;
//        
//        mainReactor->stop();
//    });
//
//    mainReactor->run();
//}
//
//void testListUnspent()
//{
//    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
//    io::Reactor::Scope scope(*mainReactor);
//    BitcoinOptions options;
//    options.m_seedPhrase = { "child", "happy", "moment", "weird", "ten", "token", "stuff", "surface", "success", "desk", "embark", "observe" };
//    options.m_chainType = wallet::SwapSecondSideChainType::Testnet;
//    Electrum electrum(*mainReactor, options);
//
//    electrum.listUnspent([mainReactor](const IBridge::Error&, const std::vector<Electrum::Utxo>& coins)
//    {
//        for (auto coin : coins)
//        {
//            LOG_INFO() << "details = " << coin.m_details;
//        }
//
//        mainReactor->stop();
//    });
//
//    mainReactor->run();
//}
//
//void testGetBalance()
//{
//    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
//    io::Reactor::Scope scope(*mainReactor);
//    LitecoinOptions options;
//    options.m_seedPhrase = { "ridge", "sunny", "neutral", "address", "fossil", "gospel", "common", "brush", "cactus", "poverty", "fitness", "duty" };
//    //sunny;ridge;neutral;address;fossil;gospel;common;brush;cactus;poverty;fitness;duty
//    //ridge;sunny;neutral;address;fossil;gospel;common;brush;cactus;poverty;fitness;duty
//
//    // snake shuffle pepper treat foam useful wife ranch cause brief lock chicken
//    options.m_chainType = wallet::SwapSecondSideChainType::Testnet;
//    LitecoinElectrum electrum(*mainReactor, options);
//
//    electrum.getBalance(0, [mainReactor](const IBridge::Error&, double balance)
//    {
//        LOG_INFO() << "balance = " << balance;
//
//        mainReactor->stop();
//    });
//
//    mainReactor->run();
//}
//
//void testFundRawTransaction()
//{
//    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
//    io::Reactor::Scope scope(*mainReactor);
//    LitecoinOptions options;
//    options.m_seedPhrase = { "sunny", "ridge", "neutral", "address", "fossil", "gospel", "common", "brush", "cactus", "poverty", "fitness", "duty" };
//    //sunny;ridge;neutral;address;fossil;gospel;common;brush;cactus;poverty;fitness;duty
//    //ridge;sunny;neutral;address;fossil;gospel;common;brush;cactus;poverty;fitness;duty
//    options.m_chainType = wallet::SwapSecondSideChainType::Testnet;
//    LitecoinElectrum electrum(*mainReactor, options);
//
//    using namespace libbitcoin;
//    using namespace libbitcoin::wallet;
//    using namespace libbitcoin::chain;
//
//    payment_address destinationAddress("miFx77ae92psazXuj2b3nRw7ssbBe4fEms");
//
//    script outputScript = script().to_pay_key_hash_pattern(destinationAddress.hash());
//    output output1(2 * satoshi_per_bitcoin / 100, outputScript);
//
//    transaction tx;
//    tx.set_version(2);
//    tx.outputs().push_back(output1);    
//
//    std::string rawTx = encode_base16(tx.to_data());
//
//    electrum.fundRawTransaction(rawTx, 10000, [mainReactor, &electrum](const IBridge::Error&, std::string tx, int changePos)
//    {
//        LOG_INFO() << "tx = " << tx;
//        LOG_INFO() << "changePos = " << changePos;
//
//        electrum.signRawTransaction(tx, [mainReactor, &electrum](const IBridge::Error&, std::string tx, bool)
//        {
//            LOG_INFO() << "tx = " << tx;
//
//            electrum.sendRawTransaction(tx, [mainReactor, &electrum](const IBridge::Error&, std::string txID)
//            {
//                LOG_INFO() << "txID = " << txID;
//                mainReactor->stop();
//            });            
//        });
//
//        //mainReactor->stop();
//    });
//
//    mainReactor->run();
//}

int main()
{
    int logLevel = LOG_LEVEL_DEBUG;
    auto logger = beam::Logger::create(logLevel, logLevel);

    testAddress();
    //testDumpPrivKey();
    //testGetTxOut();
    //testGetBlockCount();
    //testGetBalance();
    //testListUnspent();
    //testFundRawTransaction();

    assert(g_failureCount == 0);
    return WALLET_CHECK_RESULT;
}