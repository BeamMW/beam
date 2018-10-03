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

    BaseTransaction::BaseTransaction(INegotiatorGateway& gateway
                                   , beam::IKeyChain::Ptr keychain
                                   , const TxDescription& txDesc)
        : m_gateway{ gateway }
        , m_keychain{ keychain }
        , m_txDesc{ txDesc }
    {
        assert(keychain);
    }

    bool BaseTransaction::getParameter(TxParameterID paramID, ECC::Point::Native& value)
    {
        ECC::Point pt;
        if (getParameter(paramID, pt))
        {
            return value.Import(pt);
        }
        return false;
    }

    bool BaseTransaction::getParameter(TxParameterID paramID, ECC::Scalar::Native& value)
    {
        ECC::Scalar s;
        if (getParameter(paramID, s))
        {
             value.Import(s);
             return true;
        }
        return false;
    }

    void BaseTransaction::setParameter(TxParameterID paramID, const ECC::Point::Native& value)
    {
        ECC::Point pt;
        if (value.Export(pt))
        {
            setParameter(paramID, pt);
        }
    }

    void BaseTransaction::setParameter(TxParameterID paramID, const ECC::Scalar::Native& value)
    {
        ECC::Scalar s;
        value.Export(s);
        setParameter(paramID, s);
    }

    bool BaseTransaction::getTip(Block::SystemState::Full& state) const
    {
        return m_gateway.get_tip(state);
    }

    const TxID& BaseTransaction::getTxID() const
    {
        return m_txDesc.m_txId;
    }

    void BaseTransaction::cancel()
    {
        if (m_txDesc.m_status == TxDescription::Pending)
        {
            m_keychain->deleteTx(m_txDesc.m_txId);
        }
        else
        {
            updateTxDescription(TxDescription::Cancelled);
            rollbackTx();
            m_gateway.send_tx_failed(m_txDesc);
        }
    }

    void BaseTransaction::rollbackTx()
    {
        LOG_INFO() << m_txDesc.m_txId << " Transaction failed. Rollback...";
        m_keychain->rollbackTx(m_txDesc.m_txId);
    }

    void BaseTransaction::confirmKernel(const TxKernel& kernel)
    {
        updateTxDescription(TxDescription::Registered);

        auto coins = m_keychain->getCoinsCreatedByTx(m_txDesc.m_txId);

        for (auto& coin : coins)
        {
            coin.m_status = Coin::Unconfirmed;
        }
        m_keychain->update(coins);

        m_gateway.confirm_kernel(m_txDesc, kernel);
    }

    void BaseTransaction::completeTx()
    {
        LOG_INFO() << m_txDesc.m_txId << " Transaction completed";
        updateTxDescription(TxDescription::Completed);
        m_gateway.on_tx_completed(m_txDesc);
    }

    void BaseTransaction::updateTxDescription(TxDescription::Status s)
    {
        m_txDesc.m_status = s;
        m_txDesc.m_modifyTime = getTimestamp();
        m_keychain->saveTx(m_txDesc);
    }

    TxKernel::Ptr BaseTransaction::createKernel(Amount fee, Height minHeight) const
    {
        auto kernel = make_unique<TxKernel>();
        kernel->m_Fee = fee;
        kernel->m_Height.m_Min = minHeight;
        kernel->m_Height.m_Max = MaxHeight;
        kernel->m_Excess = Zero;
        return kernel;
    }

    Signature::MultiSig BaseTransaction::createMultiSig(const TxKernel& kernel, const Scalar::Native& blindingExcess) const
    {
        Signature::MultiSig msig;
        Hash::Value hv;
        kernel.get_Hash(hv);

        msig.GenerateNonce(hv, blindingExcess);
        return msig;
    }

    void BaseTransaction::onFailed(bool notify)
    {
        updateTxDescription(TxDescription::Failed);
        rollbackTx();
        if (notify)
        {
            m_gateway.send_tx_failed(m_txDesc);
        }
        m_gateway.on_tx_completed(m_txDesc);
    }

    vector<Input::Ptr> BaseTransaction::getTxInputs(const TxID& txID) const
    {
        vector<Input::Ptr> inputs;
        m_keychain->visit([this, &txID, &inputs](const Coin& c)->bool
        {
            if (c.m_spentTxId == txID && c.m_status == Coin::Locked)
            {
                Input::Ptr input = make_unique<Input>();

                Scalar::Native blindingFactor = m_keychain->calcKey(c);
                input->m_Commitment = Commitment(blindingFactor, c.m_amount);

                inputs.push_back(move(input));
            }
            return true;
        });
        return inputs;
    }

    vector<Output::Ptr> BaseTransaction::getTxOutputs(const TxID& txID) const
    {
        vector<Output::Ptr> outputs;
        m_keychain->visit([this, &txID, &outputs](const Coin& c)->bool
        {
            if (c.m_createTxId == txID && c.m_status == Coin::Draft)
            {
                Output::Ptr output = make_unique<Output>();
                output->m_Coinbase = false;

                Scalar::Native blindingFactor = m_keychain->calcKey(c);
                output->Create(blindingFactor, c.m_amount);

                outputs.push_back(move(output));
            }
            return true;
        });
        return outputs;
    }

    vector<Coin> BaseTransaction::getUnconfirmedOutputs() const
    {
        vector<Coin> outputs;
        m_keychain->visit([&](const Coin& coin)
        {
            if (coin.m_createTxId == m_txDesc.m_txId && coin.m_status == Coin::Unconfirmed
                || coin.m_spentTxId == m_txDesc.m_txId && coin.m_status == Coin::Locked)
            {
                outputs.emplace_back(coin);
            }

            return true;
        });
        return outputs;
    }

    SimpleTransaction::SimpleTransaction(INegotiatorGateway& gateway
        , beam::IKeyChain::Ptr keychain
        , const TxDescription& txDesc)
        : BaseTransaction{ gateway, keychain, txDesc }
    {

    }

    void SimpleTransaction::update()
    {
        int reason = 0;
        if (getParameter(TxParameterID::FailureReason, reason))
        {
            onFailed();
            return;
        }

        bool sender = m_txDesc.m_sender;
        Scalar::Native peerOffset;
        bool initiator = !getParameter(TxParameterID::PeerOffset, peerOffset);

        auto address = m_keychain->getAddress(m_txDesc.m_peerId);
        bool isSelfTx = address.is_initialized() && address->m_own;

        Scalar::Native offset;
        Scalar::Native blindingExcess;
        if (!getParameter(TxParameterID::BlindingExcess, blindingExcess)
            || !getParameter(TxParameterID::Offset, offset))
        {
            LOG_INFO() << m_txDesc.m_txId << (sender ? " Sending " : " Receiving ") << PrintableAmount(m_txDesc.m_amount) << " (fee: " << PrintableAmount(m_txDesc.m_fee) << ")";
            Height currentHeight = m_keychain->getCurrentHeight();
            m_txDesc.m_minHeight = currentHeight;

            if (sender)
            {
                // select and lock input utxos
                Amount amountWithFee = m_txDesc.m_amount + m_txDesc.m_fee;
                auto coins = m_keychain->selectCoins(amountWithFee);
                if (coins.empty())
                {
                    LOG_ERROR() << "You only have " << PrintableAmount(getAvailable(m_keychain));
                    onFailed(!initiator);
                    return;
                }

                for (auto& coin : coins)
                {
                    blindingExcess += m_keychain->calcKey(coin);
                    coin.m_spentTxId = m_txDesc.m_txId;
                }
                m_keychain->update(coins);

                // calculate change amount and create corresponding output if needed
                Amount change = 0;
                for (const auto &coin : coins)
                {
                    change += coin.m_amount;
                }
                change -= amountWithFee;
                m_txDesc.m_change = change;

                Amount newUtxoAmount = isSelfTx ? m_txDesc.m_amount : change;

                if (change > 0)
                {
                    // create output utxo for change
                    Coin newUtxo{ change, Coin::Draft, m_txDesc.m_minHeight };
                    newUtxo.m_createTxId = m_txDesc.m_txId;
                    m_keychain->store(newUtxo);

                    Scalar::Native blindingFactor = m_keychain->calcKey(newUtxo);
                    auto[privateExcess, newOffset] = splitKey(blindingFactor, newUtxo.m_id);

                    blindingFactor = -privateExcess;
                    blindingExcess += blindingFactor;
                    offset += newOffset;
                }
            }
            if (isSelfTx || !sender)
            {
                // create receiver utxo
                Coin newUtxo{ m_txDesc.m_amount, Coin::Draft, m_txDesc.m_minHeight };
                newUtxo.m_createTxId = m_txDesc.m_txId;
                m_keychain->store(newUtxo);

                Scalar::Native blindingFactor = m_keychain->calcKey(newUtxo);
                auto[privateExcess, newOffset] = splitKey(blindingFactor, newUtxo.m_id);

                blindingFactor = -privateExcess;
                blindingExcess += blindingFactor;
                offset += newOffset;

                LOG_INFO() << m_txDesc.m_txId << " Invitation accepted";
            }

            setParameter(TxParameterID::BlindingExcess, blindingExcess);
            setParameter(TxParameterID::Offset, offset);

            updateTxDescription(TxDescription::InProgress);
        }

        auto kernel = createKernel(m_txDesc.m_fee, m_txDesc.m_minHeight);
        auto msig = createMultiSig(*kernel, blindingExcess);

        Point::Native publicNonce = Context::get().G * msig.m_Nonce;
        Point::Native publicExcess = Context::get().G * blindingExcess;

        Point::Native publicPeerNonce, publicPeerExcess;

        if (!isSelfTx && (!getParameter(TxParameterID::PublicPeerNonce, publicPeerNonce)
            || !getParameter(TxParameterID::PublicPeerExcess, publicPeerExcess)))
        {
            assert(initiator);
            const TxID& txID = m_txDesc.m_txId;

            Invite inviteMsg;
            inviteMsg.m_txId = txID;
            inviteMsg.m_amount = m_txDesc.m_amount;
            inviteMsg.m_fee = m_txDesc.m_fee;
            inviteMsg.m_height = m_txDesc.m_minHeight;
            inviteMsg.m_send = m_txDesc.m_sender;
            inviteMsg.m_inputs = getTxInputs(txID);
            inviteMsg.m_outputs = getTxOutputs(txID);
            inviteMsg.m_publicPeerExcess = publicExcess;
            inviteMsg.m_publicPeerNonce = publicNonce;
            inviteMsg.m_offset = offset;

            m_gateway.send_tx_invitation(m_txDesc, move(inviteMsg));
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
        kernel->m_Signature.CoSign(partialSignature, message, blindingExcess, msig);

        Scalar::Native peerSignature;
        if (!isSelfTx && !getParameter(TxParameterID::PeerSignature, peerSignature))
        {
            // invited participant
            assert(!initiator);
            sendConfirmInvitation(publicExcess, publicNonce, partialSignature);
            return;
        }

        // verify peer's signature
        Signature peerSig;
        peerSig.m_e = kernel->m_Signature.m_e;
        peerSig.m_k = peerSignature;
        if (!peerSig.IsValidPartial(publicPeerNonce, publicPeerExcess))
        {
            onFailed(true);
            return;
        }

        // final signature
        kernel->m_Signature.m_k = partialSignature + peerSignature;

        bool isRegistered = false;
        if (!getParameter(TxParameterID::TransactionRegistered, isRegistered))
        {
            LOG_INFO() << m_txDesc.m_txId << " Transaction registered";
            vector<Input::Ptr> inputs;
            vector<Output::Ptr> outputs;
            if (!isSelfTx && (!getParameter(TxParameterID::PeerInputs, inputs)
                || !getParameter(TxParameterID::PeerOutputs, outputs)))
            {
                // initiator 
                assert(initiator);
                Scalar s;
                partialSignature.Export(s);
                sendConfirmTransaction(s);
            }
            else
            {
                // Construct and verify transaction
                auto transaction = make_shared<Transaction>();
                transaction->m_vKernelsOutput.push_back(move(kernel));
                transaction->m_Offset = peerOffset + offset;
                transaction->m_vInputs = move(inputs);
                transaction->m_vOutputs = move(outputs);

                {
                    auto inputs = getTxInputs(m_txDesc.m_txId);
                    move(inputs.begin(), inputs.end(), back_inserter(transaction->m_vInputs));

                    auto outputs = getTxOutputs(m_txDesc.m_txId);
                    move(outputs.begin(), outputs.end(), back_inserter(transaction->m_vOutputs));
                }

                transaction->Sort();

                // Verify final transaction
                TxBase::Context ctx;
                if (!transaction->IsValid(ctx))
                {
                    onFailed(true);
                    return;
                }
                m_gateway.register_tx(m_txDesc, transaction);
            }
            return;
        }

        if (!isRegistered)
        {
            onFailed(true);
            return;
        }

        Merkle::Proof kernelProof;
        if (!getParameter(TxParameterID::KernelProof, kernelProof))
        {
            if (!initiator)
            {
                m_gateway.send_tx_registered(m_txDesc);
            }
            confirmKernel(*kernel);
            return;
        }

        Block::SystemState::Full state;
        if (!getTip(state) || !state.IsValidProofKernel(*kernel, kernelProof))
        {
            if (!m_gateway.isTestMode())
            {
                return;
            }
        }

        vector<Coin> unconfirmed = getUnconfirmedOutputs();
        if (!unconfirmed.empty())
        {
            m_gateway.confirm_outputs(unconfirmed);
            return;
        }

        completeTx();
    }

    void SimpleTransaction::sendConfirmTransaction(const Scalar& peerSignature) const
    {
        ConfirmTransaction confirmMsg;
        confirmMsg.m_txId = m_txDesc.m_txId;
        confirmMsg.m_peerSignature = peerSignature;

        m_gateway.send_tx_confirmation(m_txDesc, move(confirmMsg));
    }

    void SimpleTransaction::sendConfirmInvitation(const Point::Native& publicExcess, const Point::Native& publicNonce, const Scalar::Native& partialSignature) const
    {
        ConfirmInvitation confirmMsg;
        confirmMsg.m_txId = m_txDesc.m_txId;
        confirmMsg.m_publicPeerExcess = publicExcess;
        confirmMsg.m_peerSignature = partialSignature;
        confirmMsg.m_publicPeerNonce = publicNonce;

        m_gateway.send_tx_confirmation(m_txDesc, move(confirmMsg));
    }
}}
