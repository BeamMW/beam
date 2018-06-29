#include "sender.h"
#include "core/block_crypt.h"
#include "wallet/wallet_serialization.h"

namespace beam::wallet
{
    using namespace ECC;
    using namespace std;

    void Sender::FSMDefinition::confirmReceiverInvitation(const events::TxReceiverInvited& event)
    {
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

    bool Sender::FSMDefinition::isValidSignature(const events::TxConfirmationCompleted& event)
    {
        // Verify sender's Schnor signature
        auto& data = event.data;
        Signature sigPeer;
        sigPeer.m_e = m_parent.m_kernel->m_Signature.m_e;
        sigPeer.m_k = data.m_senderSignature;
        return sigPeer.IsValidPartial(m_parent.m_publicPeerNonce, m_parent.m_publicPeerBlindingExcess);
    }

    void Sender::FSMDefinition::registerTx(const events::TxConfirmationCompleted& event)
    {
        if (!isValidSignature(event))
        {
            Sender::Fsm &fsm = static_cast<Sender::Fsm&>(*this);
            fsm.process_event(events::TxFailed{ true });
            return;
        }

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

    void Sender::FSMDefinition::inviteSender(const events::TxBill&)
    {

    }

    void Sender::FSMDefinition::confirmSender(const events::TxInvitationCompleted&)
    {

    }
}
