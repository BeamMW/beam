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
    constexpr uint32_t kBTCLockTimeSec = 2 * 24 * 60 * 60;
    constexpr uint32_t kBTCWithdrawTxAverageSize = 360;
    constexpr uint32_t kBTCMaxHeightDifference = 10;
    // it's average value
    constexpr uint32_t kBtcTxTimeInBeamBlocks = 70;
    constexpr uint32_t kBTCTransactionVersion = 2;


    libbitcoin::chain::script AtomicSwapContract(const libbitcoin::ec_compressed& publicKeyA
        , const libbitcoin::ec_compressed& publicKeyB
        , const libbitcoin::ec_compressed& publicKeySecret
        , int64_t locktime)
    {
        using namespace libbitcoin::machine;

        operation::list contract_operations;

        contract_operations.emplace_back(operation(opcode::if_)); // Normal redeem path
        {
            // Verify 2 of 2 multi signature is being used to redeem the output.
            contract_operations.emplace_back(operation(opcode::push_positive_2));
            contract_operations.emplace_back(operation(libbitcoin::to_chunk(publicKeyB)));
            contract_operations.emplace_back(operation(libbitcoin::to_chunk(publicKeySecret)));
            contract_operations.emplace_back(operation(opcode::push_positive_2));
            contract_operations.emplace_back(operation(opcode::checkmultisig));
        }
        contract_operations.emplace_back(operation(opcode::else_)); // Refund path
        {
            // Verify locktime and drop it off the stack (which is not done by CLTV).
            operation locktimeOp;
            locktimeOp.from_string(std::to_string(locktime));
            contract_operations.emplace_back(locktimeOp);
            contract_operations.emplace_back(operation(opcode::checklocktimeverify));
            contract_operations.emplace_back(operation(opcode::drop));

            // Verify our signature is being used to redeem the output.
            contract_operations.emplace_back(operation(libbitcoin::to_chunk(publicKeyA)));
            contract_operations.emplace_back(operation(opcode::checksig));
        }
        contract_operations.emplace_back(operation(opcode::endif));

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

    bool BitcoinSide::Initialize()
    {
        if (!LoadSwapAddress())
            return false;

        if (m_isBtcOwner)
        {
            InitSecret();
        }

        if (!GetBlockCount())
        {
            m_tx.UpdateAsync();
            return false;
        }

        return true;
    }

    bool BitcoinSide::InitLockTime()
    {
        auto height = GetBlockCount();
        assert(height);

        auto externalLockPeriod = height + m_bitcoinBridge->getLockTimeInBlocks();
        m_tx.SetParameter(TxParameterID::AtomicSwapExternalLockTime, externalLockPeriod);

        return true;
    }

    bool BitcoinSide::ValidateLockTime()
    {
        auto height = GetBlockCount();
        assert(height);
        auto externalLockTime = m_tx.GetMandatoryParameter<Height>(TxParameterID::AtomicSwapExternalLockTime);

        if (externalLockTime <= height)
        {
            return false;
        }

        Height lockPeriod = externalLockTime - height;
        if (lockPeriod > m_bitcoinBridge->getLockTimeInBlocks())
        {
            return (lockPeriod - m_bitcoinBridge->getLockTimeInBlocks()) <= kBTCMaxHeightDifference;
        }

        return (m_bitcoinBridge->getLockTimeInBlocks() - lockPeriod) <= kBTCMaxHeightDifference;
    }

    void BitcoinSide::AddTxDetails(SetTxParameter& txParameters)
    {
        auto txID = m_tx.GetMandatoryParameter<std::string>(TxParameterID::AtomicSwapExternalTxID, SubTxIndex::LOCK_TX);
        uint32_t outputIndex = m_tx.GetMandatoryParameter<uint32_t>(TxParameterID::AtomicSwapExternalTxOutputIndex, SubTxIndex::LOCK_TX);
        std::string swapPublicKeyStr = m_tx.GetMandatoryParameter<std::string>(TxParameterID::AtomicSwapPublicKey);

        txParameters.AddParameter(TxParameterID::AtomicSwapPeerPublicKey, swapPublicKeyStr)
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

        if (m_SwapLockTxConfirmations < m_bitcoinBridge->getTxMinConfirmations())
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

    bool BitcoinSide::HasEnoughTimeToProcessLockTx()
    {
        Height lockTxMaxHeight = MaxHeight;
        if (m_tx.GetParameter(TxParameterID::MaxHeight, lockTxMaxHeight, SubTxIndex::BEAM_LOCK_TX))
        {
            Block::SystemState::Full systemState;
            if (m_tx.GetTip(systemState) && systemState.m_Height > lockTxMaxHeight - GetTxTimeInBeamBlocks())
            {
                return false;
            }
        }
        return true;
    }

    uint32_t BitcoinSide::GetTxTimeInBeamBlocks() const
    {
        // it's average value
        return 70;
    }

    bool BitcoinSide::CheckAmount(Amount amount, Amount feeRate)
    {
        constexpr Amount kDustThreshold = 546;
        Amount fee = static_cast<Amount>(std::round(double(kBTCWithdrawTxAverageSize * feeRate) / 1000));
        return amount > kDustThreshold && amount > fee;
    }

    bool BitcoinSide::LoadSwapAddress()
    {
        // load or generate BTC address
        if (std::string swapPublicKeyStr; !m_tx.GetParameter(TxParameterID::AtomicSwapPublicKey, swapPublicKeyStr))
        {
            // is need to setup type 'legacy'?
            m_bitcoinBridge->getRawChangeAddress([this, weak = this->weak_from_this()](const IBitcoinBridge::Error& error, const std::string& address)
            {
                if (!weak.expired())
                {
                    OnGetRawChangeAddress(error, address);
                }
            });

            return false;
        }
        return true;
    }

    void BitcoinSide::InitSecret()
    {
        NoLeak<uintBig> secretPrivateKey;
        GenRandom(secretPrivateKey.V);
        m_tx.SetParameter(TxParameterID::AtomicSwapSecretPrivateKey, secretPrivateKey.V, false, BEAM_REDEEM_TX);
    }

    libbitcoin::chain::script BitcoinSide::CreateAtomicSwapContract()
    {
        Timestamp locktime = m_tx.GetMandatoryParameter<Timestamp>(TxParameterID::AtomicSwapExternalLockTime);
        std::string peerSwapPublicKeyStr = m_tx.GetMandatoryParameter<std::string>(TxParameterID::AtomicSwapPeerPublicKey);
        std::string swapPublicKeyStr = m_tx.GetMandatoryParameter<std::string>(TxParameterID::AtomicSwapPublicKey);


        libbitcoin::wallet::ec_public secretPublicKey;

        if (NoLeak<uintBig> secretPrivateKey; m_tx.GetParameter(TxParameterID::AtomicSwapSecretPrivateKey, secretPrivateKey.V, SubTxIndex::BEAM_REDEEM_TX))
        {
            // secretPrivateKey -> secretPublicKey
            libbitcoin::ec_secret secret;
            std::copy(std::begin(secretPrivateKey.V.m_pData), std::end(secretPrivateKey.V.m_pData), secret.begin());
            libbitcoin::wallet::ec_private privateKey(secret, m_bitcoinBridge->getAddressVersion());

            secretPublicKey = privateKey.to_public();
        }
        else
        {
            Point publicKeyPoint = m_tx.GetMandatoryParameter<Point>(TxParameterID::AtomicSwapSecretPublicKey, SubTxIndex::BEAM_REDEEM_TX);

            // publicKeyPoint -> secretPublicKey
            auto publicKeyRaw = SerializePubkey(ConvertPointToPubkey(publicKeyPoint));
            secretPublicKey = libbitcoin::wallet::ec_public(publicKeyRaw);
        }

        libbitcoin::wallet::ec_public senderPublicKey(m_isBtcOwner ? swapPublicKeyStr : peerSwapPublicKeyStr);
        libbitcoin::wallet::ec_public receiverPublicKey(m_isBtcOwner ? peerSwapPublicKeyStr : swapPublicKeyStr);

        return AtomicSwapContract(senderPublicKey.point(), receiverPublicKey.point(), secretPublicKey.point(), locktime);
    }

    bool BitcoinSide::RegisterTx(const std::string& rawTransaction, SubTxID subTxID)
    {
		uint8_t nRegistered = proto::TxStatus::Unspecified;
        if (!m_tx.GetParameter(TxParameterID::TransactionRegistered, nRegistered, subTxID))
        {
            auto callback = [this, subTxID, weak = this->weak_from_this()](const IBitcoinBridge::Error& error, const std::string& txID) {
                if (!weak.expired())
                {
                    if (error.m_type != IBitcoinBridge::None)
                    {
                        SetTxError(error, subTxID);
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
                }
            };

            m_bitcoinBridge->sendRawTransaction(rawTransaction, callback);
            return (proto::TxStatus::Ok == nRegistered);
        }

        if (proto::TxStatus::Ok != nRegistered)
        {
            m_tx.SetParameter(TxParameterID::InternalFailureReason, TxFailureReason::FailedToRegister, false, subTxID);
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
            contractTx.set_version(kBTCTransactionVersion);
            libbitcoin::chain::script outputScript = libbitcoin::chain::script::to_pay_script_hash_pattern(libbitcoin::bitcoin_short_hash(contractScript.to_data(false)));
            libbitcoin::chain::output output(swapAmount, outputScript);
            contractTx.outputs().push_back(output);

            std::string hexTx = libbitcoin::encode_base16(contractTx.to_data());

            m_bitcoinBridge->fundRawTransaction(hexTx, m_bitcoinBridge->getFeeRate(), [this, weak = this->weak_from_this()](const IBitcoinBridge::Error& error, const std::string& hexTx, int changePos)
            {
                if (!weak.expired())
                {
                    OnFundRawTransaction(error, hexTx, changePos);
                }
            });

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
            std::string withdrawAddress = GetWithdrawAddress();
            uint32_t outputIndex = m_tx.GetMandatoryParameter<uint32_t>(TxParameterID::AtomicSwapExternalTxOutputIndex, SubTxIndex::LOCK_TX);
            auto swapLockTxID = m_tx.GetMandatoryParameter<std::string>(TxParameterID::AtomicSwapExternalTxID, SubTxIndex::LOCK_TX);

            Timestamp locktime = 0;
            if (subTxID == SubTxIndex::REFUND_TX)
            {
                locktime = m_tx.GetMandatoryParameter<Timestamp>(TxParameterID::AtomicSwapExternalLockTime);
            }

            auto callback = [this, subTxID, weak = this->weak_from_this()](const IBitcoinBridge::Error& error, const std::string& hexTx) {
                if (!weak.expired())
                {
                    OnCreateWithdrawTransaction(subTxID, error, hexTx);
                }
            };
            m_bitcoinBridge->createRawTransaction(withdrawAddress, swapLockTxID, swapAmount, outputIndex, locktime, callback);
            return swapTxState;
        }

        if (swapTxState == SwapTxState::CreatingTx)
        {
            if (!m_SwapWithdrawRawTx.is_initialized())
            {
                LOG_ERROR() << m_tx.GetTxID() << "[" << subTxID << "]" << " Incorrect state, rebuilding.";
                m_tx.SetState(SwapTxState::Initial, subTxID);
                return SwapTxState::Initial;
            }

            std::string withdrawAddress = GetWithdrawAddress();
            auto callback = [this, subTxID, weak = this->weak_from_this()](const IBitcoinBridge::Error& error, const std::string& privateKey) {
                if (!weak.expired())
                {
                    OnDumpPrivateKey(subTxID, error, privateKey);
                }
            };

            m_bitcoinBridge->dumpPrivKey(withdrawAddress, callback);
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

        m_bitcoinBridge->getTxOut(txID, outputIndex, [this, weak = this->weak_from_this()](const IBitcoinBridge::Error& error, const std::string& hexScript, double amount, uint16_t confirmations)
        {
            if (!weak.expired())
            {
                OnGetSwapLockTxConfirmations(error, hexScript, amount, confirmations);
            }
        });
    }

    bool BitcoinSide::SendWithdrawTx(SubTxID subTxID)
    {
        auto refundTxState = BuildWithdrawTx(subTxID);
        if (refundTxState != SwapTxState::Constructed)
            return false;

         assert(m_SwapWithdrawRawTx.is_initialized());

        if (!RegisterTx(*m_SwapWithdrawRawTx, subTxID))
            return false;

        // TODO: check confirmations

        return true;
    }

    uint64_t BitcoinSide::GetBlockCount()
    {
        m_bitcoinBridge->getBlockCount([this, weak = this->weak_from_this()](const IBitcoinBridge::Error& error, uint64_t blockCount)
        {
            if (!weak.expired())
            {
                OnGetBlockCount(error, blockCount);
            }
        });
        return m_blockCount;
    }

    std::string BitcoinSide::GetWithdrawAddress() const
    {
        std::string swapPublicKeyStr = m_tx.GetMandatoryParameter<std::string>(TxParameterID::AtomicSwapPublicKey);
        libbitcoin::wallet::ec_public swapPublicKey(swapPublicKeyStr);
        return swapPublicKey.to_payment_address(m_bitcoinBridge->getAddressVersion()).encoded();
    }

    void BitcoinSide::SetTxError(const IBitcoinBridge::Error& error, SubTxID subTxID)
    {
        TxFailureReason previousReason;

        if (m_tx.GetParameter(TxParameterID::InternalFailureReason, previousReason, subTxID))
        {
            return;
        }

        LOG_ERROR() << m_tx.GetTxID() << "[" << static_cast<SubTxID>(subTxID) << "]" << " Bridge internal error: type = " << error.m_type << "; message = " << error.m_message;
        switch (error.m_type)
        {
        case IBitcoinBridge::EmptyResult:
        case IBitcoinBridge::InvalidResultFormat:
            m_tx.SetParameter(TxParameterID::InternalFailureReason, TxFailureReason::SwapFormatResponseError, false, subTxID);
            break;
        case IBitcoinBridge::IOError:
            m_tx.SetParameter(TxParameterID::InternalFailureReason, TxFailureReason::SwapNetworkBridgeError, false, subTxID);
            break;
        case IBitcoinBridge::InvalidCredentials:
            m_tx.SetParameter(TxParameterID::InternalFailureReason, TxFailureReason::InvalidCredentialsOfSideChain, false, subTxID);
            break;
        default:
            m_tx.SetParameter(TxParameterID::InternalFailureReason, TxFailureReason::SwapSecondSideBridgeError, false, subTxID);
        }

        m_tx.UpdateAsync();
    }

    void BitcoinSide::OnGetRawChangeAddress(const IBitcoinBridge::Error& error, const std::string& address)
    {
        try
        {
            if (error.m_type != IBitcoinBridge::None)
            {
                SetTxError(error, SubTxIndex::LOCK_TX);
                return;
            }

            // Don't need overwrite existing public key
            if (std::string swapPublicKey; !m_tx.GetParameter(TxParameterID::AtomicSwapPublicKey, swapPublicKey))
            {
                auto callback = [this, weak = this->weak_from_this()](const IBitcoinBridge::Error& error, const std::string& privateKey) {
                    if (!weak.expired())
                    {
                        if (std::string swapPublicKey; !m_tx.GetParameter(TxParameterID::AtomicSwapPublicKey, swapPublicKey))
                        {
                            libbitcoin::wallet::ec_private addressPrivateKey(privateKey, m_bitcoinBridge->getAddressVersion());

                            m_tx.SetParameter(TxParameterID::AtomicSwapPublicKey, addressPrivateKey.to_public().encoded());
                            m_tx.UpdateAsync();
                        }
                    }
                };
                m_bitcoinBridge->dumpPrivKey(address, callback);
            }
        }
        catch (const std::exception& ex)
        {
            LOG_ERROR() << m_tx.GetTxID() << " exception msg: " << ex.what();

            m_tx.SetParameter(TxParameterID::InternalFailureReason, TxFailureReason::Unknown, false, SubTxIndex::LOCK_TX);
            m_tx.UpdateAsync();
        }
    }

    void BitcoinSide::OnFundRawTransaction(const IBitcoinBridge::Error& error, const std::string& hexTx, int changePos)
    {
        if (error.m_type != IBitcoinBridge::None)
        {
            SetTxError(error, SubTxIndex::LOCK_TX);
            return;
        }

        // float fee = result["fee"].get<float>();      // calculate fee!
        uint32_t valuePosition = changePos ? 0 : 1;
        m_tx.SetParameter(TxParameterID::AtomicSwapExternalTxOutputIndex, valuePosition, false, SubTxIndex::LOCK_TX);

        m_bitcoinBridge->signRawTransaction(hexTx, [this, weak = this->weak_from_this()](const IBitcoinBridge::Error& error, const std::string& hexTx, bool complete)
        {
            if (!weak.expired())
            {
                OnSignLockTransaction(error, hexTx, complete);
            }
        });
    }

    void BitcoinSide::OnSignLockTransaction(const IBitcoinBridge::Error& error, const std::string& hexTx, bool complete)
    {
        if (error.m_type != IBitcoinBridge::None)
        {
            SetTxError(error, SubTxIndex::LOCK_TX);
            return;
        }

        assert(complete);
        m_SwapLockRawTx = hexTx;

        m_tx.SetState(SwapTxState::Constructed, SubTxIndex::LOCK_TX);
        m_tx.UpdateAsync();
    }

    void BitcoinSide::OnCreateWithdrawTransaction(SubTxID subTxID, const IBitcoinBridge::Error& error, const std::string& hexTx)
    {
        if (error.m_type != IBitcoinBridge::None)
        {
            SetTxError(error, subTxID);
            return;
        }

        if (!m_SwapWithdrawRawTx.is_initialized())
        {
            m_SwapWithdrawRawTx = hexTx;
            m_tx.SetState(SwapTxState::CreatingTx, subTxID);
            m_tx.UpdateAsync();
        }
    }

    void BitcoinSide::OnDumpPrivateKey(SubTxID subTxID, const IBitcoinBridge::Error& error, const std::string& privateKey)
    {
        try
        {
            if (error.m_type != IBitcoinBridge::None)
            {
                SetTxError(error, subTxID);
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

            if (SubTxIndex::REFUND_TX == subTxID)
            {
                // <my sig> 0
                sig_script.push_back(libbitcoin::machine::operation(sig));
                sig_script.push_back(libbitcoin::machine::operation(libbitcoin::machine::opcode(0)));
            }
            else
            {
                // AtomicSwapSecretPrivateKey -> libbitcoin::wallet::ec_private
                NoLeak<uintBig> secretPrivateKey;
                m_tx.GetParameter(TxParameterID::AtomicSwapSecretPrivateKey, secretPrivateKey.V, SubTxIndex::BEAM_REDEEM_TX);
                libbitcoin::ec_secret secret;
                std::copy(std::begin(secretPrivateKey.V.m_pData), std::end(secretPrivateKey.V.m_pData), secret.begin());

                libbitcoin::endorsement secretSig;
                libbitcoin::chain::script::create_endorsement(secretSig, secret, contractScript, withdrawTX, input_index, libbitcoin::machine::sighash_algorithm::all);

               // 0 <their sig> <secret sig> 1
                sig_script.push_back(libbitcoin::machine::operation(libbitcoin::machine::opcode(0)));
                sig_script.push_back(libbitcoin::machine::operation(sig));
                sig_script.push_back(libbitcoin::machine::operation(secretSig));
                sig_script.push_back(libbitcoin::machine::operation(libbitcoin::machine::opcode::push_positive_1));
            }

            sig_script.push_back(libbitcoin::machine::operation(contractScript.to_data(false)));

            libbitcoin::chain::script input_script(sig_script);

            // Add input script to first input in transaction
            withdrawTX.inputs()[0].set_script(input_script);

            // update m_SwapWithdrawRawTx
            m_SwapWithdrawRawTx = libbitcoin::encode_base16(withdrawTX.to_data());

            m_tx.SetParameter(TxParameterID::AtomicSwapExternalTx, *m_SwapWithdrawRawTx, subTxID);
            m_tx.SetState(SwapTxState::Constructed, subTxID);
            m_tx.UpdateAsync();
        }
        catch (const TransactionFailedException& ex)
        {
            LOG_ERROR() << m_tx.GetTxID() << " exception msg: " << ex.what();
            m_tx.SetParameter(TxParameterID::InternalFailureReason, ex.GetReason(), false, subTxID);
            m_tx.UpdateAsync();
        }
        catch (const std::exception& ex)
        {
            LOG_ERROR() << m_tx.GetTxID() << " exception msg: " << ex.what();

            m_tx.SetParameter(TxParameterID::InternalFailureReason, TxFailureReason::Unknown, false, subTxID);
            m_tx.UpdateAsync();
        }
    }

    void BitcoinSide::OnGetSwapLockTxConfirmations(const IBitcoinBridge::Error& error, const std::string& hexScript, double amount, uint16_t confirmations)
    {
        try
        {
            if (error.m_type != IBitcoinBridge::None)
            {
                SetTxError(error, SubTxIndex::LOCK_TX);
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
                    m_tx.SetParameter(TxParameterID::InternalFailureReason, TxFailureReason::SwapInvalidAmount, false, SubTxIndex::LOCK_TX);
                    m_tx.UpdateAsync();
                    return;
                }
            }

            // validate contract script
            libbitcoin::data_chunk scriptData;
            libbitcoin::decode_base16(scriptData, hexScript);
            auto script = libbitcoin::chain::script::factory_from_data(scriptData, false);

            auto contractScript = CreateAtomicSwapContract();
            auto inputScript = libbitcoin::chain::script::to_pay_script_hash_pattern(libbitcoin::bitcoin_short_hash(contractScript.to_data(false)));

            assert(script == inputScript);

            if (script != inputScript)
            {
                m_tx.SetParameter(TxParameterID::InternalFailureReason, TxFailureReason::SwapInvalidContract, false, SubTxIndex::LOCK_TX);
                m_tx.UpdateAsync();
                return;
            }

            // get confirmations
            if (m_SwapLockTxConfirmations != confirmations)
            {
                LOG_DEBUG() << m_tx.GetTxID() << "[" << static_cast<SubTxID>(SubTxIndex::LOCK_TX) << "] " << confirmations << "/"
                    << m_bitcoinBridge->getTxMinConfirmations() << " confirmations are received.";
                m_SwapLockTxConfirmations = confirmations;
            }
        }
        catch (const TransactionFailedException& ex)
        {
            LOG_ERROR() << m_tx.GetTxID() << " exception msg: " << ex.what();
            m_tx.SetParameter(TxParameterID::InternalFailureReason, ex.GetReason(), false, SubTxIndex::LOCK_TX);
            m_tx.UpdateAsync();
        }
        catch (const std::exception& ex)
        {
            LOG_ERROR() << m_tx.GetTxID() << " exception msg: " << ex.what();

            m_tx.SetParameter(TxParameterID::InternalFailureReason, TxFailureReason::Unknown, false, SubTxIndex::LOCK_TX);
            m_tx.UpdateAsync();
        }
    }

    void BitcoinSide::OnGetBlockCount(const IBitcoinBridge::Error& error, uint64_t blockCount)
    {
        try
        {
            if (error.m_type != IBitcoinBridge::None)
            {
                SetTxError(error, SubTxIndex::LOCK_TX);
                return;
            }
            m_blockCount = blockCount;
        }
        catch (const std::exception& ex)
        {
            LOG_ERROR() << m_tx.GetTxID() << " exception msg: " << ex.what();

            m_tx.SetParameter(TxParameterID::InternalFailureReason, TxFailureReason::Unknown, false, SubTxIndex::LOCK_TX);
            m_tx.UpdateAsync();
        }
    }
}