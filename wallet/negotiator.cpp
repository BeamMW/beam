#include "negotiator.h"
#include "core/block_crypt.h"
#include "wallet/wallet_serialization.h"

namespace beam::wallet
{
    using namespace ECC;
    using namespace std;

    void Negotiator::FSMDefinition::invitePeer(const events::TxInitiated&)
    {
        bool sender = m_parent.m_txDesc.m_sender;
        LOG_INFO() << ( sender ? "Sending " : "Receiving ") << PrintableAmount(m_parent.m_txDesc.m_amount);
        
        Height currentHeight = m_parent.m_keychain->getCurrentHeight();
        // TODO: add ability to calculate fee
        m_parent.createKernel(m_parent.m_txDesc.m_fee, currentHeight);
        m_parent.m_txDesc.m_minHeight = currentHeight;
        Invite inviteMsg;
        inviteMsg.m_txId = m_parent.m_txDesc.m_txId;
        inviteMsg.m_amount = m_parent.m_txDesc.m_amount;
        inviteMsg.m_fee = m_parent.m_txDesc.m_fee;
        inviteMsg.m_height = currentHeight;
        inviteMsg.m_send = sender;

        if (sender)
        {
            if (!getSenderInputsAndOutputs(currentHeight, inviteMsg.m_inputs, inviteMsg.m_outputs))
            {
                Negotiator::Fsm &fsm = static_cast<Negotiator::Fsm&>(*this);
                fsm.process_event(events::TxFailed{});
                return;
            }
        }
        else
        {
            inviteMsg.m_outputs.push_back(m_parent.createOutput(m_parent.m_txDesc.m_amount, currentHeight));
        }

        inviteMsg.m_publicPeerExcess = m_parent.getPublicExcess();
        inviteMsg.m_publicPeerNonce = m_parent.getPublicNonce();
        inviteMsg.m_offset = m_parent.m_offset;

        update_tx_description(TxDescription::InProgress);
        m_parent.m_gateway.send_tx_invitation(m_parent.m_txDesc, move(inviteMsg));
    }

    void Negotiator::FSMDefinition::confirmPeer(const events::TxInvitationCompleted& event)
    {
        auto& msg = event.data;

        if (!m_parent.isValidSignature(msg.m_peerSignature, msg.m_publicPeerNonce, msg.m_publicPeerExcess))
        {
            Negotiator::Fsm &fsm = static_cast<Negotiator::Fsm&>(*this);
            fsm.process_event(events::TxFailed{true});
            return;
        }

        m_parent.m_publicPeerExcess = msg.m_publicPeerExcess;
        m_parent.m_publicPeerNonce = msg.m_publicPeerNonce;
        m_parent.m_peerSignature = msg.m_peerSignature;

        ConfirmTransaction confirmMsg;
        confirmMsg.m_txId = m_parent.m_txDesc.m_txId;
        confirmMsg.m_peerSignature = m_parent.createSignature();

        update_tx_description(TxDescription::InProgress);
        m_parent.m_gateway.send_tx_confirmation(m_parent.m_txDesc, move(confirmMsg));
    }

    void Negotiator::FSMDefinition::confirmInvitation(const events::TxInvited&)
    {
        update_tx_description(TxDescription::Pending);
        bool sender = m_parent.m_txDesc.m_sender;
        LOG_INFO() << (sender ? "Sending " : "Receiving ") << PrintableAmount(m_parent.m_txDesc.m_amount);
        Height currentHeight = m_parent.m_keychain->getCurrentHeight();

        m_parent.createKernel(m_parent.m_txDesc.m_fee, m_parent.m_txDesc.m_minHeight);
        // TODO: check fee

        if (sender)
        {
            if (!getSenderInputsAndOutputs(currentHeight, m_parent.m_transaction->m_vInputs, m_parent.m_transaction->m_vOutputs))
            {
                Negotiator::Fsm &fsm = static_cast<Negotiator::Fsm&>(*this);
                fsm.process_event(events::TxFailed{true});
                return;
            }
        }
        else
        {
            // create receiver output
            m_parent.m_transaction->m_vOutputs.push_back(m_parent.createOutput(m_parent.m_txDesc.m_amount, currentHeight));
        }

        ConfirmInvitation confirmMsg;
        confirmMsg.m_txId = m_parent.m_txDesc.m_txId;
        confirmMsg.m_publicPeerExcess = m_parent.getPublicExcess();
        m_parent.createSignature2(confirmMsg.m_peerSignature, confirmMsg.m_publicPeerNonce);

        update_tx_description(TxDescription::InProgress);
        m_parent.m_gateway.send_tx_confirmation(m_parent.m_txDesc, move(confirmMsg));
    }

