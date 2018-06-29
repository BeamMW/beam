#include "sender.h"
#include "wallet/wallet_serialization.h"

namespace beam::wallet
{
    using namespace ECC;
    using namespace std;

    /// Sender

    void Sender::FSMDefinition::inviteReceiver(const events::TxSend&)
    {
        LOG_INFO() << "Sending " << PrintableAmount(m_parent.m_txDesc.m_amount);
        // 1. Create transaction Uuid
        InviteReceiver invitationData;
        invitationData.m_txId = m_parent.m_txDesc.m_txId;
        Height currentHeight = m_parent.m_keychain->getCurrentHeight();

        auto coins = m_parent.m_keychain->getCoins(m_parent.m_txDesc.m_amount); // need to lock 
        if (coins.empty())
        {
            LOG_ERROR() << "You only have " << PrintableAmount(get_total());
            throw runtime_error("no money");
        }
        invitationData.m_amount = m_parent.m_txDesc.m_amount;
        
        // create kernel
        m_parent.m_kernel = make_unique<TxKernel>();
        m_parent.m_kernel->m_Fee = 0;
        m_parent.m_kernel->m_Height.m_Min = currentHeight;
        m_parent.m_kernel->m_Height.m_Max = MaxHeight;

        m_parent.m_kernel->get_HashForSigning(invitationData.m_message);
        
        // 2. Set lock_height for output (current chain height)
        // 3. Select inputs using desired selection strategy
        {
            m_parent.m_blindingExcess = Zero;
            for (const auto& coin: coins)
            {
                assert(coin.m_status == Coin::Locked);
                Input::Ptr input = make_unique<Input>();

                Scalar::Native key{ m_parent.m_keychain->calcKey(coin) };
                input->m_Commitment = Commitment(key, coin.m_amount);

                invitationData.m_inputs.push_back(move(input));
                m_parent.m_blindingExcess += key;
            }
        }
        // 4. Create change_output
        // 5. Select blinding factor for change_output
        {
            Amount change = 0;
            for (const auto &coin : coins)
            {
                change += coin.m_amount;
            }

            change -= m_parent.m_txDesc.m_amount;
            if (change > 0)
            {
                auto changeCoin = beam::Coin(change, Coin::Unconfirmed, currentHeight);
                m_parent.m_keychain->store(changeCoin);
                Output::Ptr output = make_unique<Output>();
                output->m_Coinbase = false;
                Scalar::Native blindingFactor = m_parent.m_keychain->calcKey(changeCoin);
                output->Create(blindingFactor, change);

                blindingFactor = -blindingFactor;
                m_parent.m_blindingExcess += blindingFactor;

                invitationData.m_outputs.push_back(move(output));
            }
        }
        // 6. calculate tx_weight
        // 7. calculate fee
        // 8. Calculate total blinding excess for all inputs and outputs xS
        // 9. Select random nonce kS
        Signature::MultiSig msig;
		msig.GenerateNonce(invitationData.m_message, m_parent.m_blindingExcess);
        
        // 10. Multiply xS and kS by generator G to create public curve points xSG and kSG
        Point::Native publicBlindingExcess = Context::get().G *m_parent.m_blindingExcess;
        invitationData.m_publicSenderBlindingExcess = publicBlindingExcess;

        invitationData.m_publicSenderNonce = Context::get().G * msig.m_Nonce;

        update_tx_description(TxDescription::InProgress);
        m_parent.m_gateway.send_tx_invitation(m_parent.m_txDesc, move(invitationData));
    }

    bool Sender::FSMDefinition::isValidSignature(const events::TxInvitationCompleted& event)
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

    bool Sender::FSMDefinition::isValidSignature(const events::TxConfirmationCompleted& event)
    {
        auto& data = event.data;
        // 1. Verify sender's Schnor signature

        Signature sigPeer;
        sigPeer.m_e = m_parent.m_kernel->m_Signature.m_e;
        sigPeer.m_k = data.m_senderSignature;
        return sigPeer.IsValidPartial(m_parent.m_publicPeerNonce, m_parent.m_publicPeerBlindingExcess);
    }

