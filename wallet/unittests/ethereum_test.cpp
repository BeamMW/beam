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

    bridge.getBalance([mainReactor](const ethereum::IBridge::Error&, const std::string& balance)
    {
        boost::multiprecision::uint256_t tmp(balance);
        tmp /= ethereum::GetCoinUnitsMultiplier(beam::wallet::AtomicSwapCoin::Ethereum);
        std::cout << tmp.str() << std::endl;
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
    const libbitcoin::short_hash kContractAddress = ethereum::ConvertStrToEthAddress("0xe27a9126570731ac4c2e4b27690c74ea20ca56b0");

    ethereum::Settings settingsAlice;               
    settingsAlice.m_secretWords = { "silly", "profit", "jewel", "fox", "evoke", "victory", "until", "topic", "century", "depth", "usual", "update" };
    settingsAlice.m_accountIndex = 1;
    settingsAlice.m_address = "127.0.0.1:7545";
    settingsAlice.m_shouldConnect = true;

    ethereum::Settings settingsBob;
    settingsBob.m_secretWords = { "silly", "profit", "jewel", "fox", "evoke", "victory", "until", "topic", "century", "depth", "usual", "update" };
    settingsBob.m_accountIndex = 0;
    settingsBob.m_address = "127.0.0.1:7545";
    settingsBob.m_shouldConnect = true;

    auto providerAlice = std::make_shared<ethereum::Provider>(settingsAlice);
    auto providerBob = std::make_shared<ethereum::Provider>(settingsBob);
    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);
    auto bridgeAlice = std::make_shared<ethereum::EthereumBridge>(*mainReactor, *providerAlice);
    auto bridgeBob = std::make_shared<ethereum::EthereumBridge>(*mainReactor, *providerBob);

    // lock
    ECC::uintBig gas = 200000u;
    ECC::uintBig gasPrice = 3000000u;
    ECC::uintBig swapAmount = 2'000'000'000'000'000'000u;

    ECC::uintBig secret;
    ECC::GenRandom(secret);
    libbitcoin::data_chunk secretDataChunk(std::begin(secret.m_pData), std::end(secret.m_pData));
    libbitcoin::hash_digest secretHash = libbitcoin::sha256_hash(secretDataChunk);
    libbitcoin::short_hash participant = bridgeBob->generateEthAddress();

    LOG_DEBUG() << "secret: " << secret.str();
    LOG_DEBUG() << "secretHash: " << libbitcoin::encode_base16(secretHash);

    bridgeAlice->getBlockNumber([&](const ethereum::IBridge::Error& error, uint64_t blockCount)
        {
            // LockMethodHash + refundTimeInBlocks + hashedSecret + participant
            ECC::uintBig refundTimeInBlocks = 4u + blockCount;
            libbitcoin::data_chunk lockData;
            lockData.reserve(ethereum::kEthContractMethodHashSize + 3 * ethereum::kEthContractABIWordSize);
            libbitcoin::decode_base16(lockData, ethereum::swap_contract::GetLockMethodHash(false, true));
            lockData.insert(lockData.end(), std::begin(refundTimeInBlocks.m_pData), std::end(refundTimeInBlocks.m_pData));
            lockData.insert(lockData.end(), std::begin(secretHash), std::end(secretHash));
            // address's size is 20, so fill 12 elements by 0x00
            lockData.insert(lockData.end(), 12u, 0x00);
            lockData.insert(lockData.end(), std::begin(participant), std::end(participant));

            bridgeAlice->send(kContractAddress, lockData, swapAmount, gas, gasPrice, [&](const ethereum::IBridge::Error&, std::string txHash, uint64_t)
                {
                    LOG_DEBUG() << "LOCK_TX hash: " << txHash;

                    // redeem
                    // kRedeemMethodHash + secret + secretHash
                    libbitcoin::data_chunk redeemData;
                    redeemData.reserve(4 + 32 + 32);
                    libbitcoin::decode_base16(redeemData, ethereum::swap_contract::GetRedeemMethodHash(true));
                    redeemData.insert(redeemData.end(), std::begin(secret.m_pData), std::end(secret.m_pData));
                    redeemData.insert(redeemData.end(), std::begin(secretHash), std::end(secretHash));

                    bridgeBob->send(kContractAddress, redeemData, ECC::Zero, gas, gasPrice, [mainReactor](const ethereum::IBridge::Error&, std::string txHash, uint64_t)
                        {
                            LOG_DEBUG() << "REDEEM_TX hash: " << txHash;
                            mainReactor->stop();
                        });
                });
        });

    mainReactor->run();

}

