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
#include <boost/uuid/uuid_generators.hpp>

namespace beam { namespace wallet
{
    using namespace ECC;
    using namespace std;


    TxID GenerateTxID()
    {
        boost::uuids::uuid id = boost::uuids::random_generator()();
        TxID txID{};
        copy(id.begin(), id.end(), txID.begin());
        return txID;
    }

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
            m_IsInitiator = GetMandatoryParameter<bool>(TxParameterID::IsInitiator);
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

            WalletID myID = GetMandatoryParameter<WalletID>(TxParameterID::MyID);
            auto address = m_Keychain->getAddress(myID);
            if (address.is_initialized() && address->isExpired())
            {
                OnFailed();
                return;
            }

            UpdateImpl();
        }
        catch (const TransactionFailedException& ex)
        {
            OnFailed(ex.what(), ex.ShouldNofify());
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
            m_Gateway.on_tx_completed(GetTxID());
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

    void BaseTransaction::OnFailed(const string& message, bool notify)
    {
        LOG_ERROR() << GetTxID() << " Failed. " << message;
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

    IKeyChain::Ptr BaseTransaction::GetKeychain()
    {
        return m_Keychain;
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
        bool sender = GetMandatoryParameter<bool>(TxParameterID::IsSender);
        Amount amount = GetMandatoryParameter<Amount>(TxParameterID::Amount);
        Amount fee = GetMandatoryParameter<Amount>(TxParameterID::Fee);

        WalletID peerID = GetMandatoryParameter<WalletID>(TxParameterID::PeerID);
        auto address = m_Keychain->getAddress(peerID);
        bool isSelfTx = address.is_initialized() && address->m_own;

        TxBuilder builder{ *this, amount, fee };
        if (!builder.GetInitialTxParams())
        {
            LOG_INFO() << GetTxID() << (sender ? " Sending " : " Receiving ") << PrintableAmount(amount) << " (fee: " << PrintableAmount(fee) << ")";
            builder.SetMinHeight(m_Keychain->getCurrentHeight());

            if (sender)
            {
                builder.SelectInputs();
                builder.AddChangeOutput();
            }

            if (isSelfTx || !sender)
            {
                // create receiver utxo
                builder.AddOutput(amount);

                LOG_INFO() << GetTxID() << " Invitation accepted";
            }
            UpdateTxDescription(TxStatus::InProgress);
        }

        builder.GenerateNonce();
        
        if (!isSelfTx && !builder.GetPeerPublicExcessAndNonce())
        {
            assert(IsInitiator());

            SetTxParameter msg;
            msg.AddParameter(TxParameterID::Amount, builder.m_Amount)
                .AddParameter(TxParameterID::Fee, builder.m_Fee)
                .AddParameter(TxParameterID::MinHeight, builder.m_MinHeight)
                .AddParameter(TxParameterID::IsSender, !sender)
                .AddParameter(TxParameterID::PeerInputs, builder.m_Inputs)
                .AddParameter(TxParameterID::PeerOutputs, builder.m_Outputs)
                .AddParameter(TxParameterID::PeerPublicExcess, builder.GetPublicExcess())
                .AddParameter(TxParameterID::PeerPublicNonce, builder.GetPublicNonce())
                .AddParameter(TxParameterID::PeerOffset, builder.m_Offset);

            if (!SendTxParameters(move(msg)))
            {
                OnFailed("SendTxParameters failed", false);
            }
            return;
        }

        builder.SignPartial();

        if (!isSelfTx && !builder.GetPeerSignature())
        {
            // invited participant
            assert(!IsInitiator());
            // Confirm invitation
            SetTxParameter msg;
            msg.AddParameter(TxParameterID::PeerPublicExcess, builder.GetPublicExcess())
                .AddParameter(TxParameterID::PeerSignature, builder.m_PartialSignature)
                .AddParameter(TxParameterID::PeerPublicNonce, builder.GetPublicNonce());

            SendTxParameters(move(msg));
            return;
        }

        if (!builder.IsPeerSignatureValid())
        {
            OnFailed("Peer signature in not valid ", true);
            return;
        }

        bool isRegistered = false;
        if (!GetParameter(TxParameterID::TransactionRegistered, isRegistered))
        {
            if (!isSelfTx && !builder.GetPeerInputsAndOutputs())
            {
                assert(IsInitiator());
                Scalar s;
                builder.m_PartialSignature.Export(s);

                // Confirm transaction
                SetTxParameter msg;
                msg.AddParameter(TxParameterID::PeerSignature, s);
                SendTxParameters(move(msg));
            }
            else
            {
                // Construct and verify transaction
                auto transaction = builder.CreateTransaction();

                // Verify final transaction
                TxBase::Context ctx;
                if (!transaction->IsValid(ctx))
                {
                    OnFailed("tx is not valid", true);
                    return;
                }
                m_Gateway.register_tx(GetTxID(), transaction);
            }
            return;
        }

        if (!isRegistered)
        {
            OnFailed("not registered", true);
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
            ConfirmKernel(*builder.m_Kernel);
            return;
        }

        Block::SystemState::Full state;
        if (!GetTip(state) || !state.IsValidProofKernel(*builder.m_Kernel, kernelProof))
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

    TxBuilder::TxBuilder(BaseTransaction& tx, Amount amount, Amount fee)
        : m_Tx{ tx }
        , m_Amount{ amount }
        , m_Fee{ fee }
        , m_Change{0}
        , m_MinHeight{0}
    {
    }

    void TxBuilder::SelectInputs()
    {
        Amount amountWithFee = m_Amount + m_Fee;
        auto coins = m_Tx.GetKeychain()->selectCoins(amountWithFee);
        if (coins.empty())
        {
            LOG_ERROR() << "You only have " << PrintableAmount(getAvailable(m_Tx.GetKeychain()));
            throw TransactionFailedException(!m_Tx.IsInitiator());
        }

        m_Inputs.reserve(m_Inputs.size() + coins.size());
        Amount total = 0;
        for (auto& coin : coins)
        {
            coin.m_spentTxId = m_Tx.GetTxID();

            Scalar::Native blindingFactor = m_Tx.GetKeychain()->calcKey(coin);
            auto& input = m_Inputs.emplace_back(make_unique<Input>());
            input->m_Commitment = Commitment(blindingFactor, coin.m_amount);
            m_BlindingExcess += blindingFactor;
            total += coin.m_amount;
        }

        m_Change += total - amountWithFee;

        m_Tx.SetParameter(TxParameterID::BlindingExcess, m_BlindingExcess);
        m_Tx.SetParameter(TxParameterID::Change, m_Change);
        m_Tx.SetParameter(TxParameterID::Inputs, m_Inputs);
        m_Tx.SetParameter(TxParameterID::Offset, m_Offset);

        m_Tx.GetKeychain()->update(coins);
    }

    void TxBuilder::AddChangeOutput()
    {
        if (m_Change == 0)
        {
            return;
        }

        AddOutput(m_Change);
    }

    void TxBuilder::AddOutput(Amount amount)
    {
        m_Outputs.push_back(CreateOutput(amount, m_MinHeight));
        m_Tx.SetParameter(TxParameterID::Outputs, m_Outputs);
    }

    Output::Ptr TxBuilder::CreateOutput(Amount amount, bool shared, Height incubation)
    {
        Coin newUtxo{ amount, Coin::Draft, m_MinHeight };
        newUtxo.m_createTxId = m_Tx.GetTxID();
        m_Tx.GetKeychain()->store(newUtxo);

        Scalar::Native blindingFactor;
        Output::Ptr output = make_unique<Output>();
        output->Create(blindingFactor, *m_Tx.GetKeychain()->get_Kdf(), newUtxo.get_Kidv());

        auto[privateExcess, newOffset] = splitKey(blindingFactor, newUtxo.m_id);
        blindingFactor = -privateExcess;
        m_BlindingExcess += blindingFactor;
        m_Offset += newOffset;

        m_Tx.SetParameter(TxParameterID::BlindingExcess, m_BlindingExcess);
        m_Tx.SetParameter(TxParameterID::Offset, m_Offset);

        return output;
    }

    void TxBuilder::GenerateNonce()
    {
        // create kernel
        assert(!m_Kernel);
        m_Kernel = make_unique<TxKernel>();
        m_Kernel->m_Fee = m_Fee;
        m_Kernel->m_Height.m_Min = m_MinHeight;
        m_Kernel->m_Height.m_Max = MaxHeight;
        m_Kernel->m_Excess = Zero;

		if (!m_Tx.GetParameter(TxParameterID::MyNonce, m_MultiSig.m_Nonce))
		{
			Coin c;
			c.m_id = m_Tx.GetKeychain()->get_AutoIncrID();
			c.m_key_type = Key::Type::Nonce;

			m_MultiSig.m_Nonce = m_Tx.GetKeychain()->calcKey(c);

			m_Tx.SetParameter(TxParameterID::MyNonce, m_MultiSig.m_Nonce);
		}
    }

    Point::Native TxBuilder::GetPublicExcess() const
    {
        return Context::get().G * m_BlindingExcess;
    }

    Point::Native TxBuilder::GetPublicNonce() const
    {
        return Context::get().G * m_MultiSig.m_Nonce;
    }

    bool TxBuilder::GetPeerPublicExcessAndNonce()
    {
        return m_Tx.GetParameter(TxParameterID::PeerPublicExcess, m_PeerPublicExcess)
            && m_Tx.GetParameter(TxParameterID::PeerPublicNonce, m_PeerPublicNonce);
    }

    bool TxBuilder::GetPeerSignature()
    {
        return m_Tx.GetParameter(TxParameterID::PeerSignature, m_PeerSignature);
    }

    bool TxBuilder::GetInitialTxParams()
    {
        m_Tx.GetParameter(TxParameterID::Inputs, m_Inputs);
        m_Tx.GetParameter(TxParameterID::Outputs, m_Outputs);
        return m_Tx.GetParameter(TxParameterID::BlindingExcess, m_BlindingExcess)
            && m_Tx.GetParameter(TxParameterID::Offset, m_Offset)
            && m_Tx.GetParameter(TxParameterID::MinHeight, m_MinHeight);
    }

    bool TxBuilder::GetPeerInputsAndOutputs()
    {
        vector<Input::Ptr> inputs;
        vector<Output::Ptr> outputs;
        Scalar::Native peerOffset;
        if (!m_Tx.GetParameter(TxParameterID::PeerInputs, inputs)
            || !m_Tx.GetParameter(TxParameterID::PeerOutputs, outputs)
            || !m_Tx.GetParameter(TxParameterID::PeerOffset, peerOffset))
        {
            return false;
        }
        move(inputs.begin(), inputs.end(), back_inserter(m_Inputs));
        move(outputs.begin(), outputs.end(), back_inserter(m_Outputs));
        m_Offset += peerOffset;

        m_Tx.SetParameter(TxParameterID::Inputs, m_Inputs);
        m_Tx.SetParameter(TxParameterID::Outputs, m_Outputs);
        m_Tx.SetParameter(TxParameterID::Offset, m_Offset);
        
        return true;
    }

    void TxBuilder::SetMinHeight(Height minHeight)
    {
        m_MinHeight = minHeight;
        m_Tx.SetParameter(TxParameterID::MinHeight, m_MinHeight);
    }

    void TxBuilder::SignPartial()
    {
        // create signature
        Point::Native totalPublicExcess = GetPublicExcess();
        totalPublicExcess += m_PeerPublicExcess;
        m_Kernel->m_Excess = totalPublicExcess;

        m_Kernel->get_Hash(m_Message);
        m_MultiSig.m_NoncePub = GetPublicNonce() + m_PeerPublicNonce;
        LOG_DEBUG() << "[SignPartial]\nMessage:\t" << m_Message << "\nNoncePub:\t" << m_MultiSig.m_NoncePub;
        
        m_MultiSig.SignPartial(m_PartialSignature, m_Message, m_BlindingExcess);
    }

    Transaction::Ptr TxBuilder::CreateTransaction()
    {
        // final signature
        m_Kernel->m_Signature.m_NoncePub = GetPublicNonce() + m_PeerPublicNonce;
        m_Kernel->m_Signature.m_k = m_PartialSignature + m_PeerSignature;

        // create transaction
        auto transaction = make_shared<Transaction>();
        transaction->m_vKernelsOutput.push_back(move(m_Kernel));
        transaction->m_Offset = m_Offset;
        transaction->m_vInputs = move(m_Inputs);
        transaction->m_vOutputs = move(m_Outputs);
        transaction->Sort();

        // Verify final transaction
        TxBase::Context ctx;
        assert(transaction->IsValid(ctx));
        return transaction;
    }

    bool TxBuilder::IsPeerSignatureValid() const
    {
        Signature peerSig;
        peerSig.m_NoncePub = m_MultiSig.m_NoncePub;
        peerSig.m_k = m_PeerSignature;
        return peerSig.IsValidPartial(m_Message, m_PeerPublicNonce, m_PeerPublicExcess);
    }
}}
