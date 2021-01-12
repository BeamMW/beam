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

#include "ethereum_side.h"
#include <bitcoin/bitcoin.hpp>
#include <ethash/keccak.hpp>

#include "common.h"
#include "utility/logger.h"

using namespace ECC;

namespace
{
    // TODO: check
    constexpr uint32_t kExternalHeightMaxDifference = 10;
}

namespace beam::wallet
{
EthereumSide::EthereumSide(BaseTransaction& tx, ethereum::IBridge::Ptr ethBridge, ethereum::ISettingsProvider& settingsProvider, bool isBeamSide)
    : m_tx(tx)
    , m_ethBridge(ethBridge)
    , m_settingsProvider(settingsProvider)
    , m_isEthOwner(!isBeamSide)
{
    m_settingsProvider.AddRef();
}

EthereumSide::~EthereumSide()
{
    m_settingsProvider.ReleaseRef();
}

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
    auto txID = m_tx.GetMandatoryParameter<std::string>(TxParameterID::AtomicSwapExternalTxID, SubTxIndex::LOCK_TX);
    txParameters.AddParameter(TxParameterID::AtomicSwapPeerPublicKey, ethereum::ConvertEthAddressToStr(m_ethBridge->generateEthAddress()));
    txParameters.AddParameter(TxParameterID::SubTxIndex, SubTxIndex::LOCK_TX);
    txParameters.AddParameter(TxParameterID::AtomicSwapExternalTxID, txID);
}