void testSwapWithAggregateSignature()
{
    const libbitcoin::short_hash kContractAddress = ethereum::ConvertStrToEthAddress("0x81f58775ef55867c1b8685c0b1090e7cd2298da8");

    ethereum::Settings settingsAlice;
    settingsAlice.m_secretWords = { "silly", "profit", "jewel", "fox", "evoke", "victory", "until", "topic", "century", "depth", "usual", "update" };
    settingsAlice.m_accountIndex = 5;
    settingsAlice.m_address = "127.0.0.1:7545";
    settingsAlice.m_shouldConnect = true;

    ethereum::Settings settingsBob;
    settingsBob.m_secretWords = { "silly", "profit", "jewel", "fox", "evoke", "victory", "until", "topic", "century", "depth", "usual", "update" };
    settingsBob.m_accountIndex = 6;
    settingsBob.m_address = "127.0.0.1:7545";
    settingsBob.m_shouldConnect = true;

    auto providerAlice = std::make_shared<ethereum::Provider>(settingsAlice);
    auto providerBob = std::make_shared<ethereum::Provider>(settingsBob);
    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);
    auto bridgeAlice = std::make_shared<ethereum::EthereumBridge>(*mainReactor, *providerAlice);
    auto bridgeBob = std::make_shared<ethereum::EthereumBridge>(*mainReactor, *providerBob);

    ECC::uintBig secret;
    ECC::GenRandom(secret);
    libbitcoin::ec_secret secretEC;
    std::move(std::begin(secret.m_pData), std::end(secret.m_pData), std::begin(secretEC));

    auto rawPk = libbitcoin::wallet::ec_private(secretEC, libbitcoin::wallet::ec_private::mainnet, false).to_public().encoded();
    LOG_DEBUG() << "PUBLIC: " << rawPk;
    libbitcoin::short_hash addressFromSecret = ethereum::GetEthAddressFromPubkeyStr(rawPk);
    auto participant = bridgeBob->generateEthAddress();
    auto initiator = bridgeAlice->generateEthAddress();

    LOG_DEBUG() << "participant: " << libbitcoin::encode_base16(participant);
    LOG_DEBUG() << "initiator: " << libbitcoin::encode_base16(initiator);
    LOG_DEBUG() << "addressFromSecret: " << libbitcoin::encode_base16(addressFromSecret);

    ECC::uintBig gas = 200000u;
    ECC::uintBig gasPrice = 3000000u;
    ECC::uintBig swapAmount = 2'000'000'000'000'000'000u;

    bridgeAlice->getBlockNumber([&](const ethereum::IBridge::Error& error, uint64_t blockCount)
        {
            // LockMethodHash + refundTimeInBlocks + addressFromSecret + participant
            ECC::uintBig refundTimeInBlocks = 4u + blockCount;
            libbitcoin::data_chunk lockData;
            lockData.reserve(ethereum::kEthContractMethodHashSize + 3 * ethereum::kEthContractABIWordSize);
            libbitcoin::decode_base16(lockData, ethereum::swap_contract::GetLockMethodHash(false, false));
            ethereum::AddContractABIWordToBuffer({ std::begin(refundTimeInBlocks.m_pData), std::end(refundTimeInBlocks.m_pData) }, lockData);
            ethereum::AddContractABIWordToBuffer(addressFromSecret, lockData);
            ethereum::AddContractABIWordToBuffer(participant, lockData);

            bridgeAlice->send(kContractAddress, lockData, swapAmount, gas, gasPrice, [&, refundTimeInBlocks](const ethereum::IBridge::Error&, std::string txHash, uint64_t)
                {
                    LOG_DEBUG() << "LOCK_TX hash: " << txHash;

                    libbitcoin::data_chunk hashData;
                    hashData.reserve(60);
                    hashData.insert(hashData.end(), addressFromSecret.cbegin(), addressFromSecret.cend());
                    hashData.insert(hashData.end(), participant.cbegin(), participant.cend());
                    hashData.insert(hashData.end(), initiator.cbegin(), initiator.cend());
                    hashData.insert(hashData.end(), std::cbegin(refundTimeInBlocks.m_pData), std::cend(refundTimeInBlocks.m_pData));
                    auto hash = ethash::keccak256(&hashData[0], hashData.size());

                    LOG_DEBUG() << "RedeemTx, hash of data: " << libbitcoin::encode_base16({ std::cbegin(hash.bytes), std::cend(hash.bytes) });

                    libbitcoin::hash_digest hashDigest;
                    std::move(std::begin(hash.bytes), std::end(hash.bytes), hashDigest.begin());
                    libbitcoin::recoverable_signature signature;
                    libbitcoin::sign_recoverable(signature, secretEC, hashDigest);

                    {
                        libbitcoin::data_chunk resultSign(std::begin(signature.signature), std::end(signature.signature));
                        resultSign.push_back(signature.recovery_id);
                        LOG_DEBUG() << "RedeemTx, signature: " << libbitcoin::encode_base16(resultSign);
                    }

                    // redeem
                    // kRedeemMethodHash + addressFromSecret + signature (r, s, v)
                    libbitcoin::data_chunk redeemData;
                    redeemData.reserve(ethereum::kEthContractMethodHashSize + 4 * ethereum::kEthContractABIWordSize);
                    libbitcoin::decode_base16(redeemData, ethereum::swap_contract::GetRedeemMethodHash(false));
                    ethereum::AddContractABIWordToBuffer(addressFromSecret, redeemData);
                    redeemData.insert(redeemData.end(), std::begin(signature.signature), std::end(signature.signature));
                    redeemData.insert(redeemData.end(), 31u, 0x00);
                    redeemData.push_back(signature.recovery_id + 27u);

                    bridgeBob->send(kContractAddress, redeemData, ECC::Zero, gas, gasPrice, [mainReactor](const ethereum::IBridge::Error&, std::string txHash, uint64_t)
                        {
                            LOG_DEBUG() << "REDEEM_TX hash: " << txHash;
                            mainReactor->stop();
                        });
                });
        });

    mainReactor->run();
}

