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

#include "swap_transaction.h"

#include "bitcoin/bitcoin.hpp"
#include "nlohmann/json.hpp"

#include "lock_tx_builder.h"
#include "shared_tx_builder.h"

using namespace ECC;
using json = nlohmann::json;

namespace beam::wallet
{
    namespace
    {
        uint32_t kBeamLockTimeInBlocks = 24 * 60;
        uint32_t kBTCLockTimeSec = 2 * 24 * 60 * 60;
        uint32_t kBTCMinTxConfirmations = 6;

        void InitSecret(BaseTransaction& transaction, SubTxID subTxID)
        {
            NoLeak<uintBig> preimage;
            GenRandom(preimage.V);
            transaction.SetParameter(TxParameterID::PreImage, preimage.V, false, subTxID);
        }

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

        libbitcoin::chain::script CreateAtomicSwapContract(const BaseTransaction& transaction, bool isBtcOwner)
        {
            Timestamp locktime = transaction.GetMandatoryParameter<Timestamp>(TxParameterID::AtomicSwapExternalLockTime);
            std::string peerSwapAddress = transaction.GetMandatoryParameter<std::string>(TxParameterID::AtomicSwapPeerAddress);
            std::string swapAddress = transaction.GetMandatoryParameter<std::string>(TxParameterID::AtomicSwapAddress);

            // load secret or secretHash
            Hash::Value lockImage(Zero);

            if (NoLeak<uintBig> preimage; transaction.GetParameter(TxParameterID::PreImage, preimage.V, SubTxIndex::BEAM_REDEEM_TX))
            {
                Hash::Processor() << preimage.V >> lockImage;
            }
            else
            {
                lockImage = transaction.GetMandatoryParameter<uintBig>(TxParameterID::PeerLockImage, SubTxIndex::BEAM_REDEEM_TX);
            }

            libbitcoin::data_chunk secretHash = libbitcoin::to_chunk(lockImage.m_pData);
            libbitcoin::wallet::payment_address senderAddress(isBtcOwner ? swapAddress : peerSwapAddress);
            libbitcoin::wallet::payment_address receiverAddress(isBtcOwner ? peerSwapAddress : swapAddress);

            return AtomicSwapContract(senderAddress.hash(), receiverAddress.hash(), locktime, secretHash, secretHash.size());
        }
    }

    AtomicSwapTransaction::AtomicSwapTransaction(INegotiatorGateway& gateway
                                               , beam::IWalletDB::Ptr walletDB
                                               , const TxID& txID)
        : BaseTransaction(gateway, walletDB, txID)
    {

    }

    void AtomicSwapTransaction::SetNextState(State state)
    {
        SetState(state);
        UpdateAsync();
    }

    void AtomicSwapTransaction::UpdateAsync()
    {
        if (!m_EventToUpdate)
        {
            m_EventToUpdate = io::AsyncEvent::create(io::Reactor::get_Current(), [this]() { UpdateImpl(); });
        }

        m_EventToUpdate->post();
    }

    TxType AtomicSwapTransaction::GetType() const
    {
        return TxType::AtomicSwap;
    }

    AtomicSwapTransaction::State AtomicSwapTransaction::GetState(SubTxID subTxID) const
    {
        State state = State::Initial;
        GetParameter(TxParameterID::State, state, subTxID);
        return state;
    }

    AtomicSwapTransaction::SubTxState AtomicSwapTransaction::GetSubTxState(SubTxID subTxID) const
    {
        SubTxState state = SubTxState::Initial;
        GetParameter(TxParameterID::State, state, subTxID);
        return state;
    }

