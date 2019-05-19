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

#include "bitcoin_side.h"

#include "bitcoin/bitcoin.hpp"
#include "nlohmann/json.hpp"

using namespace ECC;
using json = nlohmann::json;

namespace
{
    constexpr uint32_t kBTCLockTimeInBlocks = 2 * 24 * 6;
    constexpr uint32_t kBTCLockTimeSec = 2 * 24 * 60 * 60;
    constexpr uint32_t kBTCMinTxConfirmations = 6;
    constexpr uint32_t kBTCWithdrawTxAverageSize = 240;

    libbitcoin::chain::script AtomicSwapContract(const libbitcoin::short_hash& hashPublicKeyA
        , const libbitcoin::short_hash& hashPublicKeyB
        , int64_t locktime
        , const libbitcoin::data_chunk& secretHash
        , size_t secretSize)
    {
        using namespace libbitcoin::machine;

        operation::list contract_operations;

        contract_operations.emplace_back(operation(opcode::if_)); // Normal redeem path
        {
            // Require initiator's secret to be a known length that the redeeming
            // party can audit.  This is used to prevent fraud attacks between two
            // currencies that have different maximum data sizes.
            contract_operations.emplace_back(operation(opcode::size));
            operation secretSizeOp;
            secretSizeOp.from_string(std::to_string(secretSize));
            contract_operations.emplace_back(secretSizeOp);
            contract_operations.emplace_back(operation(opcode::equalverify));

            // Require initiator's secret to be known to redeem the output.
            contract_operations.emplace_back(operation(opcode::sha256));
            contract_operations.emplace_back(operation(secretHash));
            contract_operations.emplace_back(operation(opcode::equalverify));

            // Verify their signature is being used to redeem the output.  This
            // would normally end with OP_EQUALVERIFY OP_CHECKSIG but this has been
            // moved outside of the branch to save a couple bytes.
            contract_operations.emplace_back(operation(opcode::dup));
            contract_operations.emplace_back(operation(opcode::hash160));
            contract_operations.emplace_back(operation(libbitcoin::to_chunk(hashPublicKeyB)));
        }
        contract_operations.emplace_back(operation(opcode::else_)); // Refund path
        {
            // Verify locktime and drop it off the stack (which is not done by CLTV).
            operation locktimeOp;
            locktimeOp.from_string(std::to_string(locktime));
            contract_operations.emplace_back(locktimeOp);
            contract_operations.emplace_back(operation(opcode::checklocktimeverify));
            contract_operations.emplace_back(operation(opcode::drop));

            // Verify our signature is being used to redeem the output.  This would
            // normally end with OP_EQUALVERIFY OP_CHECKSIG but this has been moved
            // outside of the branch to save a couple bytes.
            contract_operations.emplace_back(operation(opcode::dup));
            contract_operations.emplace_back(operation(opcode::hash160));
            contract_operations.emplace_back(operation(libbitcoin::to_chunk(hashPublicKeyA)));
        }
        contract_operations.emplace_back(operation(opcode::endif));

        // Complete the signature check.
        contract_operations.emplace_back(operation(opcode::equalverify));
        contract_operations.emplace_back(operation(opcode::checksig));

        return libbitcoin::chain::script(contract_operations);
    }
}

namespace beam::wallet
{
    BitcoinSide::BitcoinSide(BaseTransaction& tx, IBitcoinBridge::Ptr bitcoinBridge, bool isBeamSide)
        : m_tx(tx)
        , m_bitcoinBridge(bitcoinBridge)
        , m_isBtcOwner(!isBeamSide)
    {
    }

    bool BitcoinSide::Initial()
    {
        if (!LoadSwapAddress())
            return false;

        if (m_isBtcOwner)
        {
            InitSecret();
        }

        return true;
    }

    bool BitcoinSide::InitLockTime()
    {
        auto height = GetBlockCount();
        if (!height)
        {
            return false;
        }

        auto externalLockPeriod = height + kBTCLockTimeInBlocks;
        m_tx.SetParameter(TxParameterID::AtomicSwapExternalLockTime, externalLockPeriod);

        return true;
    }