void testERC20Swap()
{
    const libbitcoin::short_hash kTokenContractAddress = ethereum::ConvertStrToEthAddress("0xe0a55f2c08125b7dac7132e02b043ed30359034e");
    const libbitcoin::short_hash kERC20HashLockContractAddress = ethereum::ConvertStrToEthAddress("0xfff7efc650f4a18567e830cf19fc6de067e0ffcf");

    ethereum::Settings settingsAlice;
    settingsAlice.m_secretWords = { "silly", "profit", "jewel", "fox", "evoke", "victory", "until", "topic", "century", "depth", "usual", "update" };
    settingsAlice.m_accountIndex = 3;
    settingsAlice.m_address = "127.0.0.1:7545";
    settingsAlice.m_shouldConnect = true;

    ethereum::Settings settingsBob;
    settingsBob.m_secretWords = { "silly", "profit", "jewel", "fox", "evoke", "victory", "until", "topic", "century", "depth", "usual", "update" };
    settingsBob.m_accountIndex = 4;
    settingsBob.m_address = "127.0.0.1:7545";
    settingsBob.m_shouldConnect = true;

    auto providerAlice = std::make_shared<ethereum::Provider>(settingsAlice);
    auto providerBob = std::make_shared<ethereum::Provider>(settingsBob);
    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);
    auto bridgeAlice = std::make_shared<ethereum::EthereumBridge>(*mainReactor, *providerAlice);
    auto bridgeBob = std::make_shared<ethereum::EthereumBridge>(*mainReactor, *providerBob);

    // lock
    ECC::uintBig gas = 200000u;
    ECC::uintBig gasPrice = 3000000u;
    ECC::uintBig swapAmount = 2'000'000'000'000'000'000u;

    ECC::uintBig secret;
    ECC::GenRandom(secret);
    libbitcoin::data_chunk secretDataChunk(std::begin(secret.m_pData), std::end(secret.m_pData));
    libbitcoin::hash_digest secretHash = libbitcoin::sha256_hash(secretDataChunk);
    libbitcoin::short_hash participant = bridgeBob->generateEthAddress();

    LOG_DEBUG() << "secret: " << secret.str();
    LOG_DEBUG() << "secretHash: " << libbitcoin::encode_base16(secretHash);

    bridgeAlice->getBlockNumber([&](const ethereum::IBridge::Error& error, uint64_t blockCount)
        {
            // LockMethodHash + refundTimeInBlocks + addressFromSecret + participant + contractAddress + value
            ECC::uintBig refundTimeInBlocks = 4u + blockCount;
            libbitcoin::data_chunk lockData;
            lockData.reserve(ethereum::kEthContractMethodHashSize + 5 * ethereum::kEthContractABIWordSize);
            libbitcoin::decode_base16(lockData, ethereum::swap_contract::GetLockMethodHash(true, true));
            lockData.insert(lockData.end(), std::begin(refundTimeInBlocks.m_pData), std::end(refundTimeInBlocks.m_pData));
            lockData.insert(lockData.end(), std::begin(secretHash), std::end(secretHash));
            ethereum::AddContractABIWordToBuffer(participant, lockData);
            ethereum::AddContractABIWordToBuffer(kTokenContractAddress, lockData);
            ethereum::AddContractABIWordToBuffer({ std::begin(swapAmount.m_pData), std::end(swapAmount.m_pData) }, lockData);

            bridgeAlice->send(kERC20HashLockContractAddress, lockData, ECC::Zero, gas, gasPrice, [&](const ethereum::IBridge::Error&, std::string txHash, uint64_t)
                {
                    LOG_DEBUG() << "LOCK_TX hash: " << txHash;

                    // redeem
                    // kRedeemMethodHash + secret + secretHash
                    libbitcoin::data_chunk redeemData;
                    redeemData.reserve(4 + 32 + 32);
                    libbitcoin::decode_base16(redeemData, ethereum::swap_contract::GetRedeemMethodHash(true));
                    redeemData.insert(redeemData.end(), std::begin(secret.m_pData), std::end(secret.m_pData));
                    redeemData.insert(redeemData.end(), std::begin(secretHash), std::end(secretHash));

                    bridgeBob->send(kERC20HashLockContractAddress, redeemData, ECC::Zero, gas, gasPrice, [mainReactor](const ethereum::IBridge::Error&, std::string txHash, uint64_t)
                        {
                            LOG_DEBUG() << "REDEEM_TX hash: " << txHash;
                            mainReactor->stop();
                        });
                });
        });

    mainReactor->run();

}

