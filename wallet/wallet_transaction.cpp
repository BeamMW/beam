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
            AddParameter(msg, TxParameterID::FailureReason, 0);
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

    TxKernel::Ptr BaseTransaction::CreateKernel(Amount fee, Height minHeight) const
    {
        auto kernel = make_unique<TxKernel>();
        kernel->m_Fee = fee;
        kernel->m_Height.m_Min = minHeight;
        kernel->m_Height.m_Max = MaxHeight;
        kernel->m_Excess = Zero;
        return kernel;
    }

    Signature::MultiSig BaseTransaction::CreateMultiSig(const TxKernel& kernel, const Scalar::Native& blindingExcess) const
    {
        Signature::MultiSig msig;
        Hash::Value hv;
        kernel.get_Hash(hv);

        msig.GenerateNonce(hv, blindingExcess);
        return msig;
    }

    void BaseTransaction::OnFailed(bool notify)
    {
        UpdateTxDescription(TxStatus::Failed);
        RollbackTx();
        if (notify)
        {
            SetTxParameter msg;
            AddParameter(msg, TxParameterID::FailureReason, 0);
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

    struct UtxoProcessor
    {

    };

    struct SignatureBuilder
    {
        bool Update(BaseTransaction& tx)
        {
            Scalar::Native blindingExcess, offset;
            if (!tx.GetParameter(TxParameterID::BlindingExcess, blindingExcess)
                || !tx.GetParameter(TxParameterID::Offset, offset))
            {
                return false;
            }

            Height minHeight = 0;
            tx.GetMandatoryParameter(TxParameterID::MinHeight, minHeight);
            Amount fee = 0;
            tx.GetMandatoryParameter(TxParameterID::Fee, fee);

            auto kernel = make_unique<TxKernel>();
            kernel->m_Fee = fee;
            kernel->m_Height.m_Min = minHeight;
            kernel->m_Height.m_Max = MaxHeight;
            kernel->m_Excess = Zero;

            Signature::MultiSig msig;
            Hash::Value hv;
            kernel->get_Hash(hv);

            msig.GenerateNonce(hv, blindingExcess);

            Point::Native publicNonce = Context::get().G * msig.m_Nonce;
            Point::Native publicExcess = Context::get().G * blindingExcess;

            Point::Native publicPeerNonce, publicPeerExcess;
            if (!tx.GetParameter(TxParameterID::PeerPublicNonce, publicPeerNonce)
                || !tx.GetParameter(TxParameterID::PeerPublicExcess, publicPeerExcess))
            {
                return false;
            }

            msig.m_NoncePub = publicNonce + publicPeerNonce;

            Point::Native totalPublicExcess = publicExcess;
            totalPublicExcess += publicPeerExcess;
            kernel->m_Excess = totalPublicExcess;

            // create my part of signature
            Hash::Value message;
            kernel->get_Hash(message);
            Scalar::Native partialSignature;
            msig.SignPartial(partialSignature, message, blindingExcess);

            Scalar::Native peerSignature;
            if (!tx.GetParameter(TxParameterID::PeerSignature, peerSignature))
            {
                return false;
            }

            // verify peer's signature
            Signature peerSig;
            peerSig.m_NoncePub = msig.m_NoncePub;
            peerSig.m_k = peerSignature;
            if (!peerSig.IsValidPartial(message, publicPeerNonce, publicPeerExcess))
            {
                return false;
            }

            // final signature
            kernel->m_Signature.m_k = partialSignature + peerSignature;
            kernel->m_Signature.m_NoncePub = msig.m_NoncePub;

            return true;
        }
    };

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

        Scalar::Native offset;
        Scalar::Native blindingExcess;
        if (!GetParameter(TxParameterID::BlindingExcess, blindingExcess)
            || !GetParameter(TxParameterID::Offset, offset))
        {
            LOG_INFO() << GetTxID() << (sender ? " Sending " : " Receiving ") << PrintableAmount(amount) << " (fee: " << PrintableAmount(fee) << ")";
            Height currentHeight = m_Keychain->getCurrentHeight();
            SetParameter(TxParameterID::MinHeight, currentHeight);

            if (sender)
            {
                // select and lock input utxos
                Amount amountWithFee = amount + fee;
                PrepareSenderUTXOs(amountWithFee, currentHeight);
            }
            if (isSelfTx || !sender)
            {
                // create receiver utxo
                CreateOutput(amount, currentHeight);

                LOG_INFO() << GetTxID() << " Invitation accepted";
            }

            UpdateTxDescription(TxStatus::InProgress);
        }

        Height minHeight = 0;
        GetMandatoryParameter(TxParameterID::MinHeight, minHeight);
        GetMandatoryParameter(TxParameterID::BlindingExcess, blindingExcess);
        GetMandatoryParameter(TxParameterID::Offset, offset);

        auto kernel = CreateKernel(fee, minHeight);
        auto msig = CreateMultiSig(*kernel, blindingExcess);

        Point::Native publicNonce = Context::get().G * msig.m_Nonce;
        Point::Native publicExcess = Context::get().G * blindingExcess;

        Point::Native publicPeerNonce, publicPeerExcess;

        if (!isSelfTx && (!GetParameter(TxParameterID::PeerPublicNonce, publicPeerNonce)
            || !GetParameter(TxParameterID::PeerPublicExcess, publicPeerExcess)))
        {
            assert(IsInitiator());

            SetTxParameter msg;

            AddParameter(msg, TxParameterID::Amount, amount);
            AddParameter(msg, TxParameterID::Fee, fee);
            AddParameter(msg, TxParameterID::MinHeight, minHeight);
            AddParameter(msg, TxParameterID::IsSender, !sender);
            AddParameter(msg, TxParameterID::PeerInputs, GetTxInputs(GetTxID()));
            AddParameter(msg, TxParameterID::PeerOutputs, GetTxOutputs(GetTxID()));
            AddParameter(msg, TxParameterID::PeerPublicExcess, publicExcess);
            AddParameter(msg, TxParameterID::PeerPublicNonce, publicNonce);
            AddParameter(msg, TxParameterID::PeerOffset, offset);

            if (!SendTxParameters(move(msg)))
            {
                OnFailed(false);
            }
            return;
        }

        msig.m_NoncePub = publicNonce + publicPeerNonce;

        Point::Native totalPublicExcess = publicExcess;
        totalPublicExcess += publicPeerExcess;
        kernel->m_Excess = totalPublicExcess;

        // create my part of signature
        Hash::Value message;
        kernel->get_Hash(message);
        Scalar::Native partialSignature;
        msig.SignPartial(partialSignature, message, blindingExcess);

        Scalar::Native peerSignature;
        if (!isSelfTx && !GetParameter(TxParameterID::PeerSignature, peerSignature))
        {
            // invited participant
            assert(!IsInitiator());
            // Confirm invitation
            SetTxParameter msg;
            AddParameter(msg, TxParameterID::PeerPublicExcess, publicExcess);
            AddParameter(msg, TxParameterID::PeerSignature, partialSignature);
            AddParameter(msg, TxParameterID::PeerPublicNonce, publicNonce);

            SendTxParameters(move(msg));
            return;
        }

        // verify peer's signature
        Signature peerSig;
        peerSig.m_NoncePub = msig.m_NoncePub;
        peerSig.m_k = peerSignature;
        if (!peerSig.IsValidPartial(message, publicPeerNonce, publicPeerExcess))
        {
            OnFailed(true);
            return;
        }

        // final signature
        kernel->m_Signature.m_k = partialSignature + peerSignature;
        kernel->m_Signature.m_NoncePub = msig.m_NoncePub;

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
                partialSignature.Export(s);

                // Confirm transaction
                SetTxParameter msg;
                AddParameter(msg, TxParameterID::PeerSignature, s);
                
                SendTxParameters(move(msg));
            }
            else
            {
                // Construct and verify transaction
                Scalar::Native peerOffset;
                GetParameter(TxParameterID::PeerOffset, peerOffset);

                auto transaction = make_shared<Transaction>();
                transaction->m_vKernelsOutput.push_back(move(kernel));
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
                AddParameter(msg, TxParameterID::TransactionRegistered, true);
                SendTxParameters(move(msg));
            }
            ConfirmKernel(*kernel);
            return;
        }

        Block::SystemState::Full state;
        if (!GetTip(state) || !state.IsValidProofKernel(*kernel, kernelProof))
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
}}