    void AtomicSwapTransaction::UpdateImpl()
    {
        State state = GetState(kDefaultSubTxID);
        bool isBeamOwner = IsBeamSide();

        switch (state)
        {
        case State::Initial:
        {
            if (!LoadSwapAddress())
                break;

            if (!isBeamOwner)
            {
                InitSecret(*this, SubTxIndex::BEAM_REDEEM_TX);
            }

            SetNextState(State::Invitation);
            break;
        }
        case State::Invitation:
        {
            if (IsInitiator())
            {
                // init locktime
                InitExternalLockTime();
                SendInvitation();
            }
            
            SetNextState(State::BuildingBeamLockTX);
            break;
        }
        case State::BuildingBeamLockTX:
        {
            auto lockTxState = BuildBeamLockTx();
            if (lockTxState != SubTxState::Constructed)
                break;

            SetNextState(State::BuildingBeamRefundTX);
            break;
        }
        case State::BuildingBeamRefundTX:
        {
            auto subTxState = BuildBeamWithdrawTx(SubTxIndex::BEAM_REFUND_TX, m_WithdrawTx);
            if (subTxState != SubTxState::Constructed)
                break;

            m_WithdrawTx.reset();
            SetNextState(State::BuildingBeamRedeemTX);
            break;
        }
        case State::BuildingBeamRedeemTX:
        {
            auto subTxState = BuildBeamWithdrawTx(SubTxIndex::BEAM_REDEEM_TX, m_WithdrawTx);
            if (subTxState != SubTxState::Constructed)
                break;

            m_WithdrawTx.reset();
            SetNextState(State::HandlingContractTX);
            break;
        }
        case State::HandlingContractTX:
        {
            if (!isBeamOwner)
            {
                if (!SendExternalLockTx())
                    break;

                SendExternalTxDetails();
            }
            else
            {
                // TODO: check current blockchain height and cancel swap if too late

                if (!ConfirmExternalLockTx())
                    break;
            }
            SetNextState(State::SendingBeamLockTX);
            break;
        }
        case State::SendingRefundTX:
        {
            assert(!isBeamOwner);
            if (!SendExternalWithdrawTx(SubTxIndex::REFUND_TX))
                break;

            assert(false && "Not implemented yet.");
            break;
        }
        case State::SendingRedeemTX:
        {
            assert(isBeamOwner);
            if (!SendExternalWithdrawTx(SubTxIndex::REDEEM_TX))
                break;
            
            LOG_DEBUG() << GetTxID() << " Redeem TX completed!";
            SetNextState(State::CompleteSwap);
            break;
        }
        case State::SendingBeamLockTX:
        {
            if (!m_LockTx && isBeamOwner)
            {
                BuildBeamLockTx();
            }

            if (m_LockTx && !SendSubTx(m_LockTx, SubTxIndex::BEAM_LOCK_TX))
                break;

            if (!isBeamOwner)
            {
                // validate second chain height (second coin timelock)
                // SetNextState(State::SendingRefundTX);
            }

            if (!CompleteSubTx(SubTxIndex::BEAM_LOCK_TX))
                break;
            
            LOG_DEBUG() << GetTxID()<< " Beam Lock TX completed.";
            SetNextState(State::SendingBeamRedeemTX);
            break;
        }
        case State::SendingBeamRedeemTX:
        {
            if (isBeamOwner)
            {
                UpdateOnNextTip();

                if (IsBeamLockTimeExpired())
                {
                    LOG_DEBUG() << GetTxID() << " Beam locktime expired.";

                    SetNextState(State::SendingBeamRefundTX);
                    break;
                }

                // request kernel body for getting secret(preimage)
                ECC::uintBig preimage(Zero);
                if (!GetPreimageFromChain(preimage, SubTxIndex::BEAM_REDEEM_TX))
                    break;

                LOG_DEBUG() << GetTxID() << " Got preimage: " << preimage;

                // Redeem second Coin
                SetNextState(State::SendingRedeemTX);
            }
            else
            {
                if (!CompleteBeamWithdrawTx(SubTxIndex::BEAM_REDEEM_TX))
                    break;

                LOG_DEBUG() << GetTxID() << " Beam Redeem TX completed!";
                SetNextState(State::CompleteSwap);
            }
            break;
        }
        case State::SendingBeamRefundTX:
        {
            assert(isBeamOwner);
            if (!CompleteBeamWithdrawTx(SubTxIndex::BEAM_REFUND_TX))
                break;

            LOG_DEBUG() << GetTxID() << " Beam Refund TX completed!";
            SetNextState(State::CompleteSwap);
            break;
        }
        case State::CompleteSwap:
        {
            LOG_DEBUG() << GetTxID() << " Swap completed.";
            CompleteTx();
            break;
        }

        default:
            break;
        }
    }

    bool AtomicSwapTransaction::LoadSwapAddress()
    {
        // load or generate BTC address
        if (std::string swapAddress; !GetParameter(TxParameterID::AtomicSwapAddress, swapAddress))
        {
            auto bitcoin_rpc = m_Gateway.get_bitcoin_rpc();
            if (bitcoin_rpc)
            {
                // is need to setup type 'legacy'?
                bitcoin_rpc->getRawChangeAddress(BIND_THIS_MEMFN(OnGetRawChangeAddress));
            }
            return false;
        }
        return true;
    }

    void AtomicSwapTransaction::InitExternalLockTime()
    {
        auto externalLockTime = GetMandatoryParameter<Timestamp>(TxParameterID::CreateTime) + kBTCLockTimeSec;
        SetParameter(TxParameterID::AtomicSwapExternalLockTime, externalLockTime);
    }

    bool AtomicSwapTransaction::SendExternalLockTx()
    {
        auto lockTxState = BuildLockTx();
        if (lockTxState != SwapTxState::Constructed)
            return false;

        // send contractTx
        assert(m_SwapLockRawTx.is_initialized());

        if (!RegisterExternalTx(*m_SwapLockRawTx, SubTxIndex::LOCK_TX))
            return false;

        return true;
    }

    bool AtomicSwapTransaction::SendExternalWithdrawTx(SubTxID subTxID)
    {
        if (bool isRegistered = false; !GetParameter(TxParameterID::TransactionRegistered, isRegistered, SubTxIndex::REFUND_TX))
        {
            auto refundTxState = BuildWithdrawTx(SubTxIndex::REFUND_TX);
            if (refundTxState != SwapTxState::Constructed)
                return false;

            assert(m_SwapWithdrawRawTx.is_initialized());
        }

        if (!RegisterExternalTx(*m_SwapWithdrawRawTx, SubTxIndex::REFUND_TX))
            return false;

        // TODO: check confirmations

        return true;
    }

