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

#include "common.h"
#include "core/block_crypt.h"
#include "utility/logger.h"

#include "bitcoin/bitcoin.hpp"
#include "nlohmann/json.hpp"

using namespace ECC;
using json = nlohmann::json;

namespace
{
    constexpr uint32_t kExternalHeightMaxDifference = 5;

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

    libbitcoin::chain::script CreateAtomicSwapContract(const beam::wallet::BaseTransaction& tx, bool isBtcOwner, uint8_t addressVersion)
    {
        using namespace beam;
        using namespace beam::wallet;

        libbitcoin::wallet::ec_public secretPublicKey;

        if (NoLeak<uintBig> secretPrivateKey; tx.GetParameter(TxParameterID::AtomicSwapSecretPrivateKey, secretPrivateKey.V, SubTxIndex::BEAM_REDEEM_TX))
        {
            // secretPrivateKey -> secretPublicKey
            libbitcoin::ec_secret secret;
            std::copy(std::begin(secretPrivateKey.V.m_pData), std::end(secretPrivateKey.V.m_pData), secret.begin());
            libbitcoin::wallet::ec_private privateKey(secret, addressVersion);

            secretPublicKey = privateKey.to_public();
        }
        else
        {
            Point publicKeyPoint = tx.GetMandatoryParameter<Point>(TxParameterID::AtomicSwapSecretPublicKey, SubTxIndex::BEAM_REDEEM_TX);

            // publicKeyPoint -> secretPublicKey
            auto publicKeyRaw = SerializePubkey(ConvertPointToPubkey(publicKeyPoint));
            secretPublicKey = libbitcoin::wallet::ec_public(publicKeyRaw);
        }

        Timestamp locktime = tx.GetMandatoryParameter<Timestamp>(TxParameterID::AtomicSwapExternalLockTime);
        std::string peerSwapPublicKeyStr = tx.GetMandatoryParameter<std::string>(TxParameterID::AtomicSwapPeerPublicKey);
        std::string localPublicKeyStr = tx.GetMandatoryParameter<std::string>(TxParameterID::AtomicSwapPublicKey);

        libbitcoin::wallet::ec_public senderPublicKey(isBtcOwner ? localPublicKeyStr : peerSwapPublicKeyStr);
        libbitcoin::wallet::ec_public receiverPublicKey(isBtcOwner ? peerSwapPublicKeyStr : localPublicKeyStr);

        return AtomicSwapContract(senderPublicKey.point(), receiverPublicKey.point(), secretPublicKey.point(), locktime);
    }

    libbitcoin::ec_secret GetWithdrawTxLocalSecret(const beam::wallet::BaseTransaction& tx, beam::wallet::SubTxID subTxID, bool quickRefund)
    {
        NoLeak<uintBig> localPrivateKey;
        if (beam::wallet::SubTxIndex::REFUND_TX == subTxID && quickRefund)
            localPrivateKey.V = tx.GetMandatoryParameter<uintBig>(beam::wallet::TxParameterID::AtomicSwapPeerPrivateKey);
        else
            localPrivateKey.V = tx.GetMandatoryParameter<uintBig>(beam::wallet::TxParameterID::AtomicSwapPrivateKey);

        libbitcoin::ec_secret localSecret;
        std::copy(std::begin(localPrivateKey.V.m_pData), std::end(localPrivateKey.V.m_pData), localSecret.begin());
        return localSecret;
    }

    const char kBridgeNotConnectedError[] = "Bridge internal error: bridge not connected.";
}

namespace beam::wallet
{
    BitcoinSide::BitcoinSide(BaseTransaction& tx, bitcoin::IBridge::Ptr bitcoinBridge, bitcoin::ISettingsProvider& settingsProvider, bool isBeamSide)
        : m_tx(tx)
        , m_bitcoinBridge(bitcoinBridge)
        , m_settingsProvider(settingsProvider)
        , m_isBtcOwner(!isBeamSide)
    {
        m_settingsProvider.AddRef();
    }

    BitcoinSide::~BitcoinSide()
    {
        m_settingsProvider.ReleaseRef();
    }

    bool BitcoinSide::Initialize()
    {
        if (!LoadSwapAddress())
            return false;

        if (!m_blockCount)
        {
            GetBlockCount(true);
            return false;
        }

        if (m_isBtcOwner)
        {
            InitSecret();
        }

        InitLocalKeys();

        return true;
    }

    bool BitcoinSide::InitLockTime()
    {
        auto height = m_blockCount;
        assert(height);

        BEAM_LOG_DEBUG() << "InitLockTime height = " << height;

        auto externalLockPeriod = height + GetLockTimeInBlocks();
        m_tx.SetParameter(TxParameterID::AtomicSwapExternalLockTime, externalLockPeriod);

        return true;
    }

