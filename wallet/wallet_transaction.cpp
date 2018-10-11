// Copyright 2018 The Beam Team
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

#include "wallet_transaction.h"
#include "core/block_crypt.h"

namespace beam { namespace wallet
{
    using namespace ECC;
    using namespace std;

    TransactionFailedException::TransactionFailedException(bool notify, const char* message)
        : std::runtime_error(message)
        , m_Notify{notify}
    {

    }
    bool TransactionFailedException::ShouldNofify() const
    {
        return m_Notify;
    }

    BaseTransaction::BaseTransaction(INegotiatorGateway& gateway
                                   , beam::IKeyChain::Ptr keychain
                                   , const TxID& txID)
        : m_Gateway{ gateway }
        , m_Keychain{ keychain }
        , m_ID{ txID }
    {
        assert(keychain);
    }

    bool BaseTransaction::IsInitiator() const
    {
        if (!m_IsInitiator.is_initialized())
        {
            bool t = false;
            GetMandatoryParameter(TxParameterID::IsInitiator, t);
            m_IsInitiator = t;
        }
        return *m_IsInitiator;
    }

    bool BaseTransaction::GetTip(Block::SystemState::Full& state) const
    {
        return m_Gateway.get_tip(state);
    }

    const TxID& BaseTransaction::GetTxID() const
    {
        return m_ID;
    }

    void BaseTransaction::Update()
    {
        try
        {
            int reason = 0;
            if (GetParameter(TxParameterID::FailureReason, reason))
            {
                OnFailed();
                return;
            }

            UpdateImpl();
        }
        catch (const TransactionFailedException& ex)
        {
            LOG_ERROR() << GetTxID() << " exception: " << ex.what();
            OnFailed(ex.ShouldNofify());
        }
        catch (const exception& ex)
        {
            LOG_ERROR() << GetTxID() << " exception: " << ex.what();
        }
    }

    void BaseTransaction::Cancel()
    {
        TxStatus s = TxStatus::Failed;
        GetParameter(TxParameterID::Status, s);
        if (s == TxStatus::Pending)
        {
            m_Keychain->deleteTx(GetTxID());
        }
        else
        {
            UpdateTxDescription(TxStatus::Cancelled);
            RollbackTx();
            SetTxParameter msg;
            msg.AddParameter(TxParameterID::FailureReason, 0);
            SendTxParameters(move(msg));
        }
    }

    void BaseTransaction::RollbackTx()
    {
        LOG_INFO() << GetTxID() << " Transaction failed. Rollback...";
        m_Keychain->rollbackTx(GetTxID());
    }

    void BaseTransaction::ConfirmKernel(const TxKernel& kernel)
    {
        UpdateTxDescription(TxStatus::Registered);

        auto coins = m_Keychain->getCoinsCreatedByTx(GetTxID());

        for (auto& coin : coins)
        {
            coin.m_status = Coin::Unconfirmed;
        }
        m_Keychain->update(coins);

        m_Gateway.confirm_kernel(GetTxID(), kernel);
    }

    void BaseTransaction::CompleteTx()
    {
        LOG_INFO() << GetTxID() << " Transaction completed";
        UpdateTxDescription(TxStatus::Completed);
        m_Gateway.on_tx_completed(GetTxID());
    }

    void BaseTransaction::UpdateTxDescription(TxStatus s)
    {
        SetParameter(TxParameterID::Status, s);
        SetParameter(TxParameterID::ModifyTime, getTimestamp());
    }

    void BaseTransaction::OnFailed(bool notify)
    {
        UpdateTxDescription(TxStatus::Failed);
        RollbackTx();
        if (notify)
        {
            SetTxParameter msg;
            msg.AddParameter(TxParameterID::FailureReason, 0);
            SendTxParameters(move(msg));
        }
        m_Gateway.on_tx_completed(GetTxID());
    }

    vector<Input::Ptr> BaseTransaction::GetTxInputs(const TxID& txID) const
    {
        vector<Input::Ptr> inputs;
        m_Keychain->visit([this, &txID, &inputs](const Coin& c)->bool
        {
            if (c.m_spentTxId == txID && c.m_status == Coin::Locked)
            {
                Input::Ptr input = make_unique<Input>();

                Scalar::Native blindingFactor = m_Keychain->calcKey(c);
                input->m_Commitment = Commitment(blindingFactor, c.m_amount);

                inputs.push_back(move(input));
            }
            return true;
        });
        return inputs;
    }