    bool AtomicSwapTransaction::ConfirmExternalLockTx()
    {
        // wait TxID from peer
        std::string txID;
        if (!GetParameter(TxParameterID::AtomicSwapExternalTxID, txID, SubTxIndex::LOCK_TX))
            return false;

        if (m_SwapLockTxConfirmations < kBTCMinTxConfirmations)
        {
            // validate expired?

            // TODO: timeout ?
            GetSwapLockTxConfirmations();
            UpdateOnNextTip();
            return false;
        }

        return true;
    }

    bool AtomicSwapTransaction::CompleteBeamWithdrawTx(SubTxID subTxID)
    {
        if (!m_WithdrawTx)
        {
            BuildBeamWithdrawTx(subTxID, m_WithdrawTx);
        }

        if (m_WithdrawTx && !SendSubTx(m_WithdrawTx, subTxID))
        {
            return false;
        }

        if (!CompleteSubTx(subTxID))
        {
            return false;
        }

        return true;
    }

    AtomicSwapTransaction::SwapTxState AtomicSwapTransaction::BuildLockTx()
    {
        SwapTxState swapTxState = SwapTxState::Initial;
        GetParameter(TxParameterID::State, swapTxState, SubTxIndex::LOCK_TX);
        
        if (swapTxState == SwapTxState::Initial)
        {
            auto contractScript = CreateAtomicSwapContract(*this, true);
            Amount swapAmount = GetMandatoryParameter<Amount>(TxParameterID::AtomicSwapAmount);

            libbitcoin::chain::transaction contractTx;
            libbitcoin::chain::output output(swapAmount, contractScript);
            contractTx.outputs().push_back(output);

            std::string hexTx = libbitcoin::encode_base16(contractTx.to_data());

            auto bitcoin_rpc = m_Gateway.get_bitcoin_rpc();

            if (!bitcoin_rpc)
            {
                return swapTxState;
            }

            bitcoin_rpc->fundRawTransaction(hexTx, BIND_THIS_MEMFN(OnFundRawTransaction));

            SetState(SwapTxState::CreatingTx, SubTxIndex::LOCK_TX);
            return SwapTxState::CreatingTx;
        }

        if (swapTxState == SwapTxState::CreatingTx)
        {
            // TODO: implement
        }

        // TODO: check
        return swapTxState;
    }

    AtomicSwapTransaction::SwapTxState AtomicSwapTransaction::BuildWithdrawTx(SubTxID subTxID)
    {
        SwapTxState swapTxState = SwapTxState::Initial;
        GetParameter(TxParameterID::State, swapTxState, subTxID);

        if (swapTxState == SwapTxState::Initial)
        {
            // TODO: implement fee calculation
            Amount fee = 1000;

            Amount swapAmount = GetMandatoryParameter<Amount>(TxParameterID::AtomicSwapAmount);
            swapAmount = swapAmount - fee;
            std::string swapAddress = GetMandatoryParameter<std::string>(TxParameterID::AtomicSwapAddress);
            uint32_t outputIndex = GetMandatoryParameter<uint32_t>(TxParameterID::AtomicSwapExternalTxOutputIndex, SubTxIndex::LOCK_TX);
            auto swapLockTxID = GetMandatoryParameter<std::string>(TxParameterID::AtomicSwapExternalTxID, SubTxIndex::LOCK_TX);

            std::vector<std::string> args;
            args.emplace_back("[{\"txid\": \"" + swapLockTxID + "\", \"vout\":" + std::to_string(outputIndex) + ", \"Sequence\": " + std::to_string(libbitcoin::max_input_sequence - 1) + " }]");
            args.emplace_back("[{\"" + swapAddress + "\": " + std::to_string(double(swapAmount) / libbitcoin::satoshi_per_bitcoin) + "}]");
            if (subTxID == SubTxIndex::REFUND_TX)
            {
                Timestamp locktime = GetMandatoryParameter<Timestamp>(TxParameterID::AtomicSwapExternalLockTime);
                args.emplace_back(std::to_string(locktime));
            }

            auto bitcoin_rpc = m_Gateway.get_bitcoin_rpc();
            
            if (!bitcoin_rpc)
            {
                return swapTxState;
            }

            bitcoin_rpc->createRawTransaction(args, BIND_THIS_MEMFN(OnCreateWithdrawTransaction));

            SetState(SwapTxState::CreatingTx, subTxID);
            return SwapTxState::CreatingTx;
        }

        if (swapTxState == SwapTxState::CreatingTx)
        {
            std::string swapAddress = GetMandatoryParameter<std::string>(TxParameterID::AtomicSwapAddress);
            auto callback = [this, subTxID](const std::string& response) {
                OnDumpPrivateKey(subTxID, response);
            };

            if (auto bitcoin_rpc = m_Gateway.get_bitcoin_rpc(); bitcoin_rpc)
            {
                bitcoin_rpc->dumpPrivKey(swapAddress, callback);
            }
        }

        if (swapTxState == SwapTxState::Constructed && !m_SwapWithdrawRawTx.is_initialized())
        {
            m_SwapWithdrawRawTx = GetMandatoryParameter<std::string>(TxParameterID::AtomicSwapExternalTx, subTxID);
        }

        return swapTxState;
    }