bool EthereumSide::ConfirmLockTx()
{
    if (m_isEthOwner)
    {
        return true;
    }

    // wait TxID from peer
    std::string txHash;
    if (!m_tx.GetParameter(TxParameterID::AtomicSwapExternalTxID, txHash, SubTxIndex::LOCK_TX))
        return false;

    if (!m_SwapLockTxBlockNumber)
    {
        // validate contract
        m_ethBridge->getTxByHash(txHash, [this, weak = this->weak_from_this()] (const ethereum::IBridge::Error& error, const nlohmann::json& txInfo)
        {
            if (weak.expired())
            {
                return;
            }

            if (error.m_type != ethereum::IBridge::None)
            {
                LOG_DEBUG() << m_tx.GetTxID() << "[" << static_cast<SubTxID>(SubTxIndex::LOCK_TX) << "]" << " Failed to get transaction: " << error.m_message;
                return;
            }

            try
            {
                auto contractAddrStr = txInfo["to"].get<std::string>();
                auto localContractAddrStr = GetContractAddressStr();
                std::transform(localContractAddrStr.begin(),
                               localContractAddrStr.end(),
                               localContractAddrStr.begin(),
                               [](char c) -> char { return static_cast<char>(std::tolower(c)); });

                if (contractAddrStr != localContractAddrStr)
                {
                    m_tx.SetParameter(TxParameterID::InternalFailureReason, TxFailureReason::SwapInvalidContract, false, SubTxIndex::LOCK_TX);
                    m_tx.UpdateAsync();
                    return;
                }

                std::string strValue = ethereum::RemoveHexPrefix(txInfo["value"].get<std::string>());
                auto amount = ethereum::ConvertStrToUintBig(strValue);
                uintBig swapAmount = IsERC20Token() ? ECC::Zero : GetSwapAmount();

                if (amount != swapAmount)
                {
                    LOG_ERROR() << m_tx.GetTxID() << "[" << static_cast<SubTxID>(SubTxIndex::LOCK_TX) << "]"
                        << " Unexpected amount, expected: " << swapAmount.str() << ", got: " << amount.str();
                    m_tx.SetParameter(TxParameterID::InternalFailureReason, TxFailureReason::SwapInvalidAmount, false, SubTxIndex::LOCK_TX);
                    m_tx.UpdateAsync();
                    return;
                }

                auto txInputStr = ethereum::RemoveHexPrefix(txInfo["input"].get<std::string>());
                libbitcoin::data_chunk data = BuildLockTxData();
                auto lockTxDataStr = libbitcoin::encode_base16(data);

                if (txInputStr != lockTxDataStr)
                {
                    LOG_ERROR() << m_tx.GetTxID() << "[" << static_cast<SubTxID>(SubTxIndex::LOCK_TX) << "]"
                        << " Transaction data does not match.";
                    m_tx.SetParameter(TxParameterID::InternalFailureReason, TxFailureReason::SwapInvalidContract, false, SubTxIndex::LOCK_TX);
                    m_tx.UpdateAsync();
                    return;
                }
            }
            catch (const std::exception& ex)
            {
                LOG_ERROR() << m_tx.GetTxID() << "[" << static_cast<SubTxID>(SubTxIndex::LOCK_TX) << "]" << " Failed to parse txInfo: " << ex.what();
                m_tx.SetParameter(TxParameterID::InternalFailureReason, TxFailureReason::SwapFormatResponseError, false, SubTxIndex::LOCK_TX);
                m_tx.UpdateAsync();
                return;
            }
        });

        // get block number of transaction and check status of transaction
        m_ethBridge->getTxBlockNumber(txHash, [this, weak = this->weak_from_this()](const ethereum::IBridge::Error& error, uint64_t txBlockNumber)
        {
            if (weak.expired())
            {
                return;
            }

            switch (error.m_type)
            {
            case ethereum::IBridge::ErrorType::None:
                break;
            case ethereum::IBridge::ErrorType::EthError:
            {
                LOG_ERROR() << m_tx.GetTxID() << "[" << static_cast<SubTxID>(SubTxIndex::LOCK_TX) << "]" << " Transaction is not valid.";
                m_tx.SetParameter(TxParameterID::InternalFailureReason, TxFailureReason::InvalidTransaction, false, SubTxIndex::LOCK_TX);
                m_tx.UpdateAsync();
                return;
            }
            default:
            {
                m_tx.UpdateOnNextTip();
                return;
            }
            }

            m_SwapLockTxBlockNumber = txBlockNumber;
        });

        return false;
    }

    uint64_t currentBlockNumber = GetBlockCount();

    if (m_SwapLockTxBlockNumber < currentBlockNumber)
    {
        uint32_t confirmations = static_cast<uint32_t>(currentBlockNumber - m_SwapLockTxBlockNumber);
        if (confirmations != m_SwapLockTxConfirmations)
        {
            m_SwapLockTxConfirmations = confirmations;
            LOG_DEBUG() << m_tx.GetTxID() << "[" << static_cast<SubTxID>(SubTxIndex::LOCK_TX) << "] " << confirmations << "/"
                << GetTxMinConfirmations(SubTxIndex::LOCK_TX) << " confirmations are received.";
            m_tx.SetParameter(TxParameterID::Confirmations, confirmations, true, SubTxIndex::LOCK_TX);
        }
    }
    
    return m_SwapLockTxConfirmations >= GetTxMinConfirmations(SubTxIndex::LOCK_TX);
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

    if (m_WithdrawTxConfirmations < GetTxMinConfirmations(subTxID))
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
        std::string txID = m_tx.GetMandatoryParameter<std::string>(TxParameterID::AtomicSwapExternalTxID, subTxID);

        m_ethBridge->getTxBlockNumber(txID, [this, weak = this->weak_from_this()](const ethereum::IBridge::Error& error, uint64_t txBlockNumber)
        {
            if (weak.expired())
            {
                return;
            }

            if (error.m_type != ethereum::IBridge::None)
            {
                return;
            }

            m_WithdrawTxBlockNumber = txBlockNumber;
        });
        return;
    }

    auto currentBlockCount = GetBlockCount();
    if (currentBlockCount >= m_WithdrawTxBlockNumber)
    {
        uint32_t confirmations = static_cast<uint32_t>(currentBlockCount - m_WithdrawTxBlockNumber);
        if (confirmations != m_WithdrawTxConfirmations)
        {
            m_WithdrawTxConfirmations = confirmations;
            LOG_DEBUG() << m_tx.GetTxID() << "[" << subTxID << "] " << confirmations << "/"
                << GetTxMinConfirmations(subTxID) << " confirmations are received.";
            m_tx.SetParameter(TxParameterID::Confirmations, confirmations, true, subTxID);
        }
    }
}

