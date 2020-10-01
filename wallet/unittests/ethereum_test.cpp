// Copyright 2020 The Beam Team
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

#include "test_helpers.h"

#include "wallet/transactions/swaps/bridges/ethereum/ethereum.h"

WALLET_TEST_INIT

using namespace beam;
using namespace std;

namespace beam::ethereum
{
    class Provider : public ISettingsProvider
    {
    public:
        Provider(const Settings& settings)
            : m_settings(settings)
        {
        }

        Settings GetSettings() const override
        {
            return m_settings;
        }

        void SetSettings(const Settings& settings) override
        {
            m_settings = settings;
        }

        bool CanModify() const override
        {
            return true;
        }

        void AddRef() override
        {
        }

        void ReleaseRef() override
        {

        }

    private:
        Settings m_settings;
    };
}

void testAddress()
{
    std::cout << "\nTesting generation of ethereum address...\n";

    ethereum::Settings settings;
    settings.m_secretWords = { "grass", "happy", "napkin", "skill", "hazard", "isolate", "slot", "barely", "stamp", "dismiss", "there", "found" };
    settings.m_accountIndex = 0;

    auto provider = std::make_shared<ethereum::Provider>(settings);
    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);
    ethereum::EthereumBridge bridge(*mainReactor, *provider);

    std::cout << bridge.generateEthAddress() << std::endl;
}

void testBalance()
{
    std::cout << "\nTesting balance...\n";

    ethereum::Settings settings;
    settings.m_secretWords = { "grass", "happy", "napkin", "skill", "hazard", "isolate", "slot", "barely", "stamp", "dismiss", "there", "found" };
    settings.m_accountIndex = 0;
    settings.m_address = "127.0.0.1:7545";
    settings.m_shouldConnect = true;

    auto provider = std::make_shared<ethereum::Provider>(settings);
    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);
    ethereum::EthereumBridge bridge(*mainReactor, *provider);

    //std::cout << bridge.generateEthAddress() << std::endl;

    bridge.getBalance([mainReactor](ECC::uintBig balance)
    {
        std::cout << balance << std::endl;
        mainReactor->stop();
    });

    mainReactor->run();
}

void testBlockNumber()
{
    std::cout << "\nTesting block number...\n";

    ethereum::Settings settings;
    settings.m_secretWords = { "grass", "happy", "napkin", "skill", "hazard", "isolate", "slot", "barely", "stamp", "dismiss", "there", "found" };
    settings.m_accountIndex = 0;
    settings.m_address = "127.0.0.1:7545";
    settings.m_shouldConnect = true;

    auto provider = std::make_shared<ethereum::Provider>(settings);
    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);
    ethereum::EthereumBridge bridge(*mainReactor, *provider);

    //std::cout << bridge.generateEthAddress() << std::endl;

    bridge.getBlockNumber([mainReactor](Amount blockNumber)
    {
        std::cout << blockNumber << std::endl;
        mainReactor->stop();
    });

    mainReactor->run();
}

void testTransactionCount()
{
    std::cout << "\nTesting transaction count...\n";

    ethereum::Settings settings;
    settings.m_secretWords = { "grass", "happy", "napkin", "skill", "hazard", "isolate", "slot", "barely", "stamp", "dismiss", "there", "found" };
    settings.m_accountIndex = 0;
    settings.m_address = "127.0.0.1:7545";
    settings.m_shouldConnect = true;

    auto provider = std::make_shared<ethereum::Provider>(settings);
    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);
    ethereum::EthereumBridge bridge(*mainReactor, *provider);

    //std::cout << bridge.generateEthAddress() << std::endl;

    bridge.getTransactionCount([mainReactor](Amount blockNumber)
    {
        std::cout << blockNumber << std::endl;
        mainReactor->stop();
    });

    mainReactor->run();
}