    bool AtomicSwapTransaction::RegisterExternalTx(const std::string& rawTransaction, SubTxID subTxID)
    {
        bool isRegistered = false;
        if (!GetParameter(TxParameterID::TransactionRegistered, isRegistered, subTxID))
        {
            auto callback = [this, subTxID](const std::string& response) {
                json reply = json::parse(response);
                assert(reply["error"].empty());

                auto txID = reply["result"].get<std::string>();
                bool isRegistered = !txID.empty();
                SetParameter(TxParameterID::TransactionRegistered, isRegistered, false, subTxID);

                if (!txID.empty())
                {
                    SetParameter(TxParameterID::AtomicSwapExternalTxID, txID, false, subTxID);
                }

                Update();
            };

            if (auto bitcoin_rpc = m_Gateway.get_bitcoin_rpc(); bitcoin_rpc)
            {
                bitcoin_rpc->sendRawTransaction(rawTransaction, callback);
            }
            return isRegistered;
        }

        if (!isRegistered)
        {
            OnFailed(TxFailureReason::FailedToRegister, true);
        }

        return isRegistered;
    }

    void AtomicSwapTransaction::GetSwapLockTxConfirmations()
    {
        auto txID = GetMandatoryParameter<std::string>(TxParameterID::AtomicSwapExternalTxID, SubTxIndex::LOCK_TX);
        uint32_t outputIndex = GetMandatoryParameter<uint32_t>(TxParameterID::AtomicSwapExternalTxOutputIndex, SubTxIndex::LOCK_TX);

        if (auto bitcoin_rpc = m_Gateway.get_bitcoin_rpc(); bitcoin_rpc)
        {
            bitcoin_rpc->getTxOut(txID, outputIndex, BIND_THIS_MEMFN(OnGetSwapLockTxConfirmations));
        }
    }

    AtomicSwapTransaction::SubTxState AtomicSwapTransaction::BuildBeamLockTx()
    {
        // load state
        SubTxState lockTxState = SubTxState::Initial;
        GetParameter(TxParameterID::State, lockTxState, SubTxIndex::BEAM_LOCK_TX);

        bool isBeamOwner = IsBeamSide();
        // TODO: check
        Amount fee = 0;
        GetParameter(TxParameterID::Fee, fee);

        if (!fee)
        {
            return lockTxState;
        }

        auto lockTxBuilder = std::make_unique<LockTxBuilder>(*this, GetAmount(), fee);

        bool newGenerated = lockTxBuilder->GenerateBlindingExcess();
        if (newGenerated && lockTxState != SubTxState::Initial)
        {
            OnFailed(TxFailureReason::InvalidState);
            return lockTxState;
        }

        if (!lockTxBuilder->GetInitialTxParams() && lockTxState == SubTxState::Initial)
        {
            // TODO: check expired!

            if (isBeamOwner)
            {
                lockTxBuilder->SelectInputs();
                lockTxBuilder->AddChange();
                lockTxBuilder->CreateOutputs();
            }

            if (!lockTxBuilder->FinalizeOutputs())
            {
                // TODO: transaction is too big :(
            }

            UpdateTxDescription(TxStatus::InProgress);
        }

        lockTxBuilder->GenerateNonce();

        if (!lockTxBuilder->GetPeerPublicExcessAndNonce())
        {
            if (lockTxState == SubTxState::Initial && isBeamOwner)
            {
                SendLockTxInvitation(*lockTxBuilder);
                SetState(SubTxState::Invitation, SubTxIndex::BEAM_LOCK_TX);
                lockTxState = SubTxState::Invitation;
            }
            return lockTxState;
        }

        lockTxBuilder->LoadSharedParameters();
        lockTxBuilder->CreateKernel();
        lockTxBuilder->SignPartial();

        if (lockTxState == SubTxState::Initial || lockTxState == SubTxState::Invitation)
        {
            if (!lockTxBuilder->SharedUTXOProofPart2(isBeamOwner))
            {
                return lockTxState;
            }
            SendMultiSigProofPart2(*lockTxBuilder, isBeamOwner);
            SetState(SubTxState::SharedUTXOProofPart2, SubTxIndex::BEAM_LOCK_TX);
            lockTxState = SubTxState::SharedUTXOProofPart2;
            return lockTxState;
        }

        if (!lockTxBuilder->GetPeerSignature())
        {
            return lockTxState;
        }

        if (!lockTxBuilder->IsPeerSignatureValid())
        {
            LOG_INFO() << GetTxID() << " Peer signature is invalid.";
            return lockTxState;
        }

        lockTxBuilder->FinalizeSignature();

        if (lockTxState == SubTxState::SharedUTXOProofPart2)
        {
            if (!lockTxBuilder->SharedUTXOProofPart3(isBeamOwner))
            {
                return lockTxState;
            }
            SendMultiSigProofPart3(*lockTxBuilder, isBeamOwner);
            SetState(SubTxState::Constructed, SubTxIndex::BEAM_LOCK_TX);
            lockTxState = SubTxState::Constructed;
        }

        if (isBeamOwner && lockTxState == SubTxState::Constructed)
        {
            // Create TX
            auto transaction = lockTxBuilder->CreateTransaction();
            TxBase::Context::Params pars;
            TxBase::Context context(pars);
            if (!transaction->IsValid(context))
            {
                // TODO: check
                OnFailed(TxFailureReason::InvalidTransaction, true);
                return lockTxState;
            }

            // TODO: return
            m_LockTx = transaction;
        }

        return lockTxState;
    }