    vector<Output::Ptr> BaseTransaction::GetTxOutputs(const TxID& txID) const
    {
        vector<Output::Ptr> outputs;
        m_Keychain->visit([this, &txID, &outputs](const Coin& c)->bool
        {
            if (c.m_createTxId == txID && c.m_status == Coin::Draft)
            {
                Output::Ptr output = make_unique<Output>();
                output->m_Coinbase = false;

                Scalar::Native blindingFactor = m_Keychain->calcKey(c);
                output->Create(blindingFactor, c.m_amount);

                outputs.push_back(move(output));
            }
            return true;
        });
        return outputs;
    }

    vector<Coin> BaseTransaction::GetUnconfirmedOutputs() const
    {
        vector<Coin> outputs;
        m_Keychain->visit([&](const Coin& coin)
        {
            if ((coin.m_createTxId == GetTxID() && coin.m_status == Coin::Unconfirmed)
                || (coin.m_spentTxId == GetTxID() && coin.m_status == Coin::Locked))
            {
                outputs.emplace_back(coin);
            }

            return true;
        });
        return outputs;
    }

    void BaseTransaction::CreateOutput(Amount amount, Height currentHeight)
    {
        Coin newUtxo{ amount, Coin::Draft, currentHeight };
        newUtxo.m_createTxId = GetTxID();
        m_Keychain->store(newUtxo);

        Scalar::Native blindingFactor = m_Keychain->calcKey(newUtxo);
        auto[privateExcess, newOffset] = splitKey(blindingFactor, newUtxo.m_id);

        Scalar::Native blindingExcess, offset;
        GetParameter(TxParameterID::BlindingExcess, blindingExcess);
        GetParameter(TxParameterID::Offset, offset);

        blindingFactor = -privateExcess;
        blindingExcess += blindingFactor;
        offset += newOffset;

        SetParameter(TxParameterID::BlindingExcess, blindingExcess);
        SetParameter(TxParameterID::Offset, offset);
    }

    void BaseTransaction::PrepareSenderUTXOs(Amount amount, Height currentHeight)
    {
        auto coins = m_Keychain->selectCoins(amount);
        if (coins.empty())
        {
            LOG_ERROR() << "You only have " << PrintableAmount(getAvailable(m_Keychain));
            throw TransactionFailedException(!IsInitiator());
        }
        Scalar::Native blindingExcess, offset;
        GetParameter(TxParameterID::BlindingExcess, blindingExcess);
        GetParameter(TxParameterID::Offset, offset);
        for (auto& coin : coins)
        {
            blindingExcess += m_Keychain->calcKey(coin);
            coin.m_spentTxId = GetTxID();
        }
        m_Keychain->update(coins);

        SetParameter(TxParameterID::BlindingExcess, blindingExcess);
        SetParameter(TxParameterID::Offset, offset);

        // calculate change amount and create corresponding output if needed
        Amount change = 0;
        for (const auto &coin : coins)
        {
            change += coin.m_amount;
        }
        change -= amount;
        SetParameter(TxParameterID::Change, change);

        if (change > 0)
        {
            // create output utxo for change
            CreateOutput(change, currentHeight);
        }
    }

    bool BaseTransaction::SendTxParameters(SetTxParameter&& msg) const
    {
        msg.m_txId = GetTxID();
        msg.m_Type = GetType();
        
        WalletID peerID = {0};
        if (GetParameter(TxParameterID::MyID, msg.m_from) 
            && GetParameter(TxParameterID::PeerID, peerID))
        {
            m_Gateway.send_tx_params(peerID, move(msg));
            return true;
        }
        return false;
    }

    SimpleTransaction::SimpleTransaction(INegotiatorGateway& gateway
        , beam::IKeyChain::Ptr keychain
        , const TxID& txID)
        : BaseTransaction{ gateway, keychain, txID }
    {

    }

    TxType SimpleTransaction::GetType() const
    {
        return TxType::Simple;
    }