void testERC20SwapWithAggregateSignature()
{
    const libbitcoin::short_hash kTokenContractAddress = ethereum::ConvertStrToEthAddress("0xe0a55f2c08125b7dac7132e02b043ED30359034E");
    const libbitcoin::short_hash kERC20ContractAddress = ethereum::ConvertStrToEthAddress("0xd6B18EC3711e777F029B17283aa4ea6557493E35");

    ethereum::Settings settingsAlice;
    settingsAlice.m_secretWords = { "silly", "profit", "jewel", "fox", "evoke", "victory", "until", "topic", "century", "depth", "usual", "update" };
    settingsAlice.m_accountIndex = 3;
    settingsAlice.m_address = "127.0.0.1:7545";
    settingsAlice.m_shouldConnect = true;

    ethereum::Settings settingsBob;
    settingsBob.m_secretWords = { "silly", "profit", "jewel", "fox", "evoke", "victory", "until", "topic", "century", "depth", "usual", "update" };
    settingsBob.m_accountIndex = 4;
    settingsBob.m_address = "127.0.0.1:7545";
    settingsBob.m_shouldConnect = true;

    auto providerAlice = std::make_shared<ethereum::Provider>(settingsAlice);
    auto providerBob = std::make_shared<ethereum::Provider>(settingsBob);
    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);
    auto bridgeAlice = std::make_shared<ethereum::EthereumBridge>(*mainReactor, *providerAlice);
    auto bridgeBob = std::make_shared<ethereum::EthereumBridge>(*mainReactor, *providerBob);

    ECC::uintBig secret;
    ECC::GenRandom(secret);
    libbitcoin::ec_secret secretEC;
    std::move(std::begin(secret.m_pData), std::end(secret.m_pData), std::begin(secretEC));

    auto rawPk = libbitcoin::wallet::ec_private(secretEC, libbitcoin::wallet::ec_private::mainnet, false).to_public().encoded();
    LOG_DEBUG() << "PUBLIC: " << rawPk;
    libbitcoin::short_hash addressFromSecret = ethereum::GetEthAddressFromPubkeyStr(rawPk);
    auto participant = bridgeBob->generateEthAddress();
    auto initiator = bridgeAlice->generateEthAddress();

    LOG_DEBUG() << "participant: " << libbitcoin::encode_base16(participant);
    LOG_DEBUG() << "initiator: " << libbitcoin::encode_base16(initiator);
    LOG_DEBUG() << "addressFromSecret: " << libbitcoin::encode_base16(addressFromSecret);

    ECC::uintBig gas = 500000u;
    ECC::uintBig gasPrice = 3000000u;
    ECC::uintBig swapAmount = 1'00'000'000'000'000'000u;

    // TODO: call Token::approve before LockTx

    bridgeAlice->getBlockNumber([&](const ethereum::IBridge::Error& error, uint64_t blockCount)
        {
            // LockMethodHash + refundTimeInBlocks + addressFromSecret + participant + contractAddress + value
            ECC::uintBig refundTimeInBlocks = 4u + blockCount;
            libbitcoin::data_chunk lockData;
            lockData.reserve(ethereum::kEthContractMethodHashSize + 5 * ethereum::kEthContractABIWordSize);
            libbitcoin::decode_base16(lockData, ethereum::swap_contract::GetLockMethodHash(true, false));
            ethereum::AddContractABIWordToBuffer({ std::begin(refundTimeInBlocks.m_pData), std::end(refundTimeInBlocks.m_pData) }, lockData);
            ethereum::AddContractABIWordToBuffer(addressFromSecret, lockData);
            ethereum::AddContractABIWordToBuffer(participant, lockData);
            ethereum::AddContractABIWordToBuffer(kTokenContractAddress, lockData);
            ethereum::AddContractABIWordToBuffer({ std::begin(swapAmount.m_pData), std::end(swapAmount.m_pData) }, lockData);

            bridgeAlice->send(kERC20ContractAddress, lockData, ECC::Zero, gas, gasPrice, [&, refundTimeInBlocks](const ethereum::IBridge::Error&, std::string txHash, uint64_t)
                {
                    LOG_DEBUG() << "LOCK_TX hash: " << txHash;

                    libbitcoin::data_chunk hashData;
                    hashData.reserve(60);
                    hashData.insert(hashData.end(), addressFromSecret.cbegin(), addressFromSecret.cend());
                    hashData.insert(hashData.end(), participant.cbegin(), participant.cend());
                    hashData.insert(hashData.end(), initiator.cbegin(), initiator.cend());
                    hashData.insert(hashData.end(), std::cbegin(refundTimeInBlocks.m_pData), std::cend(refundTimeInBlocks.m_pData));
                    hashData.insert(hashData.end(), kTokenContractAddress.cbegin(), kTokenContractAddress.cend());
                    auto hash = ethash::keccak256(&hashData[0], hashData.size());

                    LOG_DEBUG() << "RedeemTx, hash of data: " << libbitcoin::encode_base16({ std::cbegin(hash.bytes), std::cend(hash.bytes) });

                    libbitcoin::hash_digest hashDigest;
                    std::move(std::begin(hash.bytes), std::end(hash.bytes), hashDigest.begin());
                    libbitcoin::recoverable_signature signature;
                    libbitcoin::sign_recoverable(signature, secretEC, hashDigest);

                    {
                        libbitcoin::data_chunk resultSign(std::begin(signature.signature), std::end(signature.signature));
                        resultSign.push_back(signature.recovery_id);
                        LOG_DEBUG() << "RedeemTx, signature: " << libbitcoin::encode_base16(resultSign);
                    }

                    // redeem
                    // kRedeemMethodHash + addressFromSecret + signature (r, s, v)
                    libbitcoin::data_chunk redeemData;
                    redeemData.reserve(ethereum::kEthContractMethodHashSize + 4 * ethereum::kEthContractABIWordSize);
                    libbitcoin::decode_base16(redeemData, ethereum::swap_contract::GetRedeemMethodHash(false));
                    ethereum::AddContractABIWordToBuffer(addressFromSecret, redeemData);
                    redeemData.insert(redeemData.end(), std::begin(signature.signature), std::end(signature.signature));
                    redeemData.insert(redeemData.end(), 31u, 0x00);
                    redeemData.push_back(signature.recovery_id + 27u);

                    bridgeBob->send(kERC20ContractAddress, redeemData, ECC::Zero, gas, gasPrice, [mainReactor](const ethereum::IBridge::Error&, std::string txHash, uint64_t)
                        {
                            LOG_DEBUG() << "REDEEM_TX hash: " << txHash;
                            mainReactor->stop();
                        });
                });
        });

    mainReactor->run();
}