    AtomicSwapTransaction::SubTxState AtomicSwapTransaction::BuildBeamWithdrawTx(SubTxID subTxID, Transaction::Ptr& resultTx)
    {
        SubTxState subTxState = GetSubTxState(subTxID);

        Amount withdrawFee = 0;
        Amount withdrawAmount = 0;

        if (!GetParameter(TxParameterID::Amount, withdrawAmount, subTxID))
        {
            // TODO: calculating fee!
            withdrawFee = 0;
            withdrawAmount = GetAmount() - withdrawFee;

            SetParameter(TxParameterID::Amount, withdrawAmount, subTxID);
            SetParameter(TxParameterID::Fee, withdrawFee, subTxID);
        }

        bool isTxOwner = (IsBeamSide() && (SubTxIndex::BEAM_REFUND_TX == subTxID)) || (!IsBeamSide() && (SubTxIndex::BEAM_REDEEM_TX == subTxID));
        SharedTxBuilder builder{ *this, subTxID, withdrawAmount, withdrawFee };

        if (!builder.GetSharedParameters())
        {
            return subTxState;
        }

        // send invite to get 
        if (!builder.GetInitialTxParams() && subTxState == SubTxState::Initial)
        {
            // TODO: check expired!
            builder.InitTx(isTxOwner);
        }

        bool newGenerated = builder.GenerateBlindingExcess();
        if (newGenerated && subTxState != SubTxState::Initial)
        {
            OnFailed(TxFailureReason::InvalidState);
            return subTxState;
        }

        builder.GenerateNonce();
        builder.CreateKernel();

        if (!builder.GetPeerPublicExcessAndNonce())
        {
            if (subTxState == SubTxState::Initial && isTxOwner)
            {
                SendSharedTxInvitation(builder, SubTxIndex::BEAM_REDEEM_TX == subTxID);
                SetState(SubTxState::Invitation, subTxID);
                subTxState = SubTxState::Invitation;
            }
            return subTxState;
        }

        builder.SignPartial();

        if (!builder.GetPeerSignature())
        {
            if (subTxState == SubTxState::Initial && !isTxOwner)
            {
                // invited participant
                ConfirmSharedTxInvitation(builder);
                SetState(SubTxState::Constructed, subTxID);
                subTxState = SubTxState::Constructed;
            }
            return subTxState;
        }

        if (!builder.IsPeerSignatureValid())
        {
            LOG_INFO() << GetTxID() << " Peer signature is invalid.";
            return subTxState;
        }

        builder.FinalizeSignature();

        SetState(SubTxState::Constructed, subTxID);
        subTxState = SubTxState::Constructed;

        if (isTxOwner)
        {
            auto transaction = builder.CreateTransaction();
            TxBase::Context::Params pars;
            TxBase::Context context(pars);
            if (!transaction->IsValid(context))
            {
                // TODO: check
                OnFailed(TxFailureReason::InvalidTransaction, true);
                return subTxState;
            }
            resultTx = transaction;
        }

        return subTxState;
    }

    bool AtomicSwapTransaction::SendSubTx(Transaction::Ptr transaction, SubTxID subTxID)
    {
        bool isRegistered = false;
        if (!GetParameter(TxParameterID::TransactionRegistered, isRegistered, subTxID))
        {
            m_Gateway.register_tx(GetTxID(), transaction, subTxID);
            return isRegistered;
        }

        if (!isRegistered)
        {
            OnFailed(TxFailureReason::FailedToRegister, true);
            return isRegistered;
        }

        return isRegistered;
    }

    bool AtomicSwapTransaction::IsBeamLockTimeExpired() const
    {
        Height lockTimeHeight = MaxHeight;
        GetParameter(TxParameterID::MinHeight, lockTimeHeight);

        Block::SystemState::Full state;

        return GetTip(state) && state.m_Height > (lockTimeHeight + kBeamLockTimeInBlocks);
    }