    void Negotiator::FSMDefinition::registerTx(const events::TxConfirmationCompleted& event)
    {
        if (!m_parent.isValidSignature(event.data.m_peerSignature))
        {
            Negotiator::Fsm &fsm = static_cast<Negotiator::Fsm&>(*this);
            fsm.process_event(events::TxFailed{ true });
            return;
        }

        // Calculate final signature
        Scalar::Native senderSignature;
        senderSignature = event.data.m_peerSignature;
        Scalar::Native receiverSignature = m_parent.createSignature();
        Scalar::Native finialSignature = senderSignature + receiverSignature;

        // Calculate public key for excess
        Point::Native x = m_parent.getPublicExcess();
        x += m_parent.m_publicPeerExcess;

        // Create transaction kernel and transaction
        m_parent.m_kernel->m_Excess = x;
        m_parent.m_kernel->m_Signature.m_k = finialSignature;
        m_parent.m_transaction->m_vKernelsOutput.push_back(move(m_parent.m_kernel));
        m_parent.m_transaction->m_Offset = m_parent.m_offset;
        m_parent.m_transaction->Sort();

        // Verify final transaction
        TxBase::Context ctx;
        if (!m_parent.m_transaction->IsValid(ctx))
        {
            Negotiator::Fsm &fsm = static_cast<Negotiator::Fsm&>(*this);
            fsm.process_event(events::TxFailed{ true });
            return;
        }

        update_tx_description(TxDescription::InProgress);
        m_parent.m_gateway.register_tx(m_parent.m_txDesc, m_parent.m_transaction);
    }

    void Negotiator::FSMDefinition::rollbackTx(const events::TxFailed& event)
    {
        update_tx_description(TxDescription::Failed);
        rollbackTx();
        if (event.m_notify)
        {
            m_parent.m_gateway.send_tx_failed(m_parent.m_txDesc);
        }
    }

    void Negotiator::FSMDefinition::rollbackTx()
    {
        LOG_DEBUG() << "Transaction failed. Rollback...";
        m_parent.m_keychain->rollbackTx(m_parent.m_txDesc.m_txId);
    }

    void Negotiator::FSMDefinition::confirmOutputs(const events::TxRegistrationCompleted&)
    {
        completeTx();
    }

    void Negotiator::FSMDefinition::completeTx(const events::TxRegistrationCompleted&)
    {
        LOG_INFO() << "Transaction registered";
        update_tx_description(TxDescription::Completed);
        m_parent.m_gateway.send_tx_registered(m_parent.m_txDesc);
    }
    
    void Negotiator::FSMDefinition::completeTx(const events::TxOutputsConfirmed&)
    {
        completeTx();
    }

    void Negotiator::FSMDefinition::completeTx()
    {
        LOG_DEBUG() << "Transaction completed";
        update_tx_description(TxDescription::Completed);
    }

    Amount Negotiator::FSMDefinition::get_total() const
    {
        auto currentHeight = m_parent.m_keychain->getCurrentHeight();
        Amount total = 0;
        m_parent.m_keychain->visit([&total, &currentHeight](const Coin& c)->bool
        {
            if (c.m_status == Coin::Unspent && c.m_maturity <= currentHeight)
            {
                total += c.m_amount;
            }
            return true;
        });
        return total;
    }

    void Negotiator::FSMDefinition::update_tx_description(TxDescription::Status s)
    {
        m_parent.m_txDesc.m_status = s;
        m_parent.m_txDesc.m_modifyTime = getTimestamp();
        Serializer ser;
        ser & *this;
        ser.swap_buf(m_parent.m_txDesc.m_fsmState);
        m_parent.m_keychain->saveTx(m_parent.m_txDesc);
    }

