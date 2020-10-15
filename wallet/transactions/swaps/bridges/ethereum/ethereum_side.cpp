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

#pragma once

#include "ethereum_side.h"

#include <ethash/keccak.hpp>

#include "common.h"
#include "utility/logger.h"

using namespace ECC;

namespace
{
    // TODO: check
    constexpr uint32_t kExternalHeightMaxDifference = 10;

    std::string GetRefundMethodHash(bool isHashLockScheme)
    {
        return isHashLockScheme ? "7249fbb6" : "fa89401a";
    }

    std::string GetLockMethodHash(bool isHashLockScheme)
    {
        return isHashLockScheme ? "ae052147" : "bc18cc34";
    }

    std::string GetRedeemMethodHash(bool isHashLockScheme)
    {
        return isHashLockScheme ? "b31597ad" : "8772acd6";
    }

    std::string GetDetailsMethodHash(bool isHashLockScheme)
    {
        return isHashLockScheme ? "6bfec360" : "7cf3285f";
    }

    // TODO: -> settings
    constexpr bool kIsHashLockScheme = false;
}

namespace beam::wallet
{
EthereumSide::EthereumSide(BaseTransaction& tx, ethereum::IBridge::Ptr ethBridge, ethereum::ISettingsProvider& settingsProvider, bool isBeamSide)
    : m_tx(tx),
      m_ethBridge(ethBridge),
      m_settingsProvider(settingsProvider),
      m_isEthOwner(!isBeamSide)
{
}

EthereumSide::~EthereumSide()
{}

bool EthereumSide::Initialize()
{
    if (!m_blockCount)
    {
        GetBlockCount(true);
        return false;
    }

    if (m_isEthOwner)
    {
        InitSecret();
    }
    // InitLocalKeys - ? init publicSwap & secretSwap keys
    m_tx.SetParameter(TxParameterID::AtomicSwapPublicKey, ethereum::ConvertEthAddressToStr(m_ethBridge->generateEthAddress()));

    return true;
}

bool EthereumSide::InitLockTime()
{
    auto height = m_blockCount;
    assert(height);

    LOG_DEBUG() << "InitLockTime height = " << height;

    auto externalLockPeriod = height + GetLockTimeInBlocks();
    m_tx.SetParameter(TxParameterID::AtomicSwapExternalLockTime, externalLockPeriod);

    return true;
}

bool EthereumSide::ValidateLockTime()
{
    auto height = m_blockCount;
    assert(height);
    auto externalLockTime = m_tx.GetMandatoryParameter<Height>(TxParameterID::AtomicSwapExternalLockTime);

    LOG_DEBUG() << "ValidateLockTime height = " << height << " external = " << externalLockTime;

    if (externalLockTime <= height)
    {
        return false;
    }

    double blocksPerBeamBlock = GetBlocksPerHour() / beam::Rules::get().DA.Target_s;
    Height beamCurrentHeight = m_tx.GetWalletDB()->getCurrentHeight();
    Height beamHeightDiff = beamCurrentHeight - m_tx.GetMandatoryParameter<Height>(TxParameterID::MinHeight, SubTxIndex::BEAM_LOCK_TX);

    Height peerMinHeight = externalLockTime - GetLockTimeInBlocks();
    Height peerEstCurrentHeight = peerMinHeight + static_cast<Height>(std::ceil(blocksPerBeamBlock * beamHeightDiff));

    return peerEstCurrentHeight >= height - kExternalHeightMaxDifference
        && peerEstCurrentHeight <= height + kExternalHeightMaxDifference;
}

void EthereumSide::AddTxDetails(SetTxParameter& txParameters)
{
    txParameters.AddParameter(TxParameterID::AtomicSwapPeerPublicKey, ethereum::ConvertEthAddressToStr(m_ethBridge->generateEthAddress()));
}

bool EthereumSide::ConfirmLockTx()
{
    if (m_isEthOwner)
    {
        return true;
    }

    if (!m_SwapLockTxBlockNumber)
    {
        auto secretHash = GetSecretHash();
        libbitcoin::data_chunk data;
        ethereum::AddContractABIWordToBuffer(secretHash, data);

        m_ethBridge->call(GetContractAddress(),
            GetDetailsMethodHash(kIsHashLockScheme) + libbitcoin::encode_base16(data),
            [this, weak = this->weak_from_this()](const ethereum::IBridge::Error& error, const nlohmann::json& result)
        {
            if (weak.expired())
            {
                return;
            }

            if (error.m_type != ethereum::IBridge::None)
            {
                m_tx.UpdateOnNextTip();
                return;
            }

            std::string resultStr = result.get<std::string>();

            if (std::all_of(resultStr.begin() + 2, resultStr.end(), [](const char& c) { return c == '0';}))
            {
                // lockTx isn't ready yet
                return;
            }

            uintBig swapAmount = m_tx.GetMandatoryParameter<uintBig>(TxParameterID::AtomicSwapEthAmount);
            auto resultData = beam::from_hex(std::string(resultStr.begin() + 2, resultStr.end()));
            ECC::uintBig amount = Zero;
            std::move(resultData.begin() + 32, resultData.end(), std::begin(amount.m_pData));

            if (amount != swapAmount)
            {
                LOG_ERROR() << m_tx.GetTxID() << "[" << static_cast<SubTxID>(SubTxIndex::LOCK_TX) << "]"
                    << " Unexpected amount, expected: " << swapAmount.str() << ", got: " << amount.str();
                m_tx.SetParameter(TxParameterID::InternalFailureReason, TxFailureReason::SwapInvalidAmount, false, SubTxIndex::LOCK_TX);
                m_tx.UpdateAsync();
                return;
            }

            libbitcoin::data_chunk data;
            std::copy(resultData.begin(), resultData.begin() + 32, std::back_inserter(data));
            std::string st = libbitcoin::encode_base16(data);
            m_SwapLockTxBlockNumber = std::stoull(st, nullptr, 16);
            m_tx.UpdateAsync();
        });
        return false;
    }

    uint64_t currentBlockNumber = GetBlockCount();

    if (m_SwapLockTxBlockNumber < currentBlockNumber)
    {
        m_SwapLockTxConfirmations = currentBlockNumber - m_SwapLockTxBlockNumber;
    }
    
    if (m_SwapLockTxConfirmations < GetTxMinConfirmations())
    {
        return false;
    }

    return true;
}

bool EthereumSide::ConfirmRefundTx()
{
    return ConfirmWithdrawTx(SubTxIndex::REFUND_TX);
}

bool EthereumSide::ConfirmRedeemTx()
{
    return ConfirmWithdrawTx(SubTxIndex::REDEEM_TX);
}

bool EthereumSide::ConfirmWithdrawTx(SubTxID subTxID)
{
    std::string txID;
    if (!m_tx.GetParameter(TxParameterID::AtomicSwapExternalTxID, txID, subTxID))
        return false;

    if (m_WithdrawTxConfirmations < GetTxMinConfirmations())
    {
        GetWithdrawTxConfirmations(subTxID);
        return false;
    }

    return true;
}

void EthereumSide::GetWithdrawTxConfirmations(SubTxID subTxID)
{
    if (!m_WithdrawTxBlockNumber)
    {
        // getTransactionReceipt
            std::string txID = m_tx.GetMandatoryParameter<std::string>(TxParameterID::AtomicSwapExternalTxID, subTxID);

        m_ethBridge->getTxBlockNumber(txID, [this, weak = this->weak_from_this()](const ethereum::IBridge::Error& error, uint64_t txBlockNumber)
        {
            if (weak.expired())
            {
                return;
            }

            if (error.m_type != ethereum::IBridge::None)
            {
                m_tx.UpdateOnNextTip();
                return;
            }

            m_WithdrawTxBlockNumber = txBlockNumber;
        });
        return;
    }

    auto currentBlockCount = GetBlockCount();
    if (currentBlockCount >= m_WithdrawTxBlockNumber)
    {
        m_WithdrawTxConfirmations = currentBlockCount - m_WithdrawTxBlockNumber;
    }
}

bool EthereumSide::SendLockTx()
{
    auto secretHash = GetSecretHash();
    auto participantStr = m_tx.GetMandatoryParameter<std::string>(TxParameterID::AtomicSwapPeerPublicKey);
    auto participant = ethereum::ConvertStrToEthAddress(participantStr);
    uintBig refundTimeInBlocks = GetLockTimeInBlocks();

    // LockMethodHash + refundTimeInBlocks + hashedSecret/addressFromSecret + participant
    libbitcoin::data_chunk data;
    data.reserve(ethereum::kEthContractMethodHashSize + 3 * ethereum::kEthContractABIWordSize);
    libbitcoin::decode_base16(data, GetLockMethodHash(kIsHashLockScheme));
    ethereum::AddContractABIWordToBuffer({std::begin(refundTimeInBlocks.m_pData), std::end(refundTimeInBlocks.m_pData)}, data);
    ethereum::AddContractABIWordToBuffer(secretHash, data);
    ethereum::AddContractABIWordToBuffer(participant, data);

    uintBig swapAmount = m_tx.GetMandatoryParameter<uintBig>(TxParameterID::AtomicSwapEthAmount);
    m_ethBridge->send(GetContractAddress(), data, swapAmount, GetGas(SubTxIndex::LOCK_TX), GetGasPrice(SubTxIndex::LOCK_TX),
        [this, weak = this->weak_from_this()](const ethereum::IBridge::Error& error, std::string txHash)
    {
        if (!weak.expired())
        {
            if (error.m_type != ethereum::IBridge::None)
            {
                // TODO: handle error
                return;
            }

            m_tx.SetParameter(TxParameterID::AtomicSwapExternalTxID, txHash, false, SubTxIndex::LOCK_TX);
            m_tx.UpdateAsync();
        }
    });
    return true;
}

bool EthereumSide::SendRefund()
{
    // TODO: check
    std::string txID;
    if (m_isWithdrawTxSent || m_tx.GetParameter(TxParameterID::AtomicSwapExternalTxID, txID, SubTxIndex::REFUND_TX))
        return true;

    auto secretHash = GetSecretHash();

    // kRefundMethodHash + secretHash/addressFromSecret
    libbitcoin::data_chunk data;
    data.reserve(4 + 32);
    libbitcoin::decode_base16(data, GetRefundMethodHash(kIsHashLockScheme));
    ethereum::AddContractABIWordToBuffer(secretHash, data);

    m_ethBridge->send(GetContractAddress(), data, ECC::Zero, GetGas(SubTxIndex::REFUND_TX), GetGasPrice(SubTxIndex::REFUND_TX),
        [this, weak = this->weak_from_this()](const ethereum::IBridge::Error& error, std::string txHash)
    {
        if (!weak.expired())
        {
            OnSentWithdrawTx(SubTxIndex::REFUND_TX, error, txHash);
        }
    });
    m_isWithdrawTxSent = true;
    return true;
}

bool EthereumSide::SendRedeem()
{
    // TODO: check
    std::string txID;
    if (m_isWithdrawTxSent || m_tx.GetParameter(TxParameterID::AtomicSwapExternalTxID, txID, SubTxIndex::REDEEM_TX))
        return true;

    auto secretHash = GetSecretHash();
    auto secret = GetSecret();

    libbitcoin::data_chunk data;
    if (kIsHashLockScheme)
    {
        // kRedeemMethodHash + secret + secretHash
        data.reserve(4 + 32 + 32);
        libbitcoin::decode_base16(data, GetRedeemMethodHash(kIsHashLockScheme));
        data.insert(data.end(), std::begin(secret.m_pData), std::end(secret.m_pData));
        data.insert(data.end(), std::begin(secretHash), std::end(secretHash));
    }
    else
    {
        auto initiatorStr = m_tx.GetMandatoryParameter<std::string>(TxParameterID::AtomicSwapPeerPublicKey);
        auto initiator = ethereum::ConvertStrToEthAddress(initiatorStr);
        auto participant = m_ethBridge->generateEthAddress();

        // keccak256: addressFromSecret + participant + initiator
        libbitcoin::data_chunk hashData;
        hashData.reserve(60);
        hashData.insert(hashData.end(), secretHash.cbegin(), secretHash.cend());
        hashData.insert(hashData.end(), participant.cbegin(), participant.cend());
        hashData.insert(hashData.end(), initiator.cbegin(), initiator.cend());
        auto hash = ethash::keccak256(&hashData[0], hashData.size());

        libbitcoin::hash_digest hashDigest;
        std::move(std::begin(hash.bytes), std::end(hash.bytes), hashDigest.begin());
        libbitcoin::ec_secret secretEC;
        std::move(std::begin(secret.m_pData), std::end(secret.m_pData), std::begin(secretEC));

        libbitcoin::recoverable_signature signature;
        libbitcoin::sign_recoverable(signature, secretEC, hashDigest);

        // kRedeemMethodHash + addressFromSecret + signature (r, s, v)
        data.reserve(ethereum::kEthContractMethodHashSize + 4 * ethereum::kEthContractABIWordSize);
        libbitcoin::decode_base16(data, GetRedeemMethodHash(kIsHashLockScheme));
        ethereum::AddContractABIWordToBuffer(secretHash, data);
        data.insert(data.end(), std::begin(signature.signature), std::end(signature.signature));
        data.insert(data.end(), 31u, 0x00);
        data.push_back(signature.recovery_id + 27u);
    }

    m_ethBridge->send(GetContractAddress(), data, ECC::Zero, GetGas(SubTxIndex::REDEEM_TX), GetGasPrice(SubTxIndex::REDEEM_TX),
        [this, weak = this->weak_from_this()](const ethereum::IBridge::Error& error, std::string txHash)
    {
        if (!weak.expired())
        {
            OnSentWithdrawTx(SubTxIndex::REDEEM_TX, error, txHash);
        }
    });
    m_isWithdrawTxSent = true;
    return true;
}

void EthereumSide::OnSentWithdrawTx(SubTxID subTxID, const ethereum::IBridge::Error& error, const std::string& txHash)
{
    if (error.m_type != ethereum::IBridge::None)
    {
        // TODO: handle error
        m_isWithdrawTxSent = false;
        return;
    }

    m_tx.SetParameter(TxParameterID::Confirmations, uint32_t(0), false, subTxID);
    m_tx.SetParameter(TxParameterID::AtomicSwapExternalTxID, txHash, false, subTxID);
    m_tx.UpdateAsync();
}

bool EthereumSide::IsLockTimeExpired()
{
    uint64_t height = GetBlockCount();
    uint64_t lockHeight = 0;
    m_tx.GetParameter(TxParameterID::AtomicSwapExternalLockTime, lockHeight);

    return height >= lockHeight;
}

bool EthereumSide::HasEnoughTimeToProcessLockTx()
{
    Height lockTxMaxHeight = MaxHeight;
    if (m_tx.GetParameter(TxParameterID::MaxHeight, lockTxMaxHeight, SubTxIndex::BEAM_LOCK_TX))
    {
        Block::SystemState::Full systemState;
        if (m_tx.GetTip(systemState) && systemState.m_Height > lockTxMaxHeight - GetLockTxEstimatedTimeInBeamBlocks())
        {
            return false;
        }
    }
    return true;
}

bool EthereumSide::IsQuickRefundAvailable()
{
    return false;
}

uint64_t EthereumSide::GetBlockCount(bool notify)
{
    m_ethBridge->getBlockNumber([this, weak = this->weak_from_this(), notify](const ethereum::IBridge::Error& error, uint64_t blockCount)
    {
        if (!weak.expired())
        {
            if (error.m_type != ethereum::IBridge::None)
            {
                m_tx.UpdateOnNextTip();
                return;
            }

            if (blockCount != m_blockCount)
            {
                m_blockCount = blockCount;
                m_tx.SetParameter(TxParameterID::AtomicSwapExternalHeight, m_blockCount, true);

                if (notify)
                {
                    m_tx.UpdateAsync();
                }
            }
        }
    });
    return m_blockCount;
}

void EthereumSide::InitSecret()
{
    NoLeak<uintBig> secret;
    GenRandom(secret.V);
    if (kIsHashLockScheme)
    {
        m_tx.SetParameter(TxParameterID::PreImage, secret.V, false, BEAM_REDEEM_TX);
    }
    else
    {
        m_tx.SetParameter(TxParameterID::AtomicSwapSecretPrivateKey, secret.V, false, BEAM_REDEEM_TX);
    }
}

uint16_t EthereumSide::GetTxMinConfirmations() const
{
    return m_settingsProvider.GetSettings().GetTxMinConfirmations();
}

uint32_t EthereumSide::GetLockTimeInBlocks() const
{
    return m_settingsProvider.GetSettings().GetLockTimeInBlocks();
}

double EthereumSide::GetBlocksPerHour() const
{
    return m_settingsProvider.GetSettings().GetBlocksPerHour();
}

uint32_t EthereumSide::GetLockTxEstimatedTimeInBeamBlocks() const
{
    // TODO: check
    return 10;
}

ECC::uintBig EthereumSide::GetSecret() const
{
    if (kIsHashLockScheme)
    {
        return m_tx.GetMandatoryParameter<Hash::Value>(TxParameterID::PreImage, SubTxIndex::BEAM_REDEEM_TX);
    }
    else
    {
        return m_tx.GetMandatoryParameter<uintBig>(TxParameterID::AtomicSwapSecretPrivateKey, SubTxIndex::BEAM_REDEEM_TX);
    }
}

ByteBuffer EthereumSide::GetSecretHash() const
{
    if (kIsHashLockScheme)
    {
        Hash::Value lockImage(Zero);

        if (NoLeak<uintBig> secret; m_tx.GetParameter(TxParameterID::PreImage, secret.V, SubTxIndex::BEAM_REDEEM_TX))
        {
            Hash::Processor() << secret.V >> lockImage;
        }
        else
        {
            lockImage = m_tx.GetMandatoryParameter<uintBig>(TxParameterID::PeerLockImage, SubTxIndex::BEAM_REDEEM_TX);
        }

        return libbitcoin::to_chunk(lockImage.m_pData);
    }
    else
    {
        libbitcoin::wallet::ec_public secretPublicKey;

        if (m_isEthOwner)
        {
            NoLeak<uintBig> secretPrivateKey;
            m_tx.GetParameter(TxParameterID::AtomicSwapSecretPrivateKey, secretPrivateKey.V, SubTxIndex::BEAM_REDEEM_TX);

            libbitcoin::ec_secret secret;
            std::copy(std::begin(secretPrivateKey.V.m_pData), std::end(secretPrivateKey.V.m_pData), secret.begin());
            libbitcoin::wallet::ec_private privateKey(secret, libbitcoin::wallet::ec_private::mainnet, false);

            secretPublicKey = privateKey.to_public();
        }
        else
        {
            Point publicKeyPoint = m_tx.GetMandatoryParameter<Point>(TxParameterID::AtomicSwapSecretPublicKey, SubTxIndex::BEAM_REDEEM_TX);
            auto publicKeyRaw = SerializePubkey(ConvertPointToPubkey(publicKeyPoint));
            secretPublicKey = libbitcoin::wallet::ec_public(publicKeyRaw);
        }
        libbitcoin::short_hash addressFromSecret = ethereum::GetEthAddressFromPubkeyStr(secretPublicKey.encoded());

        return ByteBuffer(addressFromSecret.begin(), addressFromSecret.end());
    }
}

ECC::uintBig EthereumSide::GetGas(SubTxID subTxID) const
{
    if (subTxID == SubTxIndex::LOCK_TX)
    {
        return m_settingsProvider.GetSettings().m_lockTxGasLimit;
    }

    return m_settingsProvider.GetSettings().m_withdrawTxGasLimit;
}

ECC::uintBig EthereumSide::GetGasPrice(SubTxID subTxID) const
{
    return m_tx.GetMandatoryParameter<ECC::uintBig>(TxParameterID::AtomicSwapGasPrice, subTxID);
}

libbitcoin::short_hash EthereumSide::GetContractAddress() const
{
    return ethereum::ConvertStrToEthAddress(m_settingsProvider.GetSettings().GetContractAddress());
}

} // namespace beam::wallet