    bool AtomicSwapTransaction::CompleteSubTx(SubTxID subTxID)
    {
        Height hProof = 0;
        GetParameter(TxParameterID::KernelProofHeight, hProof, subTxID);
        if (!hProof)
        {
            Merkle::Hash kernelID = GetMandatoryParameter<Merkle::Hash>(TxParameterID::KernelID, subTxID);
            m_Gateway.confirm_kernel(GetTxID(), kernelID, subTxID);
            return false;
        }

        if ((SubTxIndex::BEAM_REDEEM_TX == subTxID) || (SubTxIndex::BEAM_REFUND_TX == subTxID))
        {
            // store Coin in DB
            auto amount = GetMandatoryParameter<Amount>(TxParameterID::Amount, subTxID);
            Coin withdrawUtxo(amount);

            withdrawUtxo.m_createTxId = GetTxID();
            withdrawUtxo.m_ID = GetMandatoryParameter<Coin::ID>(TxParameterID::SharedCoinID, subTxID);

            GetWalletDB()->store(withdrawUtxo);
        }

        std::vector<Coin> modified;
        m_WalletDB->visit([&](const Coin& coin)
        {
            bool bIn = (coin.m_createTxId == m_ID);
            bool bOut = (coin.m_spentTxId == m_ID);
            if (bIn || bOut)
            {
                modified.emplace_back();
                Coin& c = modified.back();
                c = coin;

                if (bIn)
                {
                    c.m_confirmHeight = std::min(c.m_confirmHeight, hProof);
                    c.m_maturity = hProof + Rules::get().Maturity.Std; // so far we don't use incubation for our created outputs
                }
                if (bOut)
                    c.m_spentHeight = std::min(c.m_spentHeight, hProof);
            }

            return true;
        });

        GetWalletDB()->save(modified);

        return true;
    }

    bool AtomicSwapTransaction::GetPreimageFromChain(ECC::uintBig& preimage, SubTxID subTxID) const
    {
        Height hProof = 0;
        GetParameter(TxParameterID::KernelProofHeight, hProof, subTxID);
        GetParameter(TxParameterID::PreImage, preimage, subTxID);

        if (!hProof)
        {
            Merkle::Hash kernelID = GetMandatoryParameter<Merkle::Hash>(TxParameterID::KernelID, SubTxIndex::BEAM_REDEEM_TX);
            m_Gateway.get_kernel(GetTxID(), kernelID, subTxID);
            return false;
        }

        return true;
    }

    Amount AtomicSwapTransaction::GetAmount() const
    {
        if (!m_Amount.is_initialized())
        {
            m_Amount = GetMandatoryParameter<Amount>(TxParameterID::Amount);
        }
        return *m_Amount;
    }

    bool AtomicSwapTransaction::IsSender() const
    {
        if (!m_IsSender.is_initialized())
        {
            m_IsSender = GetMandatoryParameter<bool>(TxParameterID::IsSender);
        }
        return *m_IsSender;
    }

    bool AtomicSwapTransaction::IsBeamSide() const
    {
        if (!m_IsBeamSide.is_initialized())
        {
            bool isBeamSide = false;
            GetParameter(TxParameterID::AtomicSwapIsBeamSide, isBeamSide);
            m_IsBeamSide = isBeamSide;
        }
        return *m_IsBeamSide;
    }

    void AtomicSwapTransaction::SendInvitation()
    {
        auto swapAmount = GetMandatoryParameter<Amount>(TxParameterID::AtomicSwapAmount);
        auto swapCoin = GetMandatoryParameter<AtomicSwapCoin>(TxParameterID::AtomicSwapCoin);
        auto swapAddress = GetMandatoryParameter<std::string>(TxParameterID::AtomicSwapAddress);
        auto swapLockTime = GetMandatoryParameter<Timestamp>(TxParameterID::AtomicSwapExternalLockTime);

        // send invitation
        SetTxParameter msg;
        msg.AddParameter(TxParameterID::Amount, GetAmount())
            .AddParameter(TxParameterID::Fee, GetMandatoryParameter<Amount>(TxParameterID::Fee))
            .AddParameter(TxParameterID::IsSender, !IsSender())
            .AddParameter(TxParameterID::AtomicSwapAmount, swapAmount)
            .AddParameter(TxParameterID::AtomicSwapCoin, swapCoin)
            .AddParameter(TxParameterID::AtomicSwapPeerAddress, swapAddress)
            .AddParameter(TxParameterID::AtomicSwapExternalLockTime, swapLockTime)
            .AddParameter(TxParameterID::AtomicSwapIsBeamSide, !IsBeamSide())
            .AddParameter(TxParameterID::PeerProtoVersion, s_ProtoVersion);

        if (!SendTxParameters(std::move(msg)))
        {
            OnFailed(TxFailureReason::FailedToSendParameters, false);
        }
    }