void testTransactionReceipt()
{
    std::cout << "\nTesting transaction receipt...\n";

    ethereum::Settings settings;
    settings.m_secretWords = { "grass", "happy", "napkin", "skill", "hazard", "isolate", "slot", "barely", "stamp", "dismiss", "there", "found" };
    settings.m_accountIndex = 0;
    settings.m_address = "127.0.0.1:7545";
    settings.m_shouldConnect = true;

    auto provider = std::make_shared<ethereum::Provider>(settings);
    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);
    ethereum::EthereumBridge bridge(*mainReactor, *provider);

    //std::cout << bridge.generateEthAddress() << std::endl;

    bridge.getTransactionReceipt("0xb860a0b859ec69dc20ac5849bc7902006bad012b1ff182aac98be24c91ab5aeb", [mainReactor]()
    {
        mainReactor->stop();
    });

    mainReactor->run();
}

void testCall()
{
    std::cout << "\nTesting call...\n";

    ethereum::Settings settings;
    settings.m_secretWords = { "grass", "happy", "napkin", "skill", "hazard", "isolate", "slot", "barely", "stamp", "dismiss", "there", "found" };
    settings.m_accountIndex = 0;
    settings.m_address = "127.0.0.1:7545";
    settings.m_shouldConnect = true;

    auto provider = std::make_shared<ethereum::Provider>(settings);
    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);
    ethereum::EthereumBridge bridge(*mainReactor, *provider);

    //std::cout << bridge.generateEthAddress() << std::endl;

    bridge.call("0x1Fa4e11e4C5973321216C31a1aA698c7157dFeDd", "0xd03f4cba0000000000000000000000000000000000000000000000000000000000000004", [mainReactor]()
    {
        mainReactor->stop();
    });

    mainReactor->run();
}

libbitcoin::short_hash ShortAddressFromStr(const std::string& addressStr)
{
    auto buffer = beam::from_hex(std::string(addressStr.begin() + 2, addressStr.end()));
    libbitcoin::short_hash address;
    std::move(buffer.begin(), buffer.end(), address.begin());
    return address;
}