void testERC20GetBalance()
{
    std::cout << "\nTesting call...\n";

    ethereum::Settings settings;
    settings.m_secretWords = { "silly", "profit", "jewel", "fox", "evoke", "victory", "until", "topic", "century", "depth", "usual", "update" };
    settings.m_accountIndex = 3;
    settings.m_address = "127.0.0.1:7545";
    settings.m_shouldConnect = true;

    auto provider = std::make_shared<ethereum::Provider>(settings);
    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);
    ethereum::EthereumBridge bridge(*mainReactor, *provider);

    const libbitcoin::short_hash kTokenContractAddress = ethereum::ConvertStrToEthAddress("0x4A2043c5625ec1E6759EA429C6FF8C02979e291E");
    libbitcoin::data_chunk data;
    data.reserve(ethereum::kEthContractMethodHashSize + 1 * ethereum::kEthContractABIWordSize);
    libbitcoin::decode_base16(data, ethereum::ERC20Hashes::kBalanceOfHash);
    ethereum::AddContractABIWordToBuffer(bridge.generateEthAddress(), data);

    bridge.call(kTokenContractAddress, libbitcoin::encode_base16(data), [mainReactor](const ethereum::IBridge::Error&, const nlohmann::json& result)
        {
            LOG_DEBUG() << result.dump(4);

            boost::multiprecision::uint256_t tmp(result.get<std::string>());
            tmp /= ethereum::GetCoinUnitsMultiplier(beam::wallet::AtomicSwapCoin::Dai);

            LOG_DEBUG() << "Balance: " << tmp.convert_to<Amount>();
            mainReactor->stop();
        });

    mainReactor->run();
}