    void BitcoinSide::AddTxDetails(SetTxParameter& txParameters)
    {
        auto txID = m_tx.GetMandatoryParameter<std::string>(TxParameterID::AtomicSwapExternalTxID, SubTxIndex::LOCK_TX);
        uint32_t outputIndex = m_tx.GetMandatoryParameter<uint32_t>(TxParameterID::AtomicSwapExternalTxOutputIndex, SubTxIndex::LOCK_TX);
        std::string swapAddress = m_tx.GetMandatoryParameter<std::string>(TxParameterID::AtomicSwapAddress);

        txParameters.AddParameter(TxParameterID::AtomicSwapPeerAddress, swapAddress)
            .AddParameter(TxParameterID::SubTxIndex, SubTxIndex::LOCK_TX)
            .AddParameter(TxParameterID::AtomicSwapExternalTxID, txID)
            .AddParameter(TxParameterID::AtomicSwapExternalTxOutputIndex, outputIndex);
    }

    bool BitcoinSide::ConfirmLockTx()
    {
        // wait TxID from peer
        std::string txID;
        if (!m_tx.GetParameter(TxParameterID::AtomicSwapExternalTxID, txID, SubTxIndex::LOCK_TX))
            return false;

        if (m_SwapLockTxConfirmations < kBTCMinTxConfirmations)
        {
            // validate expired?

            GetSwapLockTxConfirmations();
            return false;
        }

        return true;
    }

    bool BitcoinSide::SendLockTx()
    {
        auto lockTxState = BuildLockTx();
        if (lockTxState != SwapTxState::Constructed)
            return false;

        // send contractTx
        assert(m_SwapLockRawTx.is_initialized());

        if (!RegisterTx(*m_SwapLockRawTx, SubTxIndex::LOCK_TX))
            return false;

        return true;
    }

    bool BitcoinSide::SendRefund()
    {
        return SendWithdrawTx(SubTxIndex::REFUND_TX);
    }

    bool BitcoinSide::SendRedeem()
    {
        return SendWithdrawTx(SubTxIndex::REDEEM_TX);
    }

    bool BitcoinSide::IsLockTimeExpired()
    {
        uint64_t height = GetBlockCount();
        uint64_t lockHeight = 0;
        m_tx.GetParameter(TxParameterID::AtomicSwapExternalLockTime, lockHeight);

        return height >= lockHeight;
    }

    bool BitcoinSide::LoadSwapAddress()
    {
        // load or generate BTC address
        if (std::string swapAddress; !m_tx.GetParameter(TxParameterID::AtomicSwapAddress, swapAddress))
        {
            // is need to setup type 'legacy'?
            m_bitcoinBridge->getRawChangeAddress(BIND_THIS_MEMFN(OnGetRawChangeAddress));

            return false;
        }
        return true;
    }

    void BitcoinSide::InitSecret()
    {
        NoLeak<uintBig> preimage;
        GenRandom(preimage.V);
        m_tx.SetParameter(TxParameterID::PreImage, preimage.V, false, BEAM_REDEEM_TX);
    }

    libbitcoin::chain::script BitcoinSide::CreateAtomicSwapContract()
    {
        Timestamp locktime = m_tx.GetMandatoryParameter<Timestamp>(TxParameterID::AtomicSwapExternalLockTime);
        std::string peerSwapAddress = m_tx.GetMandatoryParameter<std::string>(TxParameterID::AtomicSwapPeerAddress);
        std::string swapAddress = m_tx.GetMandatoryParameter<std::string>(TxParameterID::AtomicSwapAddress);

        // load secret or secretHash
        Hash::Value lockImage(Zero);

        if (NoLeak<uintBig> preimage; m_tx.GetParameter(TxParameterID::PreImage, preimage.V, SubTxIndex::BEAM_REDEEM_TX))
        {
            Hash::Processor() << preimage.V >> lockImage;
        }
        else
        {
            lockImage = m_tx.GetMandatoryParameter<uintBig>(TxParameterID::PeerLockImage, SubTxIndex::BEAM_REDEEM_TX);
        }

        libbitcoin::data_chunk secretHash = libbitcoin::to_chunk(lockImage.m_pData);
        libbitcoin::wallet::payment_address senderAddress(m_isBtcOwner ? swapAddress : peerSwapAddress);
        libbitcoin::wallet::payment_address receiverAddress(m_isBtcOwner ? peerSwapAddress : swapAddress);

        return AtomicSwapContract(senderAddress.hash(), receiverAddress.hash(), locktime, secretHash, secretHash.size());
    }