    bool BitcoinSide::ValidateLockTime()
    {
        auto height = m_blockCount;
        assert(height);
        auto externalLockTime = m_tx.GetMandatoryParameter<Height>(TxParameterID::AtomicSwapExternalLockTime);

        BEAM_LOG_DEBUG() << "ValidateLockTime height = " << height << " external = " << externalLockTime;

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

        if (m_SwapLockTxConfirmations < GetTxMinConfirmations(SubTxIndex::LOCK_TX))
        {
            // validate expired?

            GetSwapLockTxConfirmations();
            return false;
        }

        return true;
    }

    bool BitcoinSide::ConfirmRefundTx()
    {
        // wait TxID from peer
        std::string txID;
        if (!m_tx.GetParameter(TxParameterID::AtomicSwapExternalTxID, txID, SubTxIndex::REFUND_TX))
            return false;

        if (m_RefundTxConfirmations < GetTxMinConfirmations(SubTxIndex::REFUND_TX))
        {
            // validate expired?

            GetRefundTxConfirmations();
            return false;
        }

        return true;
    }

    bool BitcoinSide::ConfirmRedeemTx()
    {
        // wait TxID from peer
        std::string txID;
        if (!m_tx.GetParameter(TxParameterID::AtomicSwapExternalTxID, txID, SubTxIndex::REDEEM_TX))
            return false;

        if (m_RedeemTxConfirmations < GetTxMinConfirmations(SubTxIndex::REDEEM_TX))
        {
            // validate expired?

            GetRedeemTxConfirmations();
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
            if (m_tx.GetTip(systemState) && systemState.m_Height > lockTxMaxHeight - GetLockTxEstimatedTimeInBeamBlocks())
            {
                return false;
            }
        }
        return true;
    }

    bool BitcoinSide::IsQuickRefundAvailable()
    {
        NoLeak<uintBig> rawPeerPrivateKey;

        if (m_tx.GetParameter(TxParameterID::AtomicSwapPeerPrivateKey, rawPeerPrivateKey.V))
        {
            libbitcoin::ec_secret peerPrivateKey;
            std::copy(std::begin(rawPeerPrivateKey.V.m_pData), std::end(rawPeerPrivateKey.V.m_pData), peerPrivateKey.begin());

            libbitcoin::wallet::ec_private localPrivateKey(peerPrivateKey, GetAddressVersion());
            std::string peerSwapPublicKeyStr = m_tx.GetMandatoryParameter<std::string>(TxParameterID::AtomicSwapPeerPublicKey);

            return localPrivateKey.to_public().encoded() == peerSwapPublicKeyStr;
        }

        return false;
    }

    uint32_t BitcoinSide::GetLockTxEstimatedTimeInBeamBlocks() const
    {
        // it's average value
        return 70;
    }

    bool BitcoinSide::IsSegwitSupported() const
    {
        return true;
    }

    uint32_t BitcoinSide::GetWithdrawTxAverageSize() const
    {
        if (CanUseSegwit())
            return bitcoin::kBTCWithdrawSegwitTxAverageSize;
        return bitcoin::kBTCWithdrawTxAverageSize;
    }

    Amount BitcoinSide::GetFee(Amount feeRate) const
    {
        return static_cast<Amount>(std::round(double(GetWithdrawTxAverageSize() * feeRate) / 1000));
    }

    bool BitcoinSide::CheckLockTxAmount(Amount amount, Amount feeRate)
    {
        Amount fee = CalcWithdrawTxFee(feeRate);
        return amount > fee && (amount - fee) >= bitcoin::kDustThreshold;
    }

    Amount BitcoinSide::CalcWithdrawTxFee(Amount feeRate)
    {
        // TODO roman.strilets need to use segwit 
        return static_cast<Amount>(std::round(double(bitcoin::kBTCWithdrawTxAverageSize * feeRate) / 1000));
    }

    uint8_t BitcoinSide::GetAddressVersion() const
    {
        return m_settingsProvider.GetSettings().GetAddressVersion();
    }

    uint8_t BitcoinSide::GetSighashAlgorithm() const
    {
        return libbitcoin::machine::sighash_algorithm::all;
    }

    bool BitcoinSide::NeedSignValue() const
    {
        return false;
    }

    Amount BitcoinSide::GetFeeRate(SubTxID subTxID) const
    {
        return m_tx.GetMandatoryParameter<Amount>(TxParameterID::Fee, subTxID);
    }

    uint16_t BitcoinSide::GetTxMinConfirmations(SubTxID subTxID) const
    {
        switch (subTxID)
        {
        case SubTxIndex::LOCK_TX:
            return m_settingsProvider.GetSettings().GetLockTxMinConfirmations();
        case SubTxIndex::REDEEM_TX:
        case SubTxIndex::REFUND_TX:
            return m_settingsProvider.GetSettings().GetWithdrawTxMinConfirmations();
        default:
            assert(false && "unexpected subTxID!");
            return 0;
        }
    }