    void AtomicSwapTransaction::SendExternalTxDetails()
    {
        auto txID = GetMandatoryParameter<std::string>(TxParameterID::AtomicSwapExternalTxID, SubTxIndex::LOCK_TX);
        uint32_t outputIndex = GetMandatoryParameter<uint32_t>(TxParameterID::AtomicSwapExternalTxOutputIndex, SubTxIndex::LOCK_TX);
        std::string swapAddress = GetMandatoryParameter<std::string>(TxParameterID::AtomicSwapAddress);

        SetTxParameter msg;
        msg.AddParameter(TxParameterID::AtomicSwapPeerAddress, swapAddress)
            .AddParameter(TxParameterID::SubTxIndex, SubTxIndex::LOCK_TX)
            .AddParameter(TxParameterID::AtomicSwapExternalTxID, txID)
            .AddParameter(TxParameterID::AtomicSwapExternalTxOutputIndex, outputIndex);

        if (!SendTxParameters(std::move(msg)))
        {
            OnFailed(TxFailureReason::FailedToSendParameters, false);
        }
    }

    void AtomicSwapTransaction::SendLockTxInvitation(const LockTxBuilder& lockBuilder)
    {
        auto swapAddress = GetMandatoryParameter<std::string>(TxParameterID::AtomicSwapAddress);

        SetTxParameter msg;
        msg.AddParameter(TxParameterID::AtomicSwapPeerAddress, swapAddress)
            .AddParameter(TxParameterID::Fee, lockBuilder.GetFee())
            .AddParameter(TxParameterID::SubTxIndex, SubTxIndex::BEAM_LOCK_TX)
            .AddParameter(TxParameterID::MinHeight, lockBuilder.GetMinHeight())
            .AddParameter(TxParameterID::PeerPublicExcess, lockBuilder.GetPublicExcess())
            .AddParameter(TxParameterID::PeerPublicNonce, lockBuilder.GetPublicNonce());

        if (!SendTxParameters(std::move(msg)))
        {
            OnFailed(TxFailureReason::FailedToSendParameters, false);
        }
    }

    void AtomicSwapTransaction::SendMultiSigProofPart2(const LockTxBuilder& lockBuilder, bool isMultiSigProofOwner)
    {
        SetTxParameter msg;
        msg.AddParameter(TxParameterID::SubTxIndex, SubTxIndex::BEAM_LOCK_TX)
            .AddParameter(TxParameterID::PeerSignature, lockBuilder.GetPartialSignature())
            .AddParameter(TxParameterID::PeerOffset, lockBuilder.GetOffset())
            .AddParameter(TxParameterID::PeerPublicSharedBlindingFactor, lockBuilder.GetPublicSharedBlindingFactor());
        if (isMultiSigProofOwner)
        {
            auto proofPartialMultiSig = lockBuilder.GetProofPartialMultiSig();
            msg.AddParameter(TxParameterID::PeerSharedBulletProofMSig, proofPartialMultiSig);
        }
        else
        {
            auto bulletProof = lockBuilder.GetSharedProof();
            msg.AddParameter(TxParameterID::PeerPublicExcess, lockBuilder.GetPublicExcess())
                .AddParameter(TxParameterID::PeerPublicNonce, lockBuilder.GetPublicNonce())
                .AddParameter(TxParameterID::PeerSharedBulletProofPart2, bulletProof.m_Part2);
        }

        if (!SendTxParameters(std::move(msg)))
        {
            OnFailed(TxFailureReason::FailedToSendParameters, false);
        }
    }

    void AtomicSwapTransaction::SendMultiSigProofPart3(const LockTxBuilder& lockBuilder, bool isMultiSigProofOwner)
    {
        if (!isMultiSigProofOwner)
        {
            auto bulletProof = lockBuilder.GetSharedProof();
            SetTxParameter msg;
            msg.AddParameter(TxParameterID::SubTxIndex, SubTxIndex::BEAM_LOCK_TX)
                .AddParameter(TxParameterID::PeerSharedBulletProofPart3, bulletProof.m_Part3);

            if (!SendTxParameters(std::move(msg)))
            {
                OnFailed(TxFailureReason::FailedToSendParameters, false);
            }
        }
    }

    void AtomicSwapTransaction::SendSharedTxInvitation(const BaseTxBuilder& builder, bool shouldSendLockImage /*= false*/)
    {
        SetTxParameter msg;
        msg.AddParameter(TxParameterID::SubTxIndex, builder.GetSubTxID())
            .AddParameter(TxParameterID::Amount, builder.GetAmount())
            .AddParameter(TxParameterID::Fee, builder.GetFee())
            .AddParameter(TxParameterID::MinHeight, builder.GetMinHeight())
            .AddParameter(TxParameterID::PeerPublicExcess, builder.GetPublicExcess())
            .AddParameter(TxParameterID::PeerPublicNonce, builder.GetPublicNonce());
    
        if (shouldSendLockImage)
        {
            msg.AddParameter(TxParameterID::PeerLockImage, builder.GetLockImage());
        }

        if (!SendTxParameters(std::move(msg)))
        {
            OnFailed(TxFailureReason::FailedToSendParameters, false);
        }
    }