bool EthereumSide::SendLockTx()
{
    SwapTxState swapTxState = SwapTxState::Initial;
    bool stateExist = m_tx.GetParameter(TxParameterID::State, swapTxState, SubTxIndex::LOCK_TX);

    if (swapTxState == SwapTxState::Constructed)
        return true;

    std::string txID;
    if (m_tx.GetParameter(TxParameterID::AtomicSwapExternalTxID, txID, SubTxIndex::LOCK_TX))
    {
        // check status of transaction
        m_ethBridge->getTxBlockNumber(txID, [this, weak = this->weak_from_this()](const ethereum::IBridge::Error& error, uint64_t txBlockNumber)
        {
            if (weak.expired())
            {
                return;
            }

            switch (error.m_type)
            {
            case ethereum::IBridge::ErrorType::None:
                break;
            case ethereum::IBridge::ErrorType::EthError:
            {
                LOG_ERROR() << m_tx.GetTxID() << "[" << static_cast<SubTxID>(SubTxIndex::LOCK_TX) << "]" << " Transaction is not valid.";
                m_tx.SetParameter(TxParameterID::InternalFailureReason, TxFailureReason::InvalidTransaction, false, SubTxIndex::LOCK_TX);
                m_tx.UpdateAsync();
                return;
            }
            default:
            {
                m_tx.UpdateOnNextTip();
                return;
            }
            }

            m_tx.SetState(SwapTxState::Constructed, SubTxIndex::LOCK_TX);
            m_tx.UpdateAsync();
        });
        return false;
    }

    if (IsERC20Token())
    {
        if (!stateExist)
        {
            // ERC20::approve + swapContractAddress + value
            uintBig swapAmount = GetSwapAmount();
            libbitcoin::data_chunk data;
            data.reserve(ethereum::kEthContractMethodHashSize + 2 * ethereum::kEthContractABIWordSize);
            libbitcoin::decode_base16(data, ethereum::ERC20Hashes::kApproveHash);
            ethereum::AddContractABIWordToBuffer(GetContractAddress(), data);
            ethereum::AddContractABIWordToBuffer({ std::begin(swapAmount.m_pData), std::end(swapAmount.m_pData) }, data);

            auto swapCoin = m_tx.GetMandatoryParameter<AtomicSwapCoin>(TxParameterID::AtomicSwapCoin);
            const auto tokenContractAddress = ethereum::ConvertStrToEthAddress(m_settingsProvider.GetSettings().GetTokenContractAddress(swapCoin));

            // TODO(alex.starun): use different gaslimit
            m_ethBridge->erc20Approve(tokenContractAddress, GetContractAddress(), swapAmount, GetGas(SubTxIndex::LOCK_TX), GetGasPrice(SubTxIndex::LOCK_TX),
                [this, weak = this->weak_from_this()](const ethereum::IBridge::Error& error, std::string txHash)
            {
                if (!weak.expired())
                {
                    if (error.m_type != ethereum::IBridge::None)
                    {
                        LOG_DEBUG() << m_tx.GetTxID() << "[" << static_cast<SubTxID>(SubTxIndex::LOCK_TX) << "]" << " Failed to call ERC20::approve!";

                        if (error.m_type == ethereum::IBridge::EthError ||
                            error.m_type == ethereum::IBridge::InvalidResultFormat)
                        {
                            SetTxError(error, SubTxIndex::LOCK_TX);
                        }
                        m_tx.UpdateOnNextTip();
                        return;
                    }

                    m_tx.SetState(SwapTxState::CreatingTx, SubTxIndex::LOCK_TX);
                    m_tx.UpdateAsync();
                }
            });
            m_tx.SetState(SwapTxState::Initial, SubTxIndex::LOCK_TX);
            return false;
        }

        // waiting result of ERC20::approve
        if (swapTxState == SwapTxState::Initial)
        {
            return false;
        }
    }

    if (swapTxState != SwapTxState::Constructed)
    {
        libbitcoin::data_chunk data = BuildLockTxData();
        uintBig swapAmount = IsERC20Token() ? ECC::Zero : GetSwapAmount();

        m_ethBridge->send(GetContractAddress(), data, swapAmount, GetGas(SubTxIndex::LOCK_TX), GetGasPrice(SubTxIndex::LOCK_TX),
            [this, weak = this->weak_from_this()](const ethereum::IBridge::Error& error, std::string txHash, uint64_t txNonce)
        {
            if (!weak.expired())
            {
                if (error.m_type != ethereum::IBridge::None)
                {
                    if (error.m_type == ethereum::IBridge::EthError ||
                        error.m_type == ethereum::IBridge::InvalidResultFormat)
                    {
                        SetTxError(error, SubTxIndex::LOCK_TX);
                    }
                    m_tx.UpdateOnNextTip();
                    return;
                }

                m_tx.SetParameter(TxParameterID::AtomicSwapExternalTxID, txHash, false, SubTxIndex::LOCK_TX);
                m_tx.SetParameter(TxParameterID::NonceSlot, txNonce, false, SubTxIndex::LOCK_TX);
                m_tx.UpdateAsync();
            }
        });
    }
    return false;
}