    uint32_t BitcoinSide::GetLockTimeInBlocks() const
    {
        return m_settingsProvider.GetSettings().GetLockTimeInBlocks();
    }

    double BitcoinSide::GetBlocksPerHour() const
    {
        return m_settingsProvider.GetSettings().GetBlocksPerHour();
    }

    bool BitcoinSide::LoadSwapAddress()
    {
        // load or generate withdraw address
        if (std::string swapWithdrawAddress; !m_tx.GetParameter(TxParameterID::AtomicSwapWithdrawAddress, swapWithdrawAddress))
        {
            if (!m_bitcoinBridge)
            {
                BEAM_LOG_ERROR() << kBridgeNotConnectedError;
                return false;
            }
            // is need to setup type 'legacy'?
            m_bitcoinBridge->getRawChangeAddress([this, weak = this->weak_from_this()](const bitcoin::IBridge::Error& error, const std::string& address)
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

    void BitcoinSide::InitLocalKeys()
    {
        if (std::string publicKey; !m_tx.GetParameter(TxParameterID::AtomicSwapPublicKey, publicKey))
        {
            NoLeak<uintBig> randomPrivateKey;
            GenRandom(randomPrivateKey.V);
            m_tx.SetParameter(TxParameterID::AtomicSwapPrivateKey, randomPrivateKey.V);

            libbitcoin::ec_secret localSecret;
            std::copy(std::begin(randomPrivateKey.V.m_pData), std::end(randomPrivateKey.V.m_pData), localSecret.begin());
            libbitcoin::wallet::ec_private localPrivateKey(localSecret, GetAddressVersion());

            m_tx.SetParameter(TxParameterID::AtomicSwapPublicKey, localPrivateKey.to_public().encoded());
        }
    }

    bool BitcoinSide::RegisterTx(const std::string& rawTransaction, SubTxID subTxID)
    {
		uint8_t nRegistered = proto::TxStatus::Unspecified;
        if (!m_tx.GetParameter(TxParameterID::TransactionRegistered, nRegistered, subTxID))
        {
            auto callback = [this, subTxID, weak = this->weak_from_this()](const bitcoin::IBridge::Error& error, const std::string& txID) {
                if (!weak.expired())
                {
                    if (error.m_type != bitcoin::IBridge::None)
                    {
                        if (error.m_type == bitcoin::IBridge::BitcoinError ||
                            error.m_type == bitcoin::IBridge::InvalidResultFormat)
                        {
                            SetTxError(error, subTxID);
                        }
                        m_tx.UpdateOnNextTip();
                        return;
                    }

                    bool isRegistered = !txID.empty();
                    BEAM_LOG_DEBUG() << m_tx.GetTxID() << "[" << subTxID << "]" << (isRegistered ? " has registered. External txid: " + txID : " has failed to register.");

                    uint8_t nRegistered = isRegistered ? proto::TxStatus::Ok : proto::TxStatus::Unspecified;
                    m_tx.SetParameter(TxParameterID::TransactionRegistered, nRegistered, false, subTxID);

                    if (!txID.empty())
                    {
                        m_tx.SetParameter(TxParameterID::Confirmations, uint32_t(0), false, subTxID);
                        m_tx.SetParameter(TxParameterID::AtomicSwapExternalTxID, txID, false, subTxID);
                    }

                    m_tx.Update();
                }
            };

            if (!m_bitcoinBridge)
            {
                BEAM_LOG_ERROR() << kBridgeNotConnectedError;
                return false;
            }

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
            auto contractScript = CreateAtomicSwapContract(m_tx, m_isBtcOwner, GetAddressVersion());
            Amount swapAmount = m_tx.GetMandatoryParameter<Amount>(TxParameterID::AtomicSwapAmount);

            libbitcoin::chain::script outputScript;
            
            if (CanUseSegwit())
            {
                libbitcoin::hash_digest embeddedScriptHash = libbitcoin::sha256_hash(contractScript.to_data(false));
                libbitcoin::machine::operation::list p2shOperations;

                p2shOperations.emplace_back(libbitcoin::machine::operation(libbitcoin::machine::opcode::push_size_0));
                p2shOperations.emplace_back(libbitcoin::machine::operation(libbitcoin::to_chunk(embeddedScriptHash)));

                outputScript = libbitcoin::chain::script(p2shOperations);
            }
            else
            {
                outputScript = libbitcoin::chain::script::to_pay_script_hash_pattern(libbitcoin::bitcoin_short_hash(contractScript.to_data(false)));
            }

            libbitcoin::chain::transaction contractTx;
            contractTx.set_version(bitcoin::kTransactionVersion);
            libbitcoin::chain::output output(swapAmount, outputScript);
            contractTx.outputs().push_back(output);

            std::string hexTx = libbitcoin::encode_base16(contractTx.to_data());

            if (!m_bitcoinBridge)
            {
                BEAM_LOG_ERROR() << kBridgeNotConnectedError;
                return SwapTxState::CreatingTx;
            }

            m_bitcoinBridge->fundRawTransaction(hexTx, GetFeeRate(SubTxIndex::LOCK_TX), [this, weak = this->weak_from_this()](const bitcoin::IBridge::Error& error, const std::string& hexTx, int changePos)
            {
                if (!weak.expired())
                {
                    OnFundRawTransaction(error, hexTx, changePos);
                }
            });
            return SwapTxState::CreatingTx;
        }

        if (swapTxState == SwapTxState::CreatingTx)
        {
            if (!m_SwapLockRawTx.is_initialized())
            {
                BEAM_LOG_ERROR() << m_tx.GetTxID() << "[" << (int)SubTxIndex::LOCK_TX << "]" << " Incorrect state, rebuilding.";
                m_tx.SetState(SwapTxState::Initial, SubTxIndex::LOCK_TX);
                m_tx.UpdateAsync();
                return SwapTxState::Initial;
            }

            if (!m_bitcoinBridge)
            {
                BEAM_LOG_ERROR() << kBridgeNotConnectedError;
                return SwapTxState::CreatingTx;
            }

            m_bitcoinBridge->signRawTransaction(*m_SwapLockRawTx, [this, weak = this->weak_from_this()](const bitcoin::IBridge::Error& error, const std::string& hexTx, bool complete)
            {
                if (!weak.expired())
                {
                    OnSignLockTransaction(error, hexTx, complete);
                }
            });
        }

        if (swapTxState == SwapTxState::Constructed && !m_SwapLockRawTx.is_initialized())
        {
            m_SwapLockRawTx = m_tx.GetMandatoryParameter<std::string>(TxParameterID::AtomicSwapExternalTx, SubTxIndex::LOCK_TX);
        }

        return swapTxState;
    }

    SwapTxState BitcoinSide::BuildWithdrawTx(SubTxID subTxID)
    {
        SwapTxState swapTxState = SwapTxState::Initial;
        m_tx.GetParameter(TxParameterID::State, swapTxState, subTxID);

        if (swapTxState == SwapTxState::Initial)
        {
            Amount fee = GetFee(GetFeeRate(subTxID));
            Amount swapAmount = m_tx.GetMandatoryParameter<Amount>(TxParameterID::AtomicSwapAmount);
            swapAmount = swapAmount - fee;
            std::string withdrawAddress = GetWithdrawAddress();
            uint32_t outputIndex = m_tx.GetMandatoryParameter<uint32_t>(TxParameterID::AtomicSwapExternalTxOutputIndex, SubTxIndex::LOCK_TX);
            auto swapLockTxID = m_tx.GetMandatoryParameter<std::string>(TxParameterID::AtomicSwapExternalTxID, SubTxIndex::LOCK_TX);

            Timestamp locktime = 0;
            if (subTxID == SubTxIndex::REFUND_TX && !IsQuickRefundAvailable())
            {
                locktime = m_tx.GetMandatoryParameter<Timestamp>(TxParameterID::AtomicSwapExternalLockTime);
            }

            auto callback = [this, subTxID, weak = this->weak_from_this()](const bitcoin::IBridge::Error& error, const std::string& hexTx) {
                if (!weak.expired())
                {
                    OnCreateWithdrawTransaction(subTxID, error, hexTx);
                }
            };

            if (!m_bitcoinBridge)
            {
                BEAM_LOG_ERROR() << kBridgeNotConnectedError;
                return swapTxState;
            }

            m_bitcoinBridge->createRawTransaction(withdrawAddress, swapLockTxID, swapAmount, outputIndex, locktime, callback);
            return swapTxState;
        }

        // TODO: may be delete this branch
        if (swapTxState == SwapTxState::CreatingTx)
        {
            if (!m_SwapWithdrawRawTx.is_initialized())
            {
                BEAM_LOG_ERROR() << m_tx.GetTxID() << "[" << subTxID << "]" << " Incorrect state, rebuilding.";
                m_tx.SetState(SwapTxState::Initial, subTxID);
                return SwapTxState::Initial;
            }

            FillWithdrawTx(subTxID);
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

        if (!m_bitcoinBridge)
        {
            BEAM_LOG_ERROR() << kBridgeNotConnectedError;
            return;
        }
        
        m_bitcoinBridge->getTxOut(txID, outputIndex, [this, weak = this->weak_from_this()](const bitcoin::IBridge::Error& error, const std::string& hexScript, Amount amount, uint32_t confirmations)
        {
            if (!weak.expired())
            {
                OnGetSwapLockTxConfirmations(error, hexScript, amount, confirmations);
            }
        });
    }

    void BitcoinSide::GetRefundTxConfirmations()
    {
        auto txID = m_tx.GetMandatoryParameter<std::string>(TxParameterID::AtomicSwapExternalTxID, SubTxIndex::REFUND_TX);
        uint32_t outputIndex = 0;

        if (!m_bitcoinBridge)
        {
            BEAM_LOG_ERROR() << kBridgeNotConnectedError;
            return;
        }

        m_bitcoinBridge->getTxOut(txID, outputIndex, [this, weak = this->weak_from_this()](const bitcoin::IBridge::Error& /*error*/, const std::string& /*hexScript*/, Amount /*amount*/, uint32_t confirmations)
        {
            if (!weak.expired())
            {
                // TODO should to processed error variable
                if (m_RefundTxConfirmations != confirmations)
                {
                    BEAM_LOG_DEBUG() << m_tx.GetTxID() << "[" << static_cast<SubTxID>(SubTxIndex::REFUND_TX) << "] " << confirmations << "/"
                        << GetTxMinConfirmations(SubTxIndex::REFUND_TX) << " confirmations for Refund TX are received.";
                    m_RefundTxConfirmations = confirmations;
                    m_tx.SetParameter(TxParameterID::Confirmations, m_RefundTxConfirmations, true, SubTxIndex::REFUND_TX);
                }
            }
        });
    }

    void BitcoinSide::GetRedeemTxConfirmations()
    {
        auto txID = m_tx.GetMandatoryParameter<std::string>(TxParameterID::AtomicSwapExternalTxID, SubTxIndex::REDEEM_TX);
        uint32_t outputIndex = 0;

        if (!m_bitcoinBridge)
        {
            BEAM_LOG_ERROR() << kBridgeNotConnectedError;
            return;
        }

        m_bitcoinBridge->getTxOut(txID, outputIndex, [this, weak = this->weak_from_this()](const bitcoin::IBridge::Error& /*error*/, const std::string& /*hexScript*/, Amount /*amount*/, uint32_t confirmations)
        {
            if (!weak.expired())
            {
                // TODO should to processed error variable
                if (m_RedeemTxConfirmations != confirmations)
                {
                    BEAM_LOG_DEBUG() << m_tx.GetTxID() << "[" << static_cast<SubTxID>(SubTxIndex::REDEEM_TX) << "] " << confirmations << "/"
                        << GetTxMinConfirmations(SubTxIndex::REDEEM_TX) << " confirmations for Redeem TX are received.";
                    m_RedeemTxConfirmations = confirmations;
                    m_tx.SetParameter(TxParameterID::Confirmations, m_RedeemTxConfirmations, true, SubTxIndex::REDEEM_TX);
                }
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

        return true;
    }

    uint64_t BitcoinSide::GetBlockCount(bool notify)
    {
        if (!m_bitcoinBridge)
        {
            BEAM_LOG_ERROR() << kBridgeNotConnectedError;
            return m_blockCount;
        }

        m_bitcoinBridge->getBlockCount([this, weak = this->weak_from_this(), notify](const bitcoin::IBridge::Error& error, uint64_t blockCount)
        {
            if (!weak.expired())
            {
                OnGetBlockCount(error, blockCount, notify);
            }
        });
        return m_blockCount;
    }

    std::string BitcoinSide::GetWithdrawAddress() const
    {
        return m_tx.GetMandatoryParameter<std::string>(TxParameterID::AtomicSwapWithdrawAddress);
    }

    void BitcoinSide::SetTxError(const bitcoin::IBridge::Error& error, SubTxID subTxID)
    {
        TxFailureReason previousReason;

        if (m_tx.GetParameter(TxParameterID::InternalFailureReason, previousReason, subTxID))
        {
            return;
        }

        BEAM_LOG_ERROR() << m_tx.GetTxID() << "[" << static_cast<SubTxID>(subTxID) << "]" << " Bridge internal error: type = " << error.m_type << "; message = " << error.m_message;
        switch (error.m_type)
        {
        case bitcoin::IBridge::EmptyResult:
        case bitcoin::IBridge::InvalidResultFormat:
            m_tx.SetParameter(TxParameterID::InternalFailureReason, TxFailureReason::SwapFormatResponseError, false, subTxID);
            break;
        case bitcoin::IBridge::IOError:
            m_tx.SetParameter(TxParameterID::InternalFailureReason, TxFailureReason::SwapNetworkBridgeError, false, subTxID);
            break;
        case bitcoin::IBridge::InvalidCredentials:
            m_tx.SetParameter(TxParameterID::InternalFailureReason, TxFailureReason::InvalidCredentialsOfSideChain, false, subTxID);
            break;
        default:
            m_tx.SetParameter(TxParameterID::InternalFailureReason, TxFailureReason::SwapSecondSideBridgeError, false, subTxID);
        }

        m_tx.UpdateAsync();
    }

    void BitcoinSide::OnGetRawChangeAddress(const bitcoin::IBridge::Error& error, const std::string& address)
    {
        try
        {
            if (error.m_type != bitcoin::IBridge::None)
            {
                m_tx.UpdateOnNextTip();
                return;
            }

            // Don't need overwrite existing withdraw address
            if (std::string swapWithdrawAddress; !m_tx.GetParameter(TxParameterID::AtomicSwapWithdrawAddress, swapWithdrawAddress))
            {
                m_tx.SetParameter(TxParameterID::AtomicSwapWithdrawAddress, address);
                m_tx.UpdateAsync();
            }
        }
        catch (const std::exception& ex)
        {
            BEAM_LOG_ERROR() << m_tx.GetTxID() << " exception msg: " << ex.what();

            m_tx.SetParameter(TxParameterID::InternalFailureReason, TxFailureReason::Unknown, false, SubTxIndex::LOCK_TX);
            m_tx.UpdateAsync();
        }
    }

    void BitcoinSide::OnFundRawTransaction(const bitcoin::IBridge::Error& error, const std::string& hexTx, int changePos)
    {
        // TODO: refactor this condition.
        // Checking !m_SwapLockRawTx.is_initialized() used to ignore double lock on electrum
        if (error.m_type != bitcoin::IBridge::None && !m_SwapLockRawTx.is_initialized())
        {
            if (error.m_type == bitcoin::IBridge::BitcoinError)
            {
                SetTxError(error, SubTxIndex::LOCK_TX);
            }
            else
            {
                m_tx.UpdateOnNextTip();
            }
            return;
        }

        if (!m_SwapLockRawTx.is_initialized())
        {
            m_SwapLockRawTx = hexTx;
            m_LockTxValuePosition = changePos ? 0 : 1;
            m_tx.SetState(SwapTxState::CreatingTx, SubTxIndex::LOCK_TX);
            m_tx.UpdateAsync();
        }
    }

    void BitcoinSide::OnSignLockTransaction(const bitcoin::IBridge::Error& error, const std::string& hexTx, bool complete)
    {
        if (error.m_type != bitcoin::IBridge::None)
        {
            if (error.m_type == bitcoin::IBridge::BitcoinError)
            {
                SetTxError(error, SubTxIndex::LOCK_TX);
            }
            else
            {
                m_tx.UpdateOnNextTip();
            }
            return;
        }

        assert(complete);
        m_SwapLockRawTx = hexTx;
        // save LockTx to DB
        m_tx.SetParameter(TxParameterID::AtomicSwapExternalTxOutputIndex, m_LockTxValuePosition, false, SubTxIndex::LOCK_TX);
        m_tx.SetParameter(TxParameterID::AtomicSwapExternalTx, *m_SwapLockRawTx, false, SubTxIndex::LOCK_TX);

        m_tx.SetState(SwapTxState::Constructed, SubTxIndex::LOCK_TX);
        m_tx.UpdateAsync();
    }

    void BitcoinSide::OnCreateWithdrawTransaction(SubTxID subTxID, const bitcoin::IBridge::Error& error, const std::string& hexTx)
    {
        if (error.m_type != bitcoin::IBridge::None)
        {
            m_tx.UpdateOnNextTip();
            return;
        }

        if (!m_SwapWithdrawRawTx.is_initialized())
        {
            m_SwapWithdrawRawTx = hexTx;
            m_tx.SetState(SwapTxState::CreatingTx, subTxID);

            FillWithdrawTx(subTxID);
        }
    }

    void BitcoinSide::FillWithdrawTx(SubTxID subTxID)
    {
        try
        {
            if (CanUseSegwit())
            {
                m_SwapWithdrawRawTx = FillSegwitWithdrawTxInput(subTxID);
            }
            else
            {
                m_SwapWithdrawRawTx = FillWithdrawTxInput(subTxID);
            }

            m_tx.SetParameter(TxParameterID::AtomicSwapExternalTx, *m_SwapWithdrawRawTx, subTxID);
            m_tx.SetState(SwapTxState::Constructed, subTxID);
        }
        catch (const TransactionFailedException& ex)
        {
            BEAM_LOG_ERROR() << m_tx.GetTxID() << " exception msg: " << ex.what();
            m_tx.SetParameter(TxParameterID::InternalFailureReason, ex.GetReason(), false, subTxID);
        }
        catch (const std::exception& ex)
        {
            BEAM_LOG_ERROR() << m_tx.GetTxID() << " exception msg: " << ex.what();

            m_tx.SetParameter(TxParameterID::InternalFailureReason, TxFailureReason::Unknown, false, subTxID);
        }
        m_tx.UpdateAsync();
    }

    std::string BitcoinSide::FillSegwitWithdrawTxInput(SubTxID subTxID)
    {
        libbitcoin::data_chunk tx_data;
        libbitcoin::decode_base16(tx_data, *m_SwapWithdrawRawTx);
        libbitcoin::chain::transaction withdrawTX = libbitcoin::chain::transaction::factory_from_data(tx_data, true, true);

        auto addressVersion = GetAddressVersion();
        bool quickRefund = IsQuickRefundAvailable();
        libbitcoin::ec_secret localSecret = GetWithdrawTxLocalSecret(m_tx, subTxID, quickRefund);
        Amount input_amount = m_tx.GetMandatoryParameter<Amount>(TxParameterID::AtomicSwapAmount);
        uint32_t input_index = 0;
        auto contractScript = CreateAtomicSwapContract(m_tx, m_isBtcOwner, addressVersion);
        libbitcoin::endorsement sig;
        libbitcoin::chain::script::create_endorsement(sig, localSecret, contractScript, withdrawTX, input_index,
            GetSighashAlgorithm(), libbitcoin::machine::script_version::zero, input_amount);

        // Create input witness script
        libbitcoin::data_stack witnessStack;
        libbitcoin::data_chunk emptyChunk;

        if (SubTxIndex::REFUND_TX == subTxID && !quickRefund)
        {
            // <my sig> 0
            witnessStack.push_back(sig);
            witnessStack.push_back(emptyChunk);
        }
        else
        {
            // AtomicSwapSecretPrivateKey -> libbitcoin::wallet::ec_private
            NoLeak<uintBig> secretPrivateKey;
            m_tx.GetParameter(TxParameterID::AtomicSwapSecretPrivateKey, secretPrivateKey.V, SubTxIndex::BEAM_REDEEM_TX);
            libbitcoin::ec_secret secret;
            std::copy(std::begin(secretPrivateKey.V.m_pData), std::end(secretPrivateKey.V.m_pData), secret.begin());

            libbitcoin::endorsement secretSig;
            libbitcoin::chain::script::create_endorsement(secretSig, secret, contractScript, withdrawTX, input_index,
                GetSighashAlgorithm(), libbitcoin::machine::script_version::zero, input_amount);

            // 0 <their sig> <secret sig> 1
            witnessStack.push_back(emptyChunk);
            witnessStack.push_back(sig);
            witnessStack.push_back(secretSig);
            witnessStack.emplace_back(libbitcoin::machine::operation(libbitcoin::machine::opcode(1)).to_data());
        }
        witnessStack.emplace_back(contractScript.to_data(false));

        libbitcoin::chain::witness p2wshWitness(witnessStack);

        // Add input witness script to first input in transaction
        withdrawTX.inputs()[0].set_witness(p2wshWitness);

        return libbitcoin::encode_base16(withdrawTX.to_data(true, true));
    }

    std::string BitcoinSide::FillWithdrawTxInput(SubTxID subTxID)
    {
        libbitcoin::data_chunk tx_data;
        libbitcoin::decode_base16(tx_data, *m_SwapWithdrawRawTx);
        libbitcoin::chain::transaction withdrawTX = libbitcoin::chain::transaction::factory_from_data(tx_data);

        auto addressVersion = GetAddressVersion();
        bool quickRefund = IsQuickRefundAvailable();
        libbitcoin::ec_secret localSecret = GetWithdrawTxLocalSecret(m_tx, subTxID, quickRefund);
        uint32_t input_index = 0;
        auto contractScript = CreateAtomicSwapContract(m_tx, m_isBtcOwner, addressVersion);
        libbitcoin::endorsement sig;
        if (NeedSignValue())
        {
            uint64_t total = m_tx.GetMandatoryParameter<Amount>(beam::wallet::TxParameterID::AtomicSwapAmount);
            libbitcoin::chain::script::create_endorsement(
                sig, localSecret, contractScript, withdrawTX, input_index, GetSighashAlgorithm(), libbitcoin::machine::script_version::zero, total);
        }
        else
        {
            libbitcoin::chain::script::create_endorsement(
                sig, localSecret, contractScript, withdrawTX, input_index, GetSighashAlgorithm());
        }

        // Create input script
        libbitcoin::machine::operation::list sig_script;

        if (SubTxIndex::REFUND_TX == subTxID && !quickRefund)
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
            if (NeedSignValue())
            {
                uint64_t total = m_tx.GetMandatoryParameter<Amount>(beam::wallet::TxParameterID::AtomicSwapAmount);
                libbitcoin::chain::script::create_endorsement(
                    secretSig, secret, contractScript, withdrawTX, input_index, GetSighashAlgorithm(), libbitcoin::machine::script_version::zero, total);
            }
            else
            {
                libbitcoin::chain::script::create_endorsement(
                    secretSig, secret, contractScript, withdrawTX, input_index, GetSighashAlgorithm());
            }

            // 0 <their sig> <secret sig> 1
            sig_script.emplace_back(libbitcoin::machine::operation(libbitcoin::machine::opcode(0)));
            sig_script.emplace_back(libbitcoin::machine::operation(sig));
            sig_script.emplace_back(libbitcoin::machine::operation(secretSig));
            sig_script.emplace_back(libbitcoin::machine::operation(libbitcoin::machine::opcode::push_positive_1));
        }

        sig_script.emplace_back(libbitcoin::machine::operation(contractScript.to_data(false)));

        libbitcoin::chain::script input_script(sig_script);

        // Add input script to first input in transaction
        withdrawTX.inputs()[0].set_script(input_script);

        return libbitcoin::encode_base16(withdrawTX.to_data());
    }

    bool BitcoinSide::CanUseSegwit() const
    {
        auto peerProtoVersion = m_tx.GetMandatoryParameter<uint32_t>(TxParameterID::PeerProtoVersion);

        return IsSegwitSupported() && peerProtoVersion >= kSwapSegwitSupportMinProtoVersion;
    }

    void BitcoinSide::OnGetSwapLockTxConfirmations(const bitcoin::IBridge::Error& /*error*/, const std::string& hexScript, Amount amount, uint32_t confirmations)
    {
        try
        {
            if (hexScript.empty())
            {
                return;
            }

            // validate amount
            {
                Amount swapAmount = m_tx.GetMandatoryParameter<Amount>(TxParameterID::AtomicSwapAmount);
                if (swapAmount > amount)
                {
                    BEAM_LOG_ERROR() << m_tx.GetTxID() << "[" << static_cast<SubTxID>(SubTxIndex::LOCK_TX) << "]"
                        << " Unexpected amount, expected: " << swapAmount << ", got: " << amount;
                    m_tx.SetParameter(TxParameterID::InternalFailureReason, TxFailureReason::SwapInvalidAmount, false, SubTxIndex::LOCK_TX);
                    m_tx.UpdateAsync();
                    return;
                }
            }

            // validate contract script
            libbitcoin::data_chunk scriptData;
            libbitcoin::decode_base16(scriptData, hexScript);
            auto script = libbitcoin::chain::script::factory_from_data(scriptData, false);

            auto contractScript = CreateAtomicSwapContract(m_tx, m_isBtcOwner, GetAddressVersion());
            libbitcoin::machine::operation::list inputScriptOperations;

            if (CanUseSegwit())
            {
                libbitcoin::hash_digest contractScriptHash = libbitcoin::sha256_hash(contractScript.to_data(false));
                inputScriptOperations.emplace_back(libbitcoin::machine::operation(libbitcoin::machine::opcode::push_size_0));
                inputScriptOperations.emplace_back(libbitcoin::machine::operation(libbitcoin::to_chunk(contractScriptHash)));
            }
            else
            {
                inputScriptOperations = libbitcoin::chain::script::to_pay_script_hash_pattern(libbitcoin::bitcoin_short_hash(contractScript.to_data(false)));
            }
            libbitcoin::chain::script inputScript{inputScriptOperations};
            
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
                BEAM_LOG_DEBUG() << m_tx.GetTxID() << "[" << static_cast<SubTxID>(SubTxIndex::LOCK_TX) << "] " << confirmations << "/"
                    << GetTxMinConfirmations(SubTxIndex::LOCK_TX) << " confirmations for Lock TX are received.";
                m_SwapLockTxConfirmations = confirmations;
                m_tx.SetParameter(TxParameterID::Confirmations, m_SwapLockTxConfirmations, true, SubTxIndex::LOCK_TX);
                if (m_SwapLockTxConfirmations >= GetTxMinConfirmations(SubTxIndex::LOCK_TX))
                {
                    m_tx.UpdateAsync();
                }
            }
        }
        catch (const TransactionFailedException& ex)
        {
            BEAM_LOG_ERROR() << m_tx.GetTxID() << " exception msg: " << ex.what();
            m_tx.SetParameter(TxParameterID::InternalFailureReason, ex.GetReason(), false, SubTxIndex::LOCK_TX);
            m_tx.UpdateAsync();
        }
        catch (const std::exception& ex)
        {
            BEAM_LOG_ERROR() << m_tx.GetTxID() << " exception msg: " << ex.what();

            m_tx.SetParameter(TxParameterID::InternalFailureReason, TxFailureReason::Unknown, false, SubTxIndex::LOCK_TX);
            m_tx.UpdateAsync();
        }
    }

    void BitcoinSide::OnGetBlockCount(const bitcoin::IBridge::Error& error, uint64_t blockCount, bool notify)
    {
        try
        {
            if (error.m_type != bitcoin::IBridge::None)
            {
                m_tx.UpdateOnNextTip();
                return;
            }

            if (m_blockCount != blockCount)
            {
                m_blockCount = blockCount;
                m_tx.SetParameter(TxParameterID::AtomicSwapExternalHeight, m_blockCount, true);

                if (notify)
                {
                    m_tx.UpdateAsync();
                }
            }
        }
        catch (const std::exception& ex)
        {
            BEAM_LOG_ERROR() << m_tx.GetTxID() << " exception msg: " << ex.what();

            m_tx.SetParameter(TxParameterID::InternalFailureReason, TxFailureReason::Unknown, false, SubTxIndex::LOCK_TX);
            m_tx.UpdateAsync();
        }
    }
}