    bool BitcoinSide::RegisterTx(const std::string& rawTransaction, SubTxID subTxID)
    {
		uint8_t nRegistered = proto::TxStatus::Unspecified;
        if (!m_tx.GetParameter(TxParameterID::TransactionRegistered, nRegistered, subTxID))
        {
            auto callback = [this, subTxID](const std::string& error, const std::string& txID) {
                if (!error.empty())
                {
                    LOG_ERROR() << m_tx.GetTxID() << "[" << subTxID << "]" << " Bridge internal error: " << error;
                    m_tx.SetParameter(TxParameterID::FailureReason, TxFailureReason::SwapSecondSideBridgeError, false, subTxID);
                    m_tx.UpdateAsync();
                    return;
                }

                bool isRegistered = !txID.empty();
                LOG_DEBUG() << m_tx.GetTxID() << "[" << subTxID << "]" << (isRegistered ? " has registered." : " has failed to register.");

				uint8_t nRegistered = isRegistered ? proto::TxStatus::Ok : proto::TxStatus::Unspecified;
                m_tx.SetParameter(TxParameterID::TransactionRegistered, nRegistered, false, subTxID);

                if (!txID.empty())
                {
                    m_tx.SetParameter(TxParameterID::AtomicSwapExternalTxID, txID, false, subTxID);
                }

                m_tx.Update();
            };

            m_bitcoinBridge->sendRawTransaction(rawTransaction, callback);
            return (proto::TxStatus::Ok == nRegistered);
        }

        if (proto::TxStatus::Ok != nRegistered)
        {
            m_tx.SetParameter(TxParameterID::FailureReason, TxFailureReason::FailedToRegister, false, subTxID);
        }

        return (proto::TxStatus::Ok == nRegistered);
    }

    SwapTxState BitcoinSide::BuildLockTx()
    {
        SwapTxState swapTxState = SwapTxState::Initial;
        m_tx.GetParameter(TxParameterID::State, swapTxState, SubTxIndex::LOCK_TX);

        if (swapTxState == SwapTxState::Initial)
        {
            auto contractScript = CreateAtomicSwapContract();
            Amount swapAmount = m_tx.GetMandatoryParameter<Amount>(TxParameterID::AtomicSwapAmount);

            libbitcoin::chain::transaction contractTx;
            libbitcoin::chain::output output(swapAmount, contractScript);
            contractTx.outputs().push_back(output);

            std::string hexTx = libbitcoin::encode_base16(contractTx.to_data());

            m_bitcoinBridge->fundRawTransaction(hexTx, m_bitcoinBridge->getFeeRate(), BIND_THIS_MEMFN(OnFundRawTransaction));

            m_tx.SetState(SwapTxState::CreatingTx, SubTxIndex::LOCK_TX);
            return SwapTxState::CreatingTx;
        }

        if (swapTxState == SwapTxState::CreatingTx)
        {
            // TODO: implement
        }

        // TODO: check
        return swapTxState;
    }

