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

#include <ethash/keccak.hpp>
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

    std::cout << ethereum::ConvertEthAddressToStr(bridge.generateEthAddress()) << std::endl;
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

    bridge.getBalance([mainReactor](const ethereum::IBridge::Error&, ECC::uintBig balance)
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

    bridge.getBlockNumber([mainReactor](const ethereum::IBridge::Error&, Amount blockNumber)
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

    bridge.getTransactionCount([mainReactor](const ethereum::IBridge::Error&, Amount blockNumber)
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

    bridge.getTransactionReceipt("0xb860a0b859ec69dc20ac5849bc7902006bad012b1ff182aac98be24c91ab5aeb", [mainReactor](const ethereum::IBridge::Error&, const nlohmann::json&)
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

    auto addr = ethereum::ConvertStrToEthAddress("0x1Fa4e11e4C5973321216C31a1aA698c7157dFeDd");

    bridge.call(addr, "0xd03f4cba0000000000000000000000000000000000000000000000000000000000000004", [mainReactor](const ethereum::IBridge::Error&, const nlohmann::json&)
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
    const libbitcoin::short_hash kContractAddress = ethereum::ConvertStrToEthAddress("0xBcb29073ebFf87eFD2a9800BF51a89ad89b3070E");

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
    ECC::uintBig gas = 200000u;
    ECC::uintBig gasPrice = 3000000u;
    ECC::uintBig swapAmount = 2'000'000'000'000'000'000u;

    ECC::uintBig secret;
    ECC::GenRandom(secret);
    libbitcoin::data_chunk secretDataChunk(std::begin(secret.m_pData), std::end(secret.m_pData));
    libbitcoin::hash_digest secretHash = libbitcoin::sha256_hash(secretDataChunk);
    libbitcoin::short_hash participant = bridgeBob.generateEthAddress();

    LOG_DEBUG() << "secret: " << secret.str();
    LOG_DEBUG() << "secretHash: " << libbitcoin::encode_base16(secretHash);

    // LockMethodHash + refundTimeInBlocks + hashedSecret + participant
    ECC::uintBig refundTimeInBlocks = 2u;
    libbitcoin::data_chunk lockData;
    lockData.reserve(4 + 32 + 32 + 32);
    libbitcoin::decode_base16(lockData, std::string(std::begin(kLockMethodHash) + 2, std::end(kLockMethodHash)));
    lockData.insert(lockData.end(), std::begin(refundTimeInBlocks.m_pData), std::end(refundTimeInBlocks.m_pData));
    lockData.insert(lockData.end(), std::begin(secretHash), std::end(secretHash));
    // address's size is 20, so fill 12 elements by 0x00
    lockData.insert(lockData.end(), 12u, 0x00);
    lockData.insert(lockData.end(), std::begin(participant), std::end(participant));

    bridgeAlice.send(kContractAddress, lockData, swapAmount, gas, gasPrice, [&](const ethereum::IBridge::Error&, std::string txHash)
        {
            LOG_DEBUG() << "LOCK_TX hash: " << txHash;

            // redeem
            // kRedeemMethodHash + secret + secretHash
            libbitcoin::data_chunk redeemData;
            redeemData.reserve(4 + 32 + 32);
            libbitcoin::decode_base16(redeemData, std::string(std::begin(kRedeemMethodHash) + 2, std::end(kRedeemMethodHash)));
            redeemData.insert(redeemData.end(), std::begin(secret.m_pData), std::end(secret.m_pData));
            redeemData.insert(redeemData.end(), std::begin(secretHash), std::end(secretHash));

            bridgeBob.send(kContractAddress, redeemData, ECC::Zero, gas, gasPrice, [mainReactor](const ethereum::IBridge::Error&, std::string txHash)
                {
                    LOG_DEBUG() << "REDEEM_TX hash: " << txHash;
                    mainReactor->stop();
                });
        });
    mainReactor->run();

}

void testSwapWithAggregateSignature()
{
    const std::string kLockMethodHash = "0xbc18cc34";
    const std::string kRefundMethodHash = "0xfa89401a";
    const std::string kRedeemMethodHash = "0x8772acd6";
    const std::string kGetDetailsMethodHash = "0x7cf3285f";
    const libbitcoin::short_hash kContractAddress = ethereum::ConvertStrToEthAddress("0xAfF392dc83CC7263A619Bc0831D14c20C399a99D");

    ethereum::Settings settingsAlice;
    settingsAlice.m_secretWords = { "weather", "hen", "detail", "region", "misery", "click", "wealth", "butter", "immense", "hire", "pencil", "social" };
    settingsAlice.m_accountIndex = 5;
    settingsAlice.m_address = "127.0.0.1:7545";
    settingsAlice.m_shouldConnect = true;

    ethereum::Settings settingsBob;
    settingsBob.m_secretWords = { "weather", "hen", "detail", "region", "misery", "click", "wealth", "butter", "immense", "hire", "pencil", "social" };
    settingsBob.m_accountIndex = 6;
    settingsBob.m_address = "127.0.0.1:7545";
    settingsBob.m_shouldConnect = true;

    auto providerAlice = std::make_shared<ethereum::Provider>(settingsAlice);
    auto providerBob = std::make_shared<ethereum::Provider>(settingsBob);
    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);
    ethereum::EthereumBridge bridgeAlice(*mainReactor, *providerAlice);
    ethereum::EthereumBridge bridgeBob(*mainReactor, *providerBob);

    ECC::uintBig secret;
    ECC::GenRandom(secret);
    libbitcoin::ec_secret secretEC;
    std::move(std::begin(secret.m_pData), std::end(secret.m_pData), std::begin(secretEC));

    auto rawPk = libbitcoin::wallet::ec_private(secretEC, libbitcoin::wallet::ec_private::mainnet, false).to_public().encoded();
    LOG_DEBUG() << "PUBLIC: " << rawPk;
    auto tmp = beam::from_hex(std::string(rawPk.begin() + 2, rawPk.end()));
    auto hash = ethash::keccak256(&tmp[0], tmp.size());
    libbitcoin::short_hash addressFromSecret;
    std::copy_n(&hash.bytes[12], 20, addressFromSecret.begin());

    auto participant = bridgeBob.generateEthAddress();
    auto initiator = bridgeAlice.generateEthAddress();
    libbitcoin::data_chunk hashData;
    hashData.reserve(60);
    hashData.insert(hashData.end(), addressFromSecret.cbegin(), addressFromSecret.cend());
    hashData.insert(hashData.end(), participant.cbegin(), participant.cend());
    hashData.insert(hashData.end(), initiator.cbegin(), initiator.cend());
    hash = ethash::keccak256(&hashData[0], hashData.size());

    libbitcoin::data_chunk result(std::begin(hash.bytes), std::end(hash.bytes));

    LOG_DEBUG() << "participant: " << libbitcoin::encode_base16(participant);
    LOG_DEBUG() << "initiator: " << libbitcoin::encode_base16(initiator);
    LOG_DEBUG() << "addressFromSecret: " << libbitcoin::encode_base16(addressFromSecret);
    LOG_DEBUG() << "HASH " << libbitcoin::encode_base16(result);

    libbitcoin::hash_digest hashDigest;
    std::move(std::begin(hash.bytes), std::end(hash.bytes), hashDigest.begin());
    libbitcoin::recoverable_signature signature;
    libbitcoin::sign_recoverable(signature, secretEC, hashDigest);

    {
        libbitcoin::data_chunk resultSign(std::begin(signature.signature), std::end(signature.signature));
        resultSign.push_back(signature.recovery_id);
        LOG_DEBUG() << "SIGN: " << libbitcoin::encode_base16(resultSign);
    }

    // LockMethodHash + refundTimeInBlocks + addressFromSecret + participant
    ECC::uintBig refundTimeInBlocks = 2u;
    libbitcoin::data_chunk lockData;
    lockData.reserve(4 + 32 + 32 + 32);
    libbitcoin::decode_base16(lockData, std::string(std::begin(kLockMethodHash) + 2, std::end(kLockMethodHash)));
    lockData.insert(lockData.end(), std::begin(refundTimeInBlocks.m_pData), std::end(refundTimeInBlocks.m_pData));
    // address's size is 20, so fill 12 elements by 0x00
    lockData.insert(lockData.end(), 12u, 0x00);
    lockData.insert(lockData.end(), std::begin(addressFromSecret), std::end(addressFromSecret));
    // address's size is 20, so fill 12 elements by 0x00
    lockData.insert(lockData.end(), 12u, 0x00);
    lockData.insert(lockData.end(), std::begin(participant), std::end(participant));

    ECC::uintBig gas = 200000u;
    ECC::uintBig gasPrice = 3000000u;
    ECC::uintBig swapAmount = 2'000'000'000'000'000'000u;

    bridgeAlice.send(kContractAddress, lockData, swapAmount, gas, gasPrice, [&](const ethereum::IBridge::Error&, std::string txHash)
        {
            LOG_DEBUG() << "LOCK_TX hash: " << txHash;

            // redeem
            // kRedeemMethodHash + addressFromSecret + signature (r, s, v)
            libbitcoin::data_chunk redeemData;
            redeemData.reserve(4 + 32 + 32 + 32 + 32);
            libbitcoin::decode_base16(redeemData, std::string(std::begin(kRedeemMethodHash) + 2, std::end(kRedeemMethodHash)));
            // address's size is 20, so fill 12 elements by 0x00
            redeemData.insert(redeemData.end(), 12u, 0x00);
            redeemData.insert(redeemData.end(), std::begin(addressFromSecret), std::end(addressFromSecret));
            redeemData.insert(redeemData.end(), std::begin(signature.signature), std::end(signature.signature));
            redeemData.insert(redeemData.end(), 31u, 0x00);
            redeemData.push_back(signature.recovery_id + 27u);

            bridgeBob.send(kContractAddress, redeemData, ECC::Zero, gas, gasPrice, [mainReactor](const ethereum::IBridge::Error&, std::string txHash)
                {
                    LOG_DEBUG() << "REDEEM_TX hash: " << txHash;
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

    testSwapWithAggregateSignature();

    assert(g_failureCount == 0);
    return WALLET_CHECK_RESULT;
}