    void SimpleTransaction::UpdateImpl()
    {
        bool sender = false;
        Amount amount = 0, fee = 0;
        GetMandatoryParameter(TxParameterID::IsSender, sender);
        GetMandatoryParameter(TxParameterID::Amount, amount);
        GetMandatoryParameter(TxParameterID::Fee, fee);
        
        WalletID peerID = { 0 };
        GetMandatoryParameter(TxParameterID::PeerID, peerID);
        auto address = m_Keychain->getAddress(peerID);
        bool isSelfTx = address.is_initialized() && address->m_own;

        SignatureBuilder sb{*this};
        Height minHeight = 0;
        Scalar::Native offset;
        Scalar::Native blindingExcess;
        if (!GetParameter(TxParameterID::BlindingExcess, blindingExcess)
            || !GetParameter(TxParameterID::Offset, offset)
            || !GetParameter(TxParameterID::MinHeight, minHeight))
        {
            LOG_INFO() << GetTxID() << (sender ? " Sending " : " Receiving ") << PrintableAmount(amount) << " (fee: " << PrintableAmount(fee) << ")";
            minHeight = m_Keychain->getCurrentHeight();
            SetParameter(TxParameterID::MinHeight, minHeight);

            if (sender)
            {
                // select and lock input utxos
                Amount amountWithFee = amount + fee;
                PrepareSenderUTXOs(amountWithFee, minHeight);
            }
            if (isSelfTx || !sender)
            {
                // create receiver utxo
                CreateOutput(amount, minHeight);

                LOG_INFO() << GetTxID() << " Invitation accepted";
            }

            UpdateTxDescription(TxStatus::InProgress);
        }

        GetMandatoryParameter(TxParameterID::Offset, offset);

        sb.CreateKernel(fee, minHeight);
        sb.ApplyBlindingExcess();

        if (!isSelfTx && (!sb.ApplyPublicPeerNonce() || !sb.ApplyPublicPeerExcess()))
        {
            assert(IsInitiator());

            SetTxParameter msg;
            msg.AddParameter(TxParameterID::Amount, amount)
               .AddParameter(TxParameterID::Fee, fee)
               .AddParameter(TxParameterID::MinHeight, minHeight)
               .AddParameter(TxParameterID::IsSender, !sender)
               .AddParameter(TxParameterID::PeerInputs, GetTxInputs(GetTxID()))
               .AddParameter(TxParameterID::PeerOutputs, GetTxOutputs(GetTxID()))
               .AddParameter(TxParameterID::PeerPublicExcess, sb.m_PublicExcess)
               .AddParameter(TxParameterID::PeerPublicNonce, sb.m_PublicNonce)
               .AddParameter(TxParameterID::PeerOffset, offset);

            if (!SendTxParameters(move(msg)))
            {
                OnFailed(false);
            }
            return;
        }

        sb.SignPartial();

        if (!isSelfTx && !sb.ApplyPeerSignature())
        {
            // invited participant
            assert(!IsInitiator());
            // Confirm invitation
            SetTxParameter msg;
            msg.AddParameter(TxParameterID::PeerPublicExcess, sb.m_PublicExcess)
               .AddParameter(TxParameterID::PeerSignature, sb.m_PartialSignature)
               .AddParameter(TxParameterID::PeerPublicNonce, sb.m_PublicNonce);

            SendTxParameters(move(msg));
            return;
        }

        // verify peer's signature
        if (!sb.IsValidPeerSignature())
        {
            OnFailed(true);
            return;
        }

        sb.FinalizeSignature();

        bool isRegistered = false;
        if (!GetParameter(TxParameterID::TransactionRegistered, isRegistered))
        {
            LOG_INFO() << GetTxID() << " Transaction registered";
            vector<Input::Ptr> inputs;
            vector<Output::Ptr> outputs;
            if (!isSelfTx && (!GetParameter(TxParameterID::PeerInputs, inputs)
                || !GetParameter(TxParameterID::PeerOutputs, outputs)))
            {
                // initiator 
                assert(IsInitiator());
                Scalar s;
                sb.m_PartialSignature.Export(s);

                // Confirm transaction
                SetTxParameter msg;
                msg.AddParameter(TxParameterID::PeerSignature, s);
                
                SendTxParameters(move(msg));
            }
            else
            {
                // Construct and verify transaction
                Scalar::Native peerOffset;
                GetParameter(TxParameterID::PeerOffset, peerOffset);

                auto transaction = make_shared<Transaction>();
                transaction->m_vKernelsOutput.push_back(move(sb.m_Kernel));
                transaction->m_Offset = peerOffset + offset;
                transaction->m_vInputs = move(inputs);
                transaction->m_vOutputs = move(outputs);

                {
                    auto inputs2 = GetTxInputs(GetTxID());
                    move(inputs2.begin(), inputs2.end(), back_inserter(transaction->m_vInputs));

                    auto outputs2 = GetTxOutputs(GetTxID());
                    move(outputs2.begin(), outputs2.end(), back_inserter(transaction->m_vOutputs));
                }

                transaction->Sort();

                // Verify final transaction
                TxBase::Context ctx;
                if (!transaction->IsValid(ctx))
                {
                    OnFailed(true);
                    return;
                }
                m_Gateway.register_tx(GetTxID(), transaction);
            }
            return;
        }

        if (!isRegistered)
        {
            OnFailed(true);
            return;
        }

        Merkle::Proof kernelProof;
        if (!GetParameter(TxParameterID::KernelProof, kernelProof))
        {
            if (!IsInitiator())
            {
                // notify peer that transaction has been registered
                SetTxParameter msg;
                msg.AddParameter(TxParameterID::TransactionRegistered, true);
                SendTxParameters(move(msg));
            }
            ConfirmKernel(*sb.m_Kernel);
            return;
        }

        Block::SystemState::Full state;
        if (!GetTip(state) || !state.IsValidProofKernel(*sb.m_Kernel, kernelProof))
        {
            if (!m_Gateway.isTestMode())
            {
                return;
            }
        }

        vector<Coin> unconfirmed = GetUnconfirmedOutputs();
        if (!unconfirmed.empty())
        {
            m_Gateway.confirm_outputs(unconfirmed);
            return;
        }

        CompleteTx();
    }