void testMultipleSend()
{
    ethereum::Settings settingsAlice;
    settingsAlice.m_secretWords = { "silly", "profit", "jewel", "fox", "evoke", "victory", "until", "topic", "century", "depth", "usual", "update" };
    settingsAlice.m_accountIndex = 2;
    settingsAlice.m_address = "https://ropsten.infura.io/v3/{ID}";
    //settingsAlice.m_address = "127.0.0.1:7545";
    settingsAlice.m_shouldConnect = true;

    auto providerAlice = std::make_shared<ethereum::Provider>(settingsAlice);
    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);
    auto bridgeAlice = std::make_shared<ethereum::EthereumBridge>(*mainReactor, *providerAlice);
    auto initiator = bridgeAlice->generateEthAddress();

    ECC::uintBig gas = 25000u;
    ECC::uintBig gasPrice = 50'000'000'000u;

    bridgeAlice->send(initiator, {}, ECC::Zero, gas, gasPrice, [](const ethereum::IBridge::Error&, std::string txHash, uint64_t nonce)
        {
            LOG_DEBUG() << "First Tx hash: " << txHash << " nonce: " << nonce;
        });

    bridgeAlice->send(initiator, {}, ECC::Zero, gas, gasPrice, [](const ethereum::IBridge::Error&, std::string txHash, uint64_t nonce)
        {
            LOG_DEBUG() << "Second Tx hash: " << txHash << " nonce: " << nonce;
        });

    bridgeAlice->send(initiator, {}, ECC::Zero, gas, gasPrice, [](const ethereum::IBridge::Error&, std::string txHash, uint64_t nonce)
        {
            LOG_DEBUG() << "Third Tx hash: " << txHash << " nonce: " << nonce;
        });

    mainReactor->run();
}