bool EthereumSide::SendRefund()
{
    return SendWithdrawTx(SubTxIndex::REFUND_TX);
}

bool EthereumSide::SendRedeem()
{
    return SendWithdrawTx(SubTxIndex::REDEEM_TX);
}

bool EthereumSide::SendWithdrawTx(SubTxID subTxID)
{
    std::string txID;
    if (m_tx.GetParameter(TxParameterID::AtomicSwapExternalTxID, txID, subTxID))
        return true;

    if (!m_isWithdrawTxSent)
    {
        auto data = BuildWithdrawTxData(subTxID);

        m_ethBridge->send(GetContractAddress(), data, ECC::Zero, GetGas(subTxID), GetGasPrice(subTxID),
            [this, weak = this->weak_from_this(), subTxID](const ethereum::IBridge::Error& error, std::string txHash, uint64_t txNonce)
        {
            if (!weak.expired())
            {
                if (error.m_type != ethereum::IBridge::None)
                {
                    LOG_DEBUG() << m_tx.GetTxID() << "[" << subTxID << "]" << " Failed to send withdraw TX: " << error.m_message;
                    m_isWithdrawTxSent = false;
                    // trying to resend
                    m_tx.UpdateOnNextTip();
                    return;
                }

                m_tx.SetParameter(TxParameterID::Confirmations, uint32_t(0), false, subTxID);
                m_tx.SetParameter(TxParameterID::AtomicSwapExternalTxID, txHash, false, subTxID);
                m_tx.SetParameter(TxParameterID::NonceSlot, txNonce, false, subTxID);
                m_tx.UpdateAsync();
            }
        });
        m_isWithdrawTxSent = true;
    }
    return false;
}

beam::ByteBuffer EthereumSide::BuildWithdrawTxData(SubTxID subTxID)
{
    return (subTxID == SubTxIndex::REDEEM_TX) ? BuildRedeemTxData() : BuildRefundTxData();
}

beam::ByteBuffer EthereumSide::BuildRedeemTxData()
{
    auto secretHash = GetSecretHash();
    auto secret = GetSecret();

    libbitcoin::data_chunk data;
    if (IsHashLockScheme())
    {
        // kRedeemMethodHash + secret + secretHash
        data.reserve(ethereum::kEthContractMethodHashSize + 2 * ethereum::kEthContractABIWordSize);
        libbitcoin::decode_base16(data, ethereum::swap_contract::GetRedeemMethodHash(IsHashLockScheme()));
        data.insert(data.end(), std::begin(secret.m_pData), std::end(secret.m_pData));
        data.insert(data.end(), std::begin(secretHash), std::end(secretHash));
    }
    else
    {
        auto initiatorStr = m_tx.GetMandatoryParameter<std::string>(TxParameterID::AtomicSwapPeerPublicKey);
        auto initiator = ethereum::ConvertStrToEthAddress(initiatorStr);
        auto participant = m_ethBridge->generateEthAddress();

        // keccak256: addressFromSecret + participant + initiator + refundTimeInBlocks
        uintBig refundTimeInBlocks = m_tx.GetMandatoryParameter<Height>(TxParameterID::AtomicSwapExternalLockTime);
        libbitcoin::data_chunk hashData;
        hashData.reserve(3 * libbitcoin::short_hash_size + ethereum::kEthContractABIWordSize);
        hashData.insert(hashData.end(), secretHash.cbegin(), secretHash.cend());
        hashData.insert(hashData.end(), participant.cbegin(), participant.cend());
        hashData.insert(hashData.end(), initiator.cbegin(), initiator.cend());
        hashData.insert(hashData.end(), std::cbegin(refundTimeInBlocks.m_pData), std::cend(refundTimeInBlocks.m_pData));

        if (IsERC20Token())
        {
            // add TokenContractAddress
            auto swapCoin = m_tx.GetMandatoryParameter<AtomicSwapCoin>(TxParameterID::AtomicSwapCoin);
            const auto tokenContractAddress = ethereum::ConvertStrToEthAddress(m_settingsProvider.GetSettings().GetTokenContractAddress(swapCoin));
            hashData.insert(hashData.end(), tokenContractAddress.cbegin(), tokenContractAddress.cend());
        }

        auto hash = ethash::keccak256(&hashData[0], hashData.size());

        libbitcoin::hash_digest hashDigest;
        std::move(std::begin(hash.bytes), std::end(hash.bytes), hashDigest.begin());
        libbitcoin::ec_secret secretEC;
        std::move(std::begin(secret.m_pData), std::end(secret.m_pData), std::begin(secretEC));

        libbitcoin::recoverable_signature signature;
        libbitcoin::sign_recoverable(signature, secretEC, hashDigest);

        // kRedeemMethodHash + addressFromSecret + signature (r, s, v)
        data.reserve(ethereum::kEthContractMethodHashSize + 4 * ethereum::kEthContractABIWordSize);
        libbitcoin::decode_base16(data, ethereum::swap_contract::GetRedeemMethodHash(IsHashLockScheme()));
        ethereum::AddContractABIWordToBuffer(secretHash, data);
        data.insert(data.end(), std::begin(signature.signature), std::end(signature.signature));
        data.insert(data.end(), 31u, 0x00);
        data.push_back(signature.recovery_id + 27u);
    }

    return data;
}