void testSwap()
{
    const std::string kLockMethodHash = "0xae052147";
    const std::string kRefundMethodHash = "0x7249fbb6";
    const std::string kRedeemMethodHash = "0xb31597ad";
    const std::string kGetDetailsMethodHash = "0x6bfec360";
    const std::string kContractAddress = "0xBcb29073ebFf87eFD2a9800BF51a89ad89b3070E";

    ethereum::Settings settingsAlice;               
    settingsAlice.m_secretWords = { "weather", "hen", "detail", "region", "misery", "click", "wealth", "butter", "immense", "hire", "pencil", "social" };
    settingsAlice.m_accountIndex = 1;
    settingsAlice.m_address = "127.0.0.1:7545";
    settingsAlice.m_shouldConnect = true;

    ethereum::Settings settingsBob;
    settingsBob.m_secretWords = { "weather", "hen", "detail", "region", "misery", "click", "wealth", "butter", "immense", "hire", "pencil", "social" };
    settingsBob.m_accountIndex = 0;
    settingsBob.m_address = "127.0.0.1:7545";
    settingsBob.m_shouldConnect = true;

    auto providerAlice = std::make_shared<ethereum::Provider>(settingsAlice);
    auto providerBob = std::make_shared<ethereum::Provider>(settingsBob);
    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);
    ethereum::EthereumBridge bridgeAlice(*mainReactor, *providerAlice);
    ethereum::EthereumBridge bridgeBob(*mainReactor, *providerBob);

    // lock
    ethereum::EthBaseTransaction tx;
    tx.m_nonce = 14u;
    tx.m_gas = 200000u;
    tx.m_gasPrice = 3000000u;
    tx.m_value = 2'000'000'000'000'000'000u;
    tx.m_from = ShortAddressFromStr(bridgeAlice.generateEthAddress());
    tx.m_receiveAddress = ShortAddressFromStr(kContractAddress);

    // LockMethodHash + refundTimeInBlocks + hashedSecret + participant
    ECC::uintBig refundTimeInBlocks = 2u;
    ECC::uintBig secret;
    ECC::GenRandom(secret);
    libbitcoin::data_chunk secretDataChunk(std::begin(secret.m_pData), std::end(secret.m_pData));
    libbitcoin::hash_digest secretHash = libbitcoin::sha256_hash(secretDataChunk);
    libbitcoin::short_hash participant = ShortAddressFromStr(bridgeBob.generateEthAddress());

    LOG_DEBUG() << "secret: " << secret.str();
    LOG_DEBUG() << "secretHash: " << libbitcoin::encode_base16(secretHash);

    tx.m_data.reserve(4 + 32 + 32 + 32);
    libbitcoin::decode_base16(tx.m_data, std::string(std::begin(kLockMethodHash) + 2, std::end(kLockMethodHash)));
    tx.m_data.insert(tx.m_data.end(), std::begin(refundTimeInBlocks.m_pData), std::end(refundTimeInBlocks.m_pData));
    tx.m_data.insert(tx.m_data.end(), std::begin(secretHash), std::end(secretHash));
    // address's size is 20, so fill 12 elements by 0x00
    tx.m_data.insert(tx.m_data.end(), 12u, 0x00);
    tx.m_data.insert(tx.m_data.end(), std::begin(participant), std::end(participant));

    // TODO: sign TX to EthereumBridge ?
    libbitcoin::data_chunk secretDataAlice;
    libbitcoin::decode_base16(secretDataAlice, "feea6be022b9ec09a1ee55820bba1d7acc98c889f590cf7939c4a6a8b0967e5b");
    libbitcoin::ec_secret secretAlice;
    std::move(secretDataAlice.begin(), secretDataAlice.end(), std::begin(secretAlice));
    bridgeAlice.sendRawTransaction(libbitcoin::encode_base16(tx.GetRawSigned(secretAlice)), [&](std::string txHash)
        {
            LOG_DEBUG() << "TX hash: " << txHash;
            
            // get details
            bridgeAlice.call(kContractAddress, kGetDetailsMethodHash + libbitcoin::encode_base16(secretHash), [mainReactor]()
                {
                });

            // redeem !
            ethereum::EthBaseTransaction redeemTx;
            redeemTx.m_nonce = 11u;
            redeemTx.m_gas = 150000u;
            redeemTx.m_gasPrice = 3000000u;
            redeemTx.m_value = 0u;
            redeemTx.m_from = ShortAddressFromStr(bridgeBob.generateEthAddress());
            redeemTx.m_receiveAddress = ShortAddressFromStr(kContractAddress);

            // kRedeemMethodHash + secret + secretHash
            redeemTx.m_data.reserve(4 + 32 + 32);
            libbitcoin::decode_base16(redeemTx.m_data, std::string(std::begin(kRedeemMethodHash) + 2, std::end(kRedeemMethodHash)));
            redeemTx.m_data.insert(redeemTx.m_data.end(), std::begin(secret.m_pData), std::end(secret.m_pData));
            redeemTx.m_data.insert(redeemTx.m_data.end(), std::begin(secretHash), std::end(secretHash));

            libbitcoin::data_chunk secretDataBob;
            libbitcoin::decode_base16(secretDataBob, "a6a5f75faf7d5819369139cc031afecd6175d9960d29ee671463a1828dbb489d");
            libbitcoin::ec_secret secretBob;
            std::move(secretDataBob.begin(), secretDataBob.end(), std::begin(secretBob));

            bridgeBob.sendRawTransaction(libbitcoin::encode_base16(redeemTx.GetRawSigned(secretBob)), [mainReactor](std::string txHash)
                {
                    LOG_DEBUG() << "TX hash: " << txHash;
                    mainReactor->stop();
                });
        });

    mainReactor->run();
}

int main()
{
    int logLevel = LOG_LEVEL_DEBUG;
    auto logger = beam::Logger::create(logLevel, logLevel);

    testAddress();
    testBalance();
    testBlockNumber();
    testTransactionCount();
    testTransactionReceipt();
    testCall();

    testSwap();
    
    assert(g_failureCount == 0);
    return WALLET_CHECK_RESULT;
}