    SwapTxState BitcoinSide::BuildWithdrawTx(SubTxID subTxID)
    {
        SwapTxState swapTxState = SwapTxState::Initial;
        m_tx.GetParameter(TxParameterID::State, swapTxState, subTxID);

        if (swapTxState == SwapTxState::Initial)
        {
            Amount fee = static_cast<Amount>(std::round(double(kBTCWithdrawTxAverageSize * m_bitcoinBridge->getFeeRate()) / 1000));
            Amount swapAmount = m_tx.GetMandatoryParameter<Amount>(TxParameterID::AtomicSwapAmount);
            swapAmount = swapAmount - fee;
            std::string swapAddress = m_tx.GetMandatoryParameter<std::string>(TxParameterID::AtomicSwapAddress);
            uint32_t outputIndex = m_tx.GetMandatoryParameter<uint32_t>(TxParameterID::AtomicSwapExternalTxOutputIndex, SubTxIndex::LOCK_TX);
            auto swapLockTxID = m_tx.GetMandatoryParameter<std::string>(TxParameterID::AtomicSwapExternalTxID, SubTxIndex::LOCK_TX);

            Timestamp locktime = 0;
            if (subTxID == SubTxIndex::REFUND_TX)
            {
                locktime = m_tx.GetMandatoryParameter<Timestamp>(TxParameterID::AtomicSwapExternalLockTime);
            }

            auto callback = [this, subTxID](const std::string& error, const std::string& hexTx) {
                OnCreateWithdrawTransaction(subTxID, error, hexTx);
            };
            m_bitcoinBridge->createRawTransaction(swapAddress, swapLockTxID, swapAmount, outputIndex, locktime, callback);
            m_tx.SetState(SwapTxState::CreatingTx, subTxID);
            return SwapTxState::CreatingTx;
        }

        if (swapTxState == SwapTxState::CreatingTx)
        {
            std::string swapAddress = m_tx.GetMandatoryParameter<std::string>(TxParameterID::AtomicSwapAddress);
            auto callback = [this, subTxID](const std::string& error, const std::string& privateKey) {
                OnDumpPrivateKey(subTxID, error, privateKey);
            };

            m_bitcoinBridge->dumpPrivKey(swapAddress, callback);
        }

        if (swapTxState == SwapTxState::Constructed && !m_SwapWithdrawRawTx.is_initialized())
        {
            m_SwapWithdrawRawTx = m_tx.GetMandatoryParameter<std::string>(TxParameterID::AtomicSwapExternalTx, subTxID);
        }

        return swapTxState;
    }

    void BitcoinSide::GetSwapLockTxConfirmations()
    {
        auto txID = m_tx.GetMandatoryParameter<std::string>(TxParameterID::AtomicSwapExternalTxID, SubTxIndex::LOCK_TX);
        uint32_t outputIndex = m_tx.GetMandatoryParameter<uint32_t>(TxParameterID::AtomicSwapExternalTxOutputIndex, SubTxIndex::LOCK_TX);

        m_bitcoinBridge->getTxOut(txID, outputIndex, BIND_THIS_MEMFN(OnGetSwapLockTxConfirmations));
    }

    bool BitcoinSide::SendWithdrawTx(SubTxID subTxID)
    {
        if (uint8_t nRegistered = proto::TxStatus::Unspecified; !m_tx.GetParameter(TxParameterID::TransactionRegistered, nRegistered, subTxID)) // TODO
        {
            auto refundTxState = BuildWithdrawTx(subTxID);
            if (refundTxState != SwapTxState::Constructed)
                return false;

            assert(m_SwapWithdrawRawTx.is_initialized());
        }

        if (!RegisterTx(*m_SwapWithdrawRawTx, subTxID))
            return false;

        // TODO: check confirmations

        return true;
    }

    uint64_t BitcoinSide::GetBlockCount()
    {
        m_bitcoinBridge->getBlockCount(BIND_THIS_MEMFN(OnGetBlockCount));
        return m_blockCount;
    }

    void BitcoinSide::OnGetRawChangeAddress(const std::string& error, const std::string& address)
    {
        if (!error.empty())
        {
            LOG_ERROR() << m_tx.GetTxID() << "[" << static_cast<SubTxID>(SubTxIndex::LOCK_TX) << "]" << " Bridge internal error: " << error;
            m_tx.SetParameter(TxParameterID::FailureReason, TxFailureReason::SwapSecondSideBridgeError, false, SubTxIndex::LOCK_TX);
            m_tx.UpdateAsync();
            return;
        }

        // Don't need overwrite existing address
        if (std::string swapAddress; !m_tx.GetParameter(TxParameterID::AtomicSwapAddress, swapAddress))
        {
            m_tx.SetParameter(TxParameterID::AtomicSwapAddress, address);
        }

        m_tx.UpdateAsync();
    }