void testERC20MultipleApprove()
{
    beam::wallet::AtomicSwapCoin token = beam::wallet::AtomicSwapCoin::Dai;
    const libbitcoin::short_hash kTokenContractAddress = ethereum::ConvertStrToEthAddress("0xe0a55f2c08125b7dac7132e02b043ED30359034E");
    const libbitcoin::short_hash kERC20ContractAddress = ethereum::ConvertStrToEthAddress("0xd6B18EC3711e777F029B17283aa4ea6557493E35");

    ethereum::Settings settingsAlice;
    settingsAlice.m_secretWords = { "silly", "profit", "jewel", "fox", "evoke", "victory", "until", "topic", "century", "depth", "usual", "update" };
    settingsAlice.m_accountIndex = 3;
    settingsAlice.m_address = "127.0.0.1:7545";
    settingsAlice.m_shouldConnect = true;

    auto providerAlice = std::make_shared<ethereum::Provider>(settingsAlice);
    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);
    auto bridgeAlice = std::make_shared<ethereum::EthereumBridge>(*mainReactor, *providerAlice);
    auto initiator = bridgeAlice->generateEthAddress();

    ECC::uintBig gas = 50000u;
    ECC::uintBig gasPrice = 50'000'000'000u;
    ECC::uintBig value = 1'000'000'000'000'000'000u;

    // get current allowance
    Amount startAllowance = 0;
    {
        libbitcoin::data_chunk data;
        data.reserve(ethereum::kEthContractMethodHashSize + 2 * ethereum::kEthContractABIWordSize);
        libbitcoin::decode_base16(data, ethereum::ERC20Hashes::kAllowanceHash);
        ethereum::AddContractABIWordToBuffer(bridgeAlice->generateEthAddress(), data);
        ethereum::AddContractABIWordToBuffer(kERC20ContractAddress, data);

        bridgeAlice->call(kTokenContractAddress, libbitcoin::encode_base16(data), [mainReactor, &startAllowance, token](const ethereum::IBridge::Error&, const nlohmann::json& result)
            {
                boost::multiprecision::uint256_t tmp(result.get<std::string>());
                tmp /= ethereum::GetCoinUnitsMultiplier(token);
                startAllowance = tmp.convert_to<Amount>();
                LOG_DEBUG() << "allowance: " << startAllowance;

                mainReactor->stop();
            });

        mainReactor->run();
    }

    // send multiple approveTx
    {
        bridgeAlice->erc20Approve(kTokenContractAddress, kERC20ContractAddress, value, gas, gasPrice, [](const ethereum::IBridge::Error&, std::string txHash)
            {
                LOG_DEBUG() << "First ApproveTx hash: " << txHash;
            });

        bridgeAlice->erc20Approve(kTokenContractAddress, kERC20ContractAddress, value, gas, gasPrice, [](const ethereum::IBridge::Error&, std::string txHash)
            {
                LOG_DEBUG() << "Second ApproveTx hash: " << txHash;
            });

        bridgeAlice->erc20Approve(kTokenContractAddress, kERC20ContractAddress, value, gas, gasPrice, [mainReactor](const ethereum::IBridge::Error&, std::string txHash)
            {
                LOG_DEBUG() << "Third ApproveTx hash: " << txHash;
                mainReactor->stop();
            });

        mainReactor->run();
    }

    // check allowance
    Amount endAllowance = 0;
    {
        libbitcoin::data_chunk data;
        data.reserve(ethereum::kEthContractMethodHashSize + 2 * ethereum::kEthContractABIWordSize);
        libbitcoin::decode_base16(data, ethereum::ERC20Hashes::kAllowanceHash);
        ethereum::AddContractABIWordToBuffer(bridgeAlice->generateEthAddress(), data);
        ethereum::AddContractABIWordToBuffer(kERC20ContractAddress, data);

        bridgeAlice->call(kTokenContractAddress, libbitcoin::encode_base16(data), [mainReactor, &endAllowance, token](const ethereum::IBridge::Error&, const nlohmann::json& result)
            {
                boost::multiprecision::uint256_t tmp(result.get<std::string>());
                tmp /= ethereum::GetCoinUnitsMultiplier(token);
                endAllowance = tmp.convert_to<Amount>();
                LOG_DEBUG() << "end allowance: " << tmp.convert_to<Amount>();

                mainReactor->stop();
            });

        mainReactor->run();
    }

    std::string hex = beam::ethereum::AddHexPrefix(value.str());
    boost::multiprecision::uint256_t tmp(hex);
    tmp /= ethereum::GetCoinUnitsMultiplier(token);
    auto expectedAllowance = startAllowance + (3 * tmp).convert_to<Amount>();
    WALLET_CHECK(endAllowance == expectedAllowance);
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
    testERC20Swap();
    testERC20SwapWithAggregateSignature();

    testERC20GetBalance();
    testERC20MultipleApprove();

    testMultipleSend();

    assert(g_failureCount == 0);
    return WALLET_CHECK_RESULT;
}