    void Sender::FSMDefinition::confirmReceiver(const events::TxInvitationCompleted& event)
    {
        auto& data = event.data;
        // 4. Compute Sender Schnorr signature

        ConfirmTransaction confirmationData;
        confirmationData.m_txId = m_parent.m_txDesc.m_txId;
		
        Hash::Value message;
        m_parent.m_kernel->get_HashForSigning(message);
		
        Signature::MultiSig msig;
        msig.GenerateNonce(message, m_parent.m_blindingExcess);
        Point::Native publicNonce = Context::get().G * msig.m_Nonce;
        msig.m_NoncePub = publicNonce + data.m_publicPeerNonce;

        Scalar::Native senderSignature;
        m_parent.m_kernel->m_Signature.CoSign(senderSignature, message, m_parent.m_blindingExcess, msig);

        confirmationData.m_senderSignature = senderSignature;
        update_tx_description(TxDescription::InProgress);
        m_parent.m_gateway.send_tx_confirmation(m_parent.m_txDesc, move(confirmationData));
    }

    /// Receiver

    void Sender::FSMDefinition::confirmReceiverInvitation(const events::TxReceiverInvited& event)
    {
        auto& data = event.data;
        m_parent.m_message = data.m_message;
        m_parent.m_publicPeerBlindingExcess = data.m_publicSenderBlindingExcess;
        m_parent.m_publicPeerNonce = data.m_publicSenderNonce;

        m_parent.m_transaction = make_shared<Transaction>();
        m_parent.m_transaction->m_Offset = ECC::Zero;
    //    m_transaction->m_vInputs = move(data.m_inputs);
    //    m_transaction->m_vOutputs = move(data.m_outputs);
        update_tx_description(TxDescription::Pending);

        LOG_INFO() << "Receiving " << PrintableAmount(m_parent.m_txDesc.m_amount);
        ConfirmInvitation confirmationData;
        confirmationData.m_txId = m_parent.m_txDesc.m_txId;

        Height currentHeight = m_parent.m_keychain->getCurrentHeight();

        m_parent.m_kernel = make_unique<TxKernel>();
        m_parent.m_kernel->m_Fee = 0;
        m_parent.m_kernel->m_Height.m_Min = currentHeight;
        m_parent.m_kernel->m_Height.m_Max = MaxHeight;

        // 1. Check fee
        // 2. Create receiver_output
        // 3. Choose random blinding factor for receiver_output
        Amount amount = m_parent.m_txDesc.m_amount;
        Output::Ptr output = make_unique<Output>();
        output->m_Coinbase = false;
        Coin coin{ m_parent.m_txDesc.m_amount, Coin::Unconfirmed, currentHeight };
        coin.m_createTxId = m_parent.m_txDesc.m_txId;
        m_parent.m_keychain->store(coin);
        Scalar::Native blindingFactor = m_parent.m_keychain->calcKey(coin);
        output->Create(blindingFactor, amount);
        auto[privateExcess, offset] = splitKey(blindingFactor, coin.m_id);

        m_parent.m_blindingExcess = -privateExcess;
        assert(m_parent.m_transaction->m_Offset.m_Value == Zero);
        m_parent.m_transaction->m_Offset = offset;

        m_parent.m_transaction->m_vOutputs.push_back(move(output));

        // 4. Calculate message M
        // 5. Choose random nonce
        Signature::MultiSig msig;
        msig.GenerateNonce(m_parent.m_message, m_parent.m_blindingExcess);
        // 6. Make public nonce and blinding factor
        Point::Native publicBlindingExcess;
        publicBlindingExcess = Context::get().G * m_parent.m_blindingExcess;
        confirmationData.m_publicPeerBlindingExcess = publicBlindingExcess;

        Point::Native publicNonce;
        publicNonce = Context::get().G * msig.m_Nonce;
        confirmationData.m_publicPeerNonce = publicNonce;

        // 7. Compute Shnorr challenge e = H(M|K)

        msig.m_NoncePub = m_parent.m_publicPeerNonce + confirmationData.m_publicPeerNonce;

        // 8. Compute recepient Shnorr signature
        Scalar::Native receiverSignature;
        m_parent.m_kernel->m_Signature.CoSign(receiverSignature, m_parent.m_message, m_parent.m_blindingExcess, msig);

        confirmationData.m_peerSignature = receiverSignature;

        update_tx_description(TxDescription::InProgress);
        m_parent.m_gateway.send_tx_confirmation(m_parent.m_txDesc, move(confirmationData));
    }