    void BitcoinSide::OnFundRawTransaction(const std::string& error, const std::string& hexTx, int changePos)
    {
        if (!error.empty())
        {
            LOG_ERROR() << m_tx.GetTxID() << "[" << static_cast<SubTxID>(SubTxIndex::LOCK_TX) << "]" << " Bridge internal error: " << error;
            m_tx.SetParameter(TxParameterID::FailureReason, TxFailureReason::SwapSecondSideBridgeError, false, SubTxIndex::LOCK_TX);
            m_tx.UpdateAsync();
            return;
        }

        // float fee = result["fee"].get<float>();      // calculate fee!
        uint32_t valuePosition = changePos ? 0 : 1;
        m_tx.SetParameter(TxParameterID::AtomicSwapExternalTxOutputIndex, valuePosition, false, SubTxIndex::LOCK_TX);

        m_bitcoinBridge->signRawTransaction(hexTx, BIND_THIS_MEMFN(OnSignLockTransaction));
    }

    void BitcoinSide::OnSignLockTransaction(const std::string& error, const std::string& hexTx, bool complete)
    {
        if (!error.empty())
        {
            LOG_ERROR() << m_tx.GetTxID() << "[" << static_cast<SubTxID>(SubTxIndex::LOCK_TX) << "]" << " Bridge internal error: " << error;
            m_tx.SetParameter(TxParameterID::FailureReason, TxFailureReason::SwapSecondSideBridgeError, false, SubTxIndex::LOCK_TX);
            m_tx.UpdateAsync();
            return;
        }

        assert(complete);
        m_SwapLockRawTx = hexTx;

        m_tx.SetState(SwapTxState::Constructed, SubTxIndex::LOCK_TX);
        m_tx.UpdateAsync();
    }

    void BitcoinSide::OnCreateWithdrawTransaction(SubTxID subTxID, const std::string& error, const std::string& hexTx)
    {
        if (!error.empty())
        {
            LOG_ERROR() << m_tx.GetTxID() << "[" << subTxID << "]" << " Bridge internal error: " << error;
            m_tx.SetParameter(TxParameterID::FailureReason, TxFailureReason::SwapSecondSideBridgeError, false, subTxID);
            m_tx.UpdateAsync();
            return;
        }

        if (!m_SwapWithdrawRawTx.is_initialized())
        {
            m_SwapWithdrawRawTx = hexTx;
            m_tx.UpdateAsync();
        }
    }

    void BitcoinSide::OnDumpPrivateKey(SubTxID subTxID, const std::string& error, const std::string& privateKey)
    {
        if (!error.empty())
        {
            LOG_ERROR() << m_tx.GetTxID() << "[" << subTxID << "]" << " Bridge internal error: " << error;
            m_tx.SetParameter(TxParameterID::FailureReason, TxFailureReason::SwapSecondSideBridgeError, false, subTxID);
            m_tx.UpdateAsync();
            return;
        }

        libbitcoin::data_chunk tx_data;
        libbitcoin::decode_base16(tx_data, *m_SwapWithdrawRawTx);
        libbitcoin::chain::transaction withdrawTX = libbitcoin::chain::transaction::factory_from_data(tx_data);

        libbitcoin::wallet::ec_private wallet_key(privateKey, m_bitcoinBridge->getAddressVersion());
        libbitcoin::endorsement sig;

        uint32_t input_index = 0;
        auto contractScript = CreateAtomicSwapContract();
        libbitcoin::chain::script::create_endorsement(sig, wallet_key.secret(), contractScript, withdrawTX, input_index, libbitcoin::machine::sighash_algorithm::all);

        // Create input script
        libbitcoin::machine::operation::list sig_script;
        libbitcoin::ec_compressed pubkey = wallet_key.to_public().point();

        if (SubTxIndex::REFUND_TX == subTxID)
        {
            // <my sig> <my pubkey> 0
            sig_script.push_back(libbitcoin::machine::operation(sig));
            sig_script.push_back(libbitcoin::machine::operation(libbitcoin::to_chunk(pubkey)));
            sig_script.push_back(libbitcoin::machine::operation(libbitcoin::machine::opcode(0)));
        }
        else
        {
            auto secret = m_tx.GetMandatoryParameter<ECC::uintBig>(TxParameterID::PreImage, SubTxIndex::BEAM_REDEEM_TX);

            // <their sig> <their pubkey> <initiator secret> 1
            sig_script.push_back(libbitcoin::machine::operation(sig));
            sig_script.push_back(libbitcoin::machine::operation(libbitcoin::to_chunk(pubkey)));
            sig_script.push_back(libbitcoin::machine::operation(libbitcoin::to_chunk(secret.m_pData)));
            sig_script.push_back(libbitcoin::machine::operation(libbitcoin::machine::opcode::push_positive_1));
        }

        libbitcoin::chain::script input_script(sig_script);

        // Add input script to first input in transaction
        withdrawTX.inputs()[0].set_script(input_script);

        // update m_SwapWithdrawRawTx
        m_SwapWithdrawRawTx = libbitcoin::encode_base16(withdrawTX.to_data());

        m_tx.SetParameter(TxParameterID::AtomicSwapExternalTx, *m_SwapWithdrawRawTx, subTxID);
        m_tx.SetState(SwapTxState::Constructed, subTxID);
        m_tx.UpdateAsync();
    }

