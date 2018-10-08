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
                                   , const TxID& txID)
        : m_gateway{ gateway }
        , m_keychain{ keychain }
        , m_txID{ txID }
    {
        assert(keychain);
    }

    bool BaseTransaction::getTip(Block::SystemState::Full& state) const
    {
        return m_gateway.get_tip(state);
    }

    const TxID& BaseTransaction::getTxID() const
    {
        return m_txID;
    }

    void BaseTransaction::cancel()
    {
        TxStatus s = TxStatus::Failed;
        getParameter(TxParameterID::Status, s);
        if (s == TxStatus::Pending)
        {
            m_keychain->deleteTx(getTxID());
        }
        else
        {
            updateTxDescription(TxStatus::Cancelled);
            rollbackTx();
            SetTxParameter msg;
            addParameter(msg, TxParameterID::FailureReason, 0);
            sendTxParameters(move(msg));
        }
    }

    void BaseTransaction::rollbackTx()
    {
        LOG_INFO() << getTxID() << " Transaction failed. Rollback...";
        m_keychain->rollbackTx(getTxID());
    }

    void BaseTransaction::confirmKernel(const TxKernel& kernel)
    {
        updateTxDescription(TxStatus::Registered);

        auto coins = m_keychain->getCoinsCreatedByTx(getTxID());

        for (auto& coin : coins)
        {
            coin.m_status = Coin::Unconfirmed;
        }
        m_keychain->update(coins);

        m_gateway.confirm_kernel(getTxID(), kernel);
    }

    void BaseTransaction::completeTx()
    {
        LOG_INFO() << getTxID() << " Transaction completed";
        updateTxDescription(TxStatus::Completed);
        m_gateway.on_tx_completed(getTxID());
    }

    void BaseTransaction::updateTxDescription(TxStatus s)
    {
        setParameter(TxParameterID::Status, s);
        setParameter(TxParameterID::ModifyTime, getTimestamp());
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
        updateTxDescription(TxStatus::Failed);
        rollbackTx();
        if (notify)
        {
            SetTxParameter msg;
            addParameter(msg, TxParameterID::FailureReason, 0);
            sendTxParameters(move(msg));
        }
        m_gateway.on_tx_completed(getTxID());
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
            if ((coin.m_createTxId == getTxID() && coin.m_status == Coin::Unconfirmed)
                || (coin.m_spentTxId == getTxID() && coin.m_status == Coin::Locked))
            {
                outputs.emplace_back(coin);
            }

            return true;
        });
        return outputs;
    }

    bool BaseTransaction::sendTxParameters(SetTxParameter&& msg) const
    {
        msg.m_txId = getTxID();
        msg.m_Type = getType();
        
        WalletID peerID = {0};
        if (getParameter(TxParameterID::MyID, msg.m_from) 
            && getParameter(TxParameterID::PeerID, peerID))
        {
            m_gateway.send_tx_params(peerID, move(msg));
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

    TxType SimpleTransaction::getType() const
    {
        return TxType::SimpleTransaction;
    }

    void SimpleTransaction::update()
    {
        int reason = 0;
        if (getParameter(TxParameterID::FailureReason, reason))
        {
            onFailed();
            return;
        }

        bool sender = false;
        Amount amount = 0, fee = 0;
        if (!getParameter(TxParameterID::IsSender, sender) 
            || !getParameter(TxParameterID::Amount, amount)
            || !getParameter(TxParameterID::Fee, fee))
        {
            onFailed();
            return;
        }
        Scalar::Native peerOffset;
        bool initiator = !getParameter(TxParameterID::PeerOffset, peerOffset);

        WalletID peerID = { 0 };
        getParameter(TxParameterID::PeerID, peerID);
        auto address = m_keychain->getAddress(peerID);
        bool isSelfTx = address.is_initialized() && address->m_own;

        Scalar::Native offset;
        Scalar::Native blindingExcess;
        if (!getParameter(TxParameterID::BlindingExcess, blindingExcess)
            || !getParameter(TxParameterID::Offset, offset))
        {
            LOG_INFO() << getTxID() << (sender ? " Sending " : " Receiving ") << PrintableAmount(amount) << " (fee: " << PrintableAmount(fee) << ")";
            Height currentHeight = m_keychain->getCurrentHeight();
            setParameter(TxParameterID::MinHeight, currentHeight);

            if (sender)
            {
                // select and lock input utxos
                Amount amountWithFee = amount + fee;
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
                    coin.m_spentTxId = getTxID();
                }
                m_keychain->update(coins);

                // calculate change amount and create corresponding output if needed
                Amount change = 0;
                for (const auto &coin : coins)
                {
                    change += coin.m_amount;
                }
                change -= amountWithFee;
                setParameter(TxParameterID::Change, change);

                if (change > 0)
                {
                    // create output utxo for change
                    Coin newUtxo{ change, Coin::Draft, currentHeight };
                    newUtxo.m_createTxId = getTxID();
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
                Coin newUtxo{ amount, Coin::Draft, currentHeight };
                newUtxo.m_createTxId = getTxID();
                m_keychain->store(newUtxo);

                Scalar::Native blindingFactor = m_keychain->calcKey(newUtxo);
                auto[privateExcess, newOffset] = splitKey(blindingFactor, newUtxo.m_id);

                blindingFactor = -privateExcess;
                blindingExcess += blindingFactor;
                offset += newOffset;

                LOG_INFO() << getTxID() << " Invitation accepted";
            }

            setParameter(TxParameterID::BlindingExcess, blindingExcess);
            setParameter(TxParameterID::Offset, offset);

            updateTxDescription(TxStatus::InProgress);
        }

        Height minHeight = 0;
        if (!getParameter(TxParameterID::MinHeight, minHeight))
        {
            onFailed(true);
            return;
        }

        auto kernel = createKernel(fee, minHeight);
        auto msig = createMultiSig(*kernel, blindingExcess);

        Point::Native publicNonce = Context::get().G * msig.m_Nonce;
        Point::Native publicExcess = Context::get().G * blindingExcess;

        Point::Native publicPeerNonce, publicPeerExcess;

        if (!isSelfTx && (!getParameter(TxParameterID::PeerPublicNonce, publicPeerNonce)
            || !getParameter(TxParameterID::PeerPublicExcess, publicPeerExcess)))
        {
            assert(initiator);

            SetTxParameter msg;

            addParameter(msg, TxParameterID::Amount, amount);
            addParameter(msg, TxParameterID::Fee, fee);
            addParameter(msg, TxParameterID::MinHeight, minHeight);
            addParameter(msg, TxParameterID::IsSender, !sender);
            addParameter(msg, TxParameterID::PeerInputs, getTxInputs(getTxID()));
            addParameter(msg, TxParameterID::PeerOutputs, getTxOutputs(getTxID()));
            addParameter(msg, TxParameterID::PeerPublicExcess, publicExcess);
            addParameter(msg, TxParameterID::PeerPublicNonce, publicNonce);
            addParameter(msg, TxParameterID::PeerOffset, offset);

            if (!sendTxParameters(move(msg)))
            {
                onFailed(false);
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
        if (!isSelfTx && !getParameter(TxParameterID::PeerSignature, peerSignature))
        {
            // invited participant
            assert(!initiator);
            // Confirm invitation
            SetTxParameter msg;
            addParameter(msg, TxParameterID::PeerPublicExcess, publicExcess);
            addParameter(msg, TxParameterID::PeerSignature, partialSignature);
            addParameter(msg, TxParameterID::PeerPublicNonce, publicNonce);

            sendTxParameters(move(msg));
            return;
        }

        // verify peer's signature
        Signature peerSig;
        peerSig.m_NoncePub = msig.m_NoncePub;
        peerSig.m_k = peerSignature;
        if (!peerSig.IsValidPartial(message, publicPeerNonce, publicPeerExcess))
        {
            onFailed(true);
            return;
        }

        // final signature
        kernel->m_Signature.m_k = partialSignature + peerSignature;
        kernel->m_Signature.m_NoncePub = msig.m_NoncePub;

        bool isRegistered = false;
        if (!getParameter(TxParameterID::TransactionRegistered, isRegistered))
        {
            LOG_INFO() << getTxID() << " Transaction registered";
            vector<Input::Ptr> inputs;
            vector<Output::Ptr> outputs;
            if (!isSelfTx && (!getParameter(TxParameterID::PeerInputs, inputs)
                || !getParameter(TxParameterID::PeerOutputs, outputs)))
            {
                // initiator 
                assert(initiator);
                Scalar s;
                partialSignature.Export(s);
                //sendConfirmTransaction(s);


                // Confirm transaction
                SetTxParameter msg;
                addParameter(msg, TxParameterID::PeerSignature, s);
                
                sendTxParameters(move(msg));
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
                    auto inputs2 = getTxInputs(getTxID());
                    move(inputs2.begin(), inputs2.end(), back_inserter(transaction->m_vInputs));

                    auto outputs2 = getTxOutputs(getTxID());
                    move(outputs2.begin(), outputs2.end(), back_inserter(transaction->m_vOutputs));
                }

                transaction->Sort();

                // Verify final transaction
                TxBase::Context ctx;
                if (!transaction->IsValid(ctx))
                {
                    onFailed(true);
                    return;
                }
                m_gateway.register_tx(getTxID(), transaction);
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
                // notify peer that transaction has been registered
                SetTxParameter msg;
                addParameter(msg, TxParameterID::TransactionRegistered, true);
                sendTxParameters(move(msg));
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

}}