    void Sender::FSMDefinition::confirmSenderInvitation(const events::TxSenderInvited&)
    {

    }

    /// Common

    void Sender::FSMDefinition::registerTx(const events::TxConfirmationCompleted& event)
    {
        // 2. Calculate final signature
        Scalar::Native senderSignature;
        senderSignature = event.data.m_senderSignature;

        Signature::MultiSig msig;
        msig.GenerateNonce(m_parent.m_message, m_parent.m_blindingExcess);
        Point::Native publicNonce;
        publicNonce = Context::get().G * msig.m_Nonce;
        msig.m_NoncePub = m_parent.m_publicPeerNonce + publicNonce;
        Scalar::Native receiverSignature;
        m_parent.m_kernel->m_Signature.CoSign(receiverSignature, m_parent.m_message, m_parent.m_blindingExcess, msig);

        Scalar::Native finialSignature = senderSignature + receiverSignature;

        // 3. Calculate public key for excess
        Point::Native x;
        x = Context::get().G * m_parent.m_blindingExcess;
        x += m_parent.m_publicPeerBlindingExcess;
        // 4. Verify excess value in final transaction
        // 5. Create transaction kernel
        m_parent.m_kernel->m_Excess = x;
        m_parent.m_kernel->m_Signature.m_k = finialSignature;

        m_parent.m_transaction->m_vKernelsOutput.push_back(move(m_parent.m_kernel));

        // 6. Create final transaction and send it to mempool
        m_parent.m_transaction->Sort();

        update_tx_description(TxDescription::InProgress);
        m_parent.m_gateway.register_tx(m_parent.m_txDesc, m_parent.m_transaction);
    }

    void Sender::FSMDefinition::rollbackTx(const events::TxFailed& )
    {
        update_tx_description(TxDescription::Failed);
        rollbackTx();
    }

    void Sender::FSMDefinition::confirmOutputs(const events::TxConfirmationCompleted&)
    {

    }

	void Sender::FSMDefinition::rollbackTx()
	{
        LOG_DEBUG() << "Transaction failed. Rollback...";
        m_parent.m_keychain->rollbackTx(m_parent.m_txDesc.m_txId);
	}

    void Sender::FSMDefinition::completeTx(const events::TxOutputsConfirmed&)
    {
        complete_tx();
    }

    void Sender::FSMDefinition::complete_tx()
    {
        LOG_DEBUG() << "Transaction completed";
        update_tx_description(TxDescription::Completed);
    }

    void Sender::FSMDefinition::inviteSender(const events::TxBill&)
    {

    }

    void Sender::FSMDefinition::confirmSender(const events::TxInvitationCompleted&)
    {

    }

    void Sender::FSMDefinition::confirmOutputs(const events::TxRegistrationCompleted&)
    {

    }


    Amount Sender::FSMDefinition::get_total() const
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

    void Sender::FSMDefinition::update_tx_description(TxDescription::Status s)
    {
        m_parent.m_txDesc.m_status = s;
        m_parent.m_txDesc.m_modifyTime = wallet::getTimestamp();
        Serializer ser;
        ser & *this;
        ser.swap_buf(m_parent.m_txDesc.m_fsmState);
        m_parent.m_keychain->saveTx(m_parent.m_txDesc);
    }

}