    void BitcoinSide::OnGetSwapLockTxConfirmations(const std::string& error, const std::string& hexScript, double amount, uint16_t confirmations)
    {
        if (!error.empty())
        {
            LOG_ERROR() << m_tx.GetTxID() << "[" << static_cast<SubTxID>(SubTxIndex::LOCK_TX) << "]" << " Bridge internal error: " << error;
            m_tx.SetParameter(TxParameterID::FailureReason, TxFailureReason::SwapSecondSideBridgeError, false, SubTxIndex::LOCK_TX);
            m_tx.UpdateAsync();
            return;
        }

        if (hexScript.empty())
        {
            return;
        }

        // validate amount
        {
            Amount swapAmount = m_tx.GetMandatoryParameter<Amount>(TxParameterID::AtomicSwapAmount);
            Amount outputAmount = static_cast<Amount>(std::round(amount * libbitcoin::satoshi_per_bitcoin));
            if (swapAmount > outputAmount)
            {
                LOG_ERROR() << m_tx.GetTxID() << "[" << static_cast<SubTxID>(SubTxIndex::LOCK_TX) << "]" 
                    << " Unexpected amount, expected: " << swapAmount << ", got: " << outputAmount;
                m_tx.SetParameter(TxParameterID::FailureReason, TxFailureReason::SwapInvalidAmount, false, SubTxIndex::LOCK_TX);
                return;
            }
        }

        // validate contract script
        libbitcoin::data_chunk scriptData;
        libbitcoin::decode_base16(scriptData, hexScript);
        auto script = libbitcoin::chain::script::factory_from_data(scriptData, false);

        auto contractScript = CreateAtomicSwapContract();

        assert(script == contractScript);

        if (script != contractScript)
        {
            m_tx.SetParameter(TxParameterID::FailureReason, TxFailureReason::SwapInvalidContract, false, SubTxIndex::LOCK_TX);
            return;
        }

        // get confirmations
        if (m_SwapLockTxConfirmations != confirmations)
        {
            LOG_DEBUG() << m_tx.GetTxID() << "[" << static_cast<SubTxID>(SubTxIndex::LOCK_TX) << "] " << confirmations << "/" 
                << kBTCMinTxConfirmations <<" confirmations are received.";
            m_SwapLockTxConfirmations = confirmations;
        }
    }

    void BitcoinSide::OnGetBlockCount(const std::string& error, uint64_t blockCount)
    {
        if (!error.empty())
        {
            LOG_ERROR() << m_tx.GetTxID() << "[" << static_cast<SubTxID>(SubTxIndex::LOCK_TX) << "]" << " Bridge internal error: " << error;
            m_tx.SetParameter(TxParameterID::FailureReason, TxFailureReason::SwapSecondSideBridgeError, false, SubTxIndex::LOCK_TX);
            m_tx.UpdateAsync();
            return;
        }
        m_blockCount = blockCount;
    }
}