beam::ByteBuffer EthereumSide::BuildRefundTxData()
{
    // kRefundMethodHash + secretHash/addressFromSecret
    auto secretHash = GetSecretHash();
    libbitcoin::data_chunk data;
    data.reserve(ethereum::kEthContractMethodHashSize + ethereum::kEthContractABIWordSize);
    libbitcoin::decode_base16(data, ethereum::swap_contract::GetRefundMethodHash(IsHashLockScheme()));
    ethereum::AddContractABIWordToBuffer(secretHash, data);

    return data;
}

bool EthereumSide::IsERC20Token() const
{
    auto swapCoin = m_tx.GetMandatoryParameter<AtomicSwapCoin>(TxParameterID::AtomicSwapCoin);
    switch (swapCoin)
    {
        case beam::wallet::AtomicSwapCoin::Dai:
        case beam::wallet::AtomicSwapCoin::Usdt:
        case beam::wallet::AtomicSwapCoin::WBTC:
            return true;
        case beam::wallet::AtomicSwapCoin::Ethereum:
            return false;
        default:
        {
            assert("Unexpected swapCoin type.");
            return false;
        }
    }
}

beam::ByteBuffer EthereumSide::BuildLockTxData()
{
    auto participantStr = m_isEthOwner ?
        m_tx.GetMandatoryParameter<std::string>(TxParameterID::AtomicSwapPeerPublicKey) :
        m_tx.GetMandatoryParameter<std::string>(TxParameterID::AtomicSwapPublicKey);
    auto participant = ethereum::ConvertStrToEthAddress(participantStr);
    uintBig refundTimeInBlocks = m_tx.GetMandatoryParameter<Height>(TxParameterID::AtomicSwapExternalLockTime);

    // LockMethodHash + refundTimeInBlocks + hashedSecret/addressFromSecret + participant
    beam::ByteBuffer out;
    out.reserve(ethereum::kEthContractMethodHashSize + 3 * ethereum::kEthContractABIWordSize);
    libbitcoin::decode_base16(out, ethereum::swap_contract::GetLockMethodHash(IsERC20Token(), IsHashLockScheme()));
    ethereum::AddContractABIWordToBuffer({ std::begin(refundTimeInBlocks.m_pData), std::end(refundTimeInBlocks.m_pData) }, out);
    ethereum::AddContractABIWordToBuffer(GetSecretHash(), out);
    ethereum::AddContractABIWordToBuffer(participant, out);

    if (IsERC20Token())
    {
        // + ERC20 contractAddress, + value
        auto swapCoin = m_tx.GetMandatoryParameter<AtomicSwapCoin>(TxParameterID::AtomicSwapCoin);
        const auto tokenContractAddress = ethereum::ConvertStrToEthAddress(m_settingsProvider.GetSettings().GetTokenContractAddress(swapCoin));
        uintBig swapAmount = GetSwapAmount();

        ethereum::AddContractABIWordToBuffer(tokenContractAddress, out);
        ethereum::AddContractABIWordToBuffer({ std::begin(swapAmount.m_pData), std::end(swapAmount.m_pData) }, out);
    }

    return out;
}