    bool Negotiator::FSMDefinition::getSenderInputsAndOutputs(const Height& currentHeight, std::vector<Input::Ptr>& inputs, std::vector<Output::Ptr>& outputs)
    {
        auto coins = m_parent.m_keychain->getCoins(m_parent.m_txDesc.m_amount);
        if (coins.empty())
        {
            LOG_ERROR() << "You only have " << PrintableAmount(get_total());
            return false;
        }
        for (auto& coin : coins)
        {
            inputs.push_back(m_parent.createInput(coin));
            coin.m_spentTxId = m_parent.m_txDesc.m_txId;
        }
        m_parent.m_keychain->update(coins);
        // calculate change amount and create corresponding output if needed
        Amount change = 0;
        for (const auto &coin : coins)
        {
            change += coin.m_amount;
        }
        change -= m_parent.m_txDesc.m_amount;
        if (change > 0)
        {
            outputs.push_back(m_parent.createOutput(change, currentHeight));
        }
        return true;
    }

    void Negotiator::createKernel(Amount fee, Height minHeight)
    {
        m_kernel = make_unique<TxKernel>();
        m_kernel->m_Fee = fee;
        m_kernel->m_Height.m_Min = minHeight;
        m_kernel->m_Height.m_Max = MaxHeight;
    }

    Input::Ptr Negotiator::createInput(const Coin& utxo)
    {
        assert(utxo.m_status == Coin::Locked);
        Input::Ptr input = make_unique<Input>();

        Scalar::Native blindingFactor = m_keychain->calcKey(utxo);
        input->m_Commitment = Commitment(blindingFactor, utxo.m_amount);

        m_blindingExcess += blindingFactor;

        return input;
    }

    Output::Ptr Negotiator::createOutput(Amount amount, Height height)
    {
        Coin newUtxo{ amount, Coin::Unconfirmed, height };
        newUtxo.m_createTxId = m_txDesc.m_txId;
        m_keychain->store(newUtxo);

        Output::Ptr output = make_unique<Output>();
        output->m_Coinbase = false;
        
        Scalar::Native blindingFactor = m_keychain->calcKey(newUtxo);
        output->Create(blindingFactor, amount);
        auto [privateExcess, offset] = splitKey(blindingFactor, newUtxo.m_id);

        blindingFactor = -privateExcess;
        m_blindingExcess += blindingFactor;
        m_offset += offset;

        return output;
    }

    void Negotiator::setPublicPeerNonce(const Point& publicPeerNonce)
    {
        m_publicPeerNonce = publicPeerNonce;
    }

    Scalar Negotiator::createSignature()
    {
        Point publicNonce;
        Scalar partialSignature;
        createSignature2(partialSignature, publicNonce);
        return partialSignature;
    }

    void Negotiator::createSignature2(Scalar& signature, Point& publicNonce)
    {
        Hash::Value message;
        m_kernel->get_HashForSigning(message);

        Signature::MultiSig msig;
        msig.GenerateNonce(message, m_blindingExcess);

        publicNonce = Context::get().G * msig.m_Nonce;
        msig.m_NoncePub = m_publicPeerNonce + publicNonce;

        Scalar::Native partialSignature;
        m_kernel->m_Signature.CoSign(partialSignature, message, m_blindingExcess, msig);
        signature = partialSignature;
    }

    Point Negotiator::getPublicExcess()
    {
        return Point(Context::get().G * m_blindingExcess);
    }

    Point Negotiator::getPublicNonce()
    {
        Hash::Value message;
        m_kernel->get_HashForSigning(message);

        Signature::MultiSig msig;
        msig.GenerateNonce(message, m_blindingExcess);

        return Point(Context::get().G * msig.m_Nonce);
    }

    bool Negotiator::isValidSignature(const Scalar& peerSignature)
    {
        return isValidSignature(peerSignature, m_publicPeerNonce, m_publicPeerExcess);
    }

    bool Negotiator::isValidSignature(const Scalar& peerSignature, const Point& publicPeerNonce, const Point& publicPeerExcess)
    {
        //assert(m_kernel);
        if (!m_kernel)
        {
            return false;
        }
        Signature::MultiSig msig;
        Hash::Value message;
        m_kernel->get_HashForSigning(message);

        msig.GenerateNonce(message, m_blindingExcess);
        Point::Native publicNonce = Context::get().G * msig.m_Nonce;
        msig.m_NoncePub = publicNonce + publicPeerNonce;

        // temp signature to calc challenge
        Scalar::Native mySig;
        Signature peerSig;
        peerSig.CoSign(mySig, message, m_blindingExcess, msig);
        peerSig.m_k = peerSignature;
        return peerSig.IsValidPartial(publicPeerNonce, publicPeerExcess);
    }
}
