#include "sender.h"
#include "wallet/wallet_serialization.h"

namespace beam::wallet
{
    using namespace ECC;
    using namespace std;

    /// Sender

    void Negotiator::FSMDefinition::inviteReceiver(const events::TxSend&)
    {
        LOG_INFO() << "Sending " << PrintableAmount(m_parent.m_txDesc.m_amount);

        InviteReceiver invitationData;
        invitationData.m_txId = m_parent.m_txDesc.m_txId;
        Height currentHeight = m_parent.m_keychain->getCurrentHeight();

        invitationData.m_amount = m_parent.m_txDesc.m_amount;
        invitationData.m_fee = m_parent.m_txDesc.m_fee;

        // Select inputs using desired selection strategy
        auto coins = m_parent.m_keychain->getCoins(m_parent.m_txDesc.m_amount);
        if (coins.empty())
        {
            LOG_ERROR() << "You only have " << PrintableAmount(get_total());
            throw runtime_error("no money");
        }
        for (const auto& coin: coins)
        {
            invitationData.m_inputs.push_back(m_parent.createInput(coin));
        }

        // calculate change amount and create corresponding output if needed
        Amount change = 0;
        for (const auto &coin : coins)
        {
            change += coin.m_amount;
        }
        change -= m_parent.m_txDesc.m_amount;
        if (change > 0)
        {
            invitationData.m_outputs.push_back(m_parent.createOutput(change, currentHeight));
        }

        // TODO: add ability to calculate fee
        m_parent.createKernel(m_parent.m_txDesc.m_fee, currentHeight);


        invitationData.m_publicSenderExcess = m_parent.getPublicExcess();
        invitationData.m_publicSenderNonce = m_parent.getPublicNonce();
        invitationData.m_offset = m_parent.m_offset;

        update_tx_description(TxDescription::InProgress);
        m_parent.m_gateway.send_tx_invitation(m_parent.m_txDesc, move(invitationData));
    }

    bool Negotiator::FSMDefinition::isValidSignature(const events::TxInvitationCompleted& event)
    {
        auto& data = event.data;
        // 4. Compute Sender Schnorr signature
        // 1. Calculate message m
        Signature::MultiSig msig;

        Hash::Value message;
        m_parent.m_kernel->get_HashForSigning(message);

        msig.GenerateNonce(message, m_parent.m_blindingExcess);
        Point::Native publicNonce = Context::get().G * msig.m_Nonce;
        msig.m_NoncePub = publicNonce + data.m_publicPeerNonce;

        // temp signature to calc challenge
        Scalar::Native senderSignature;
        m_parent.m_kernel->m_Signature.CoSign(senderSignature, message, m_parent.m_blindingExcess, msig);

        // 3. Verify recepients Schnorr signature 
        Signature sigPeer;
        sigPeer.m_e = m_parent.m_kernel->m_Signature.m_e;
        sigPeer.m_k = data.m_peerSignature;
        return sigPeer.IsValidPartial(data.m_publicPeerNonce, data.m_publicPeerBlindingExcess);
    }

    void Negotiator::FSMDefinition::confirmReceiver(const events::TxInvitationCompleted& event)
    {
        auto& data = event.data;
        m_parent.setPublicPeerNonce(data.m_publicPeerNonce);
        if (!isValidSignature(event))
        {
            Negotiator::Fsm &fsm = static_cast<Negotiator::Fsm&>(*this);
            fsm.process_event(events::TxFailed{true});
            return;
        }

        // Compute Sender Schnorr signature
        ConfirmTransaction confirmationData;
        confirmationData.m_txId = m_parent.m_txDesc.m_txId;
        confirmationData.m_senderSignature = m_parent.createSignature();

        update_tx_description(TxDescription::InProgress);
        m_parent.m_gateway.send_tx_confirmation(m_parent.m_txDesc, move(confirmationData));
    }


    void Negotiator::FSMDefinition::confirmSenderInvitation(const events::TxSenderInvited&)
    {

    }

    /// Common


    void Negotiator::FSMDefinition::rollbackTx(const events::TxFailed& event)
    {
        update_tx_description(TxDescription::Failed);
        rollbackTx();
        m_parent.m_gateway.send_tx_failed(m_parent.m_txDesc);
    }

    void Negotiator::FSMDefinition::confirmOutputs(const events::TxConfirmationCompleted&)
    {

    }

    void Negotiator::FSMDefinition::rollbackTx()
    {
        LOG_DEBUG() << "Transaction failed. Rollback...";
        m_parent.m_keychain->rollbackTx(m_parent.m_txDesc.m_txId);
    }

    void Negotiator::FSMDefinition::completeTx(const events::TxOutputsConfirmed&)
    {
        completeTx();
    }

    void Negotiator::FSMDefinition::completeTx(const events::TxRegistrationCompleted&)
    {
        LOG_INFO() << "Transaction completed and sent to node";
        update_tx_description(TxDescription::Completed);
        m_parent.m_gateway.send_tx_registered(m_parent.m_txDesc);
    }
    
    void Negotiator::FSMDefinition::completeTx(const events::TxConfirmationCompleted&)
    {
        completeTx();
    }

    void Negotiator::FSMDefinition::completeTx()
    {
        LOG_DEBUG() << "Transaction completed";
        update_tx_description(TxDescription::Completed);
    }

    void Negotiator::FSMDefinition::confirmOutputs(const events::TxRegistrationCompleted& event)
    {

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
        m_parent.m_txDesc.m_modifyTime = wallet::getTimestamp();
        Serializer ser;
        ser & *this;
        ser.swap_buf(m_parent.m_txDesc.m_fsmState);
        m_parent.m_keychain->saveTx(m_parent.m_txDesc);
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
        //assert(!(m_publicPeerNonce == Zero));
        Hash::Value message;
        m_kernel->get_HashForSigning(message);

        Signature::MultiSig msig;
        msig.GenerateNonce(message, m_blindingExcess);

        Point::Native publicNonce = Context::get().G * msig.m_Nonce;
        msig.m_NoncePub = m_publicPeerNonce + publicNonce;

        Scalar::Native partialSignature;
        m_kernel->m_Signature.CoSign(partialSignature, message, m_blindingExcess, msig);

        return Scalar(partialSignature);
    }

    void Negotiator::createSignature2(ECC::Scalar& signature, ECC::Point& publicNonce)
    {
        assert(!(m_publicPeerNonce == Zero));
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
}