bool EthereumSide::IsHashLockScheme() const
{
    // TODO: -> settings or mb add as TxParameterID
    return false;
}

void EthereumSide::SetTxError(const ethereum::IBridge::Error& error, SubTxID subTxID)
{
    TxFailureReason previousReason;

    if (m_tx.GetParameter(TxParameterID::InternalFailureReason, previousReason, subTxID))
    {
        return;
    }

    LOG_ERROR() << m_tx.GetTxID() << "[" << static_cast<SubTxID>(subTxID) << "]" << " Bridge internal error: type = " << error.m_type << "; message = " << error.m_message;
    switch (error.m_type)
    {
    case ethereum::IBridge::EmptyResult:
    case ethereum::IBridge::InvalidResultFormat:
        m_tx.SetParameter(TxParameterID::InternalFailureReason, TxFailureReason::SwapFormatResponseError, false, subTxID);
        break;
    case ethereum::IBridge::IOError:
        m_tx.SetParameter(TxParameterID::InternalFailureReason, TxFailureReason::SwapNetworkBridgeError, false, subTxID);
        break;
    default:
        m_tx.SetParameter(TxParameterID::InternalFailureReason, TxFailureReason::SwapSecondSideBridgeError, false, subTxID);
    }

    m_tx.UpdateAsync();
}

ECC::uintBig EthereumSide::GetSwapAmount() const
{
    auto swapCoin = m_tx.GetMandatoryParameter<AtomicSwapCoin>(TxParameterID::AtomicSwapCoin);
    uintBig swapAmount = m_tx.GetMandatoryParameter<Amount>(TxParameterID::AtomicSwapAmount);
    swapAmount = swapAmount * ECC::uintBig(ethereum::GetCoinUnitsMultiplier(swapCoin));
    return swapAmount;
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

Amount EthereumSide::CalcLockTxFee(Amount priceGas, AtomicSwapCoin /*swapCoin*/)
{
    return priceGas * ethereum::kLockTxGasLimit;
}

Amount EthereumSide::CalcWithdrawTxFee(Amount priceGas, AtomicSwapCoin swapCoin)
{
    return priceGas * ethereum::kWithdrawTxGasLimit;
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
    if (IsHashLockScheme())
    {
        m_tx.SetParameter(TxParameterID::PreImage, secret.V, false, BEAM_REDEEM_TX);
    }
    else
    {
        m_tx.SetParameter(TxParameterID::AtomicSwapSecretPrivateKey, secret.V, false, BEAM_REDEEM_TX);
    }
}

uint16_t EthereumSide::GetTxMinConfirmations(SubTxID subTxID) const
{
    switch (subTxID)
    {
    case SubTxIndex::LOCK_TX:
        return m_settingsProvider.GetSettings().GetLockTxMinConfirmations();
    case SubTxIndex::REDEEM_TX:
    case SubTxIndex::REFUND_TX:
        return m_settingsProvider.GetSettings().GetWithdrawTxMinConfirmations();
    default:
        assert(false && "unexpected subTxID");
        return 0;
    }
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
    if (IsHashLockScheme())
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
    if (IsHashLockScheme())
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
    // TODO need change this. maybe overflow
    // convert from gwei to wei
    return m_tx.GetMandatoryParameter<Amount>(TxParameterID::Fee, subTxID) * 1'000'000'000u;
}

libbitcoin::short_hash EthereumSide::GetContractAddress() const
{
    return ethereum::ConvertStrToEthAddress(GetContractAddressStr());
}

std::string EthereumSide::GetContractAddressStr() const
{
    if (IsERC20Token())
    {
        return m_settingsProvider.GetSettings().GetERC20SwapContractAddress(IsHashLockScheme());
    }
    return m_settingsProvider.GetSettings().GetContractAddress(IsHashLockScheme());
}

} // namespace beam::wallet