    SignatureBuilder::SignatureBuilder(BaseTransaction& tx) : m_Tx{ tx }
    {

    }

    void SignatureBuilder::CreateKernel(Amount fee, Height minHeight)
    {
        assert(!m_Kernel);
        m_Kernel = make_unique<TxKernel>();
        m_Kernel->m_Fee = fee;
        m_Kernel->m_Height.m_Min = minHeight;
        m_Kernel->m_Height.m_Max = MaxHeight;
        m_Kernel->m_Excess = Zero;
    }

    void SignatureBuilder::SetBlindingExcess(const Scalar::Native& blindingExcess)
    {
        assert(m_Kernel);
        Hash::Value hv;
        // Excess should be zero
        m_Kernel->get_Hash(hv);

        m_MultiSig.GenerateNonce(hv, blindingExcess);

        m_PublicNonce = Context::get().G * m_MultiSig.m_Nonce;
        m_PublicExcess = Context::get().G * blindingExcess;
    }

    bool SignatureBuilder::ApplyBlindingExcess()
    {
        assert(m_Kernel);
        m_Tx.GetMandatoryParameter(TxParameterID::BlindingExcess, m_BlindingExcess);
        Hash::Value hv;
        m_Kernel->get_Hash(hv);

        m_MultiSig.GenerateNonce(hv, m_BlindingExcess);

        m_PublicNonce = Context::get().G * m_MultiSig.m_Nonce;
        m_PublicExcess = Context::get().G * m_BlindingExcess;

        return true;
    }

    bool SignatureBuilder::ApplyPublicPeerNonce()
    {
        if (!m_Tx.GetParameter(TxParameterID::PeerPublicNonce, m_PublicPeerNonce))
        {
            return false;
        }
        m_MultiSig.m_NoncePub = m_PublicNonce + m_PublicPeerNonce;
        return true;
    }

    bool SignatureBuilder::ApplyPublicPeerExcess()
    {
        if (!m_Tx.GetParameter(TxParameterID::PeerPublicExcess, m_PublicPeerExcess))
        {
            return false;
        }
        return true;
    }

    void SignatureBuilder::SignPartial()
    {
        Point::Native totalPublicExcess = m_PublicExcess;
        totalPublicExcess += m_PublicPeerExcess;
        m_Kernel->m_Excess = totalPublicExcess;
        m_Kernel->get_Hash(m_Message);
        m_MultiSig.SignPartial(m_PartialSignature, m_Message, m_BlindingExcess);
    }

    bool SignatureBuilder::ApplyPeerSignature()
    {
        if (!m_Tx.GetParameter(TxParameterID::PeerSignature, m_PeerSignature))
        {
            return false;
        }
        return true;
    }

    bool SignatureBuilder::IsValidPeerSignature() const
    {
        Signature peerSig;
        peerSig.m_NoncePub = m_MultiSig.m_NoncePub;
        peerSig.m_k = m_PeerSignature;
        return peerSig.IsValidPartial(m_Message, m_PublicPeerNonce, m_PublicPeerExcess);
    }

    void SignatureBuilder::FinalizeSignature()
    {
        m_Kernel->m_Signature.m_k = m_PartialSignature + m_PeerSignature;
        m_Kernel->m_Signature.m_NoncePub = m_MultiSig.m_NoncePub;
    }
}}