    void AtomicSwapTransaction::ConfirmSharedTxInvitation(const BaseTxBuilder& builder)
    {
        SetTxParameter msg;
        msg.AddParameter(TxParameterID::SubTxIndex, builder.GetSubTxID())
            .AddParameter(TxParameterID::PeerPublicExcess, builder.GetPublicExcess())
            .AddParameter(TxParameterID::PeerSignature, builder.GetPartialSignature())
            .AddParameter(TxParameterID::PeerPublicNonce, builder.GetPublicNonce())
            .AddParameter(TxParameterID::PeerOffset, builder.GetOffset());

        if (!SendTxParameters(std::move(msg)))
        {
            OnFailed(TxFailureReason::FailedToSendParameters, false);
        }
    }

    void AtomicSwapTransaction::OnGetRawChangeAddress(const std::string& response)
    {
        json reply = json::parse(response);
        assert(reply["error"].empty());

        // TODO: validate error
        // const auto& error = reply["error"];

        const auto& result = reply["result"];

        // Don't need overwrite existing address
        if (std::string swapAddress; !GetParameter(TxParameterID::AtomicSwapAddress, swapAddress))
        {
            SetParameter(TxParameterID::AtomicSwapAddress, result.get<std::string>());
        }

        UpdateAsync();
    }

    void AtomicSwapTransaction::OnFundRawTransaction(const std::string& response)
    {
        json reply = json::parse(response);
        assert(reply["error"].empty());

        //const auto& error = reply["error"];
        const auto& result = reply["result"];
        auto hexTx = result["hex"].get<std::string>();
        int changePos = result["changepos"].get<int>();

        // float fee = result["fee"].get<float>();      // calculate fee!
        uint32_t valuePosition = changePos ? 0 : 1;
        SetParameter(TxParameterID::AtomicSwapExternalTxOutputIndex, valuePosition, false, SubTxIndex::LOCK_TX);

        assert(m_Gateway.get_bitcoin_rpc());
        m_Gateway.get_bitcoin_rpc()->signRawTransaction(hexTx, BIND_THIS_MEMFN(OnSignLockTransaction));
    }

    void AtomicSwapTransaction::OnSignLockTransaction(const std::string& response)
    {
        json reply = json::parse(response);
        assert(reply["error"].empty());

        const auto& result = reply["result"];

        assert(result["complete"].get<bool>());
        m_SwapLockRawTx = result["hex"].get<std::string>();

        SetState(SwapTxState::Constructed, SubTxIndex::LOCK_TX);
        UpdateAsync();
    }

    void AtomicSwapTransaction::OnCreateWithdrawTransaction(const std::string& response)
    {
        json reply = json::parse(response);
        assert(reply["error"].empty());
        if (!m_SwapWithdrawRawTx.is_initialized())
        {
            m_SwapWithdrawRawTx = reply["result"].get<std::string>();
            UpdateAsync();
        }
    }

    void AtomicSwapTransaction::OnDumpPrivateKey(SubTxID subTxID, const std::string& response)
    {
        json reply = json::parse(response);
        assert(reply["error"].empty());

        const auto& result = reply["result"];

        libbitcoin::data_chunk tx_data;
        libbitcoin::decode_base16(tx_data, *m_SwapWithdrawRawTx);
        libbitcoin::chain::transaction withdrawTX = libbitcoin::chain::transaction::factory_from_data(tx_data);

        libbitcoin::wallet::ec_private wallet_key(result.get<std::string>(), libbitcoin::wallet::ec_private::testnet_wif);
        libbitcoin::endorsement sig;

        uint32_t input_index = 0;
        auto contractScript = CreateAtomicSwapContract(*this, (SubTxIndex::REFUND_TX == subTxID));
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
            auto secret = GetMandatoryParameter<ECC::uintBig>(TxParameterID::PreImage, SubTxIndex::BEAM_REDEEM_TX);

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
        
        SetParameter(TxParameterID::AtomicSwapExternalTx, *m_SwapWithdrawRawTx, subTxID);
        SetState(SwapTxState::Constructed, subTxID);
        UpdateAsync();
    }

    void AtomicSwapTransaction::OnGetSwapLockTxConfirmations(const std::string& response)
    {
        json reply = json::parse(response);
        assert(reply["error"].empty());

        const auto& result = reply["result"];

        if (result.empty())
        {
            return;
        }

        // validate amount
        {
            Amount swapAmount = GetMandatoryParameter<Amount>(TxParameterID::AtomicSwapAmount);
            Amount outputAmount = static_cast<Amount>(std::round(result["value"].get<double>() * libbitcoin::satoshi_per_bitcoin));
            if (swapAmount > outputAmount)
            {
                LOG_DEBUG() << GetTxID() << "Unexpected amount, excpected: " << swapAmount << ", got: " << outputAmount;

                // TODO: implement error handling
                return;
            }
        }

        // validate contract script
        libbitcoin::data_chunk scriptData;
        libbitcoin::decode_base16(scriptData, result["scriptPubKey"]["hex"]);
        auto script = libbitcoin::chain::script::factory_from_data(scriptData, false);

        auto contractScript = CreateAtomicSwapContract(*this, false);

        assert(script == contractScript);

        if (script != contractScript)
        {
            // TODO: implement
            return;
        }

        // get confirmations
        m_SwapLockTxConfirmations = result["confirmations"];
    }    
} // namespace