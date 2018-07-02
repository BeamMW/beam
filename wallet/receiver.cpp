#include "sender.h"
#include "core/block_crypt.h"
#include "wallet/wallet_serialization.h"

namespace beam::wallet
{
    using namespace ECC;
    using namespace std;

    void Negotiator::FSMDefinition::confirmReceiverInvitation(const events::TxReceiverInvited& event)
    {
        update_tx_description(TxDescription::Pending);

        LOG_INFO() << "Receiving " << PrintableAmount(m_parent.m_txDesc.m_amount);
        ConfirmInvitation confirmationData;
        confirmationData.m_txId = m_parent.m_txDesc.m_txId;

        Height currentHeight = m_parent.m_keychain->getCurrentHeight();

        m_parent.createKernel(m_parent.m_txDesc.m_fee, currentHeight);
        // 1. Check fee
        
        // 2. Create receiver_output
        // 3. Choose random blinding factor for receiver_output
        Amount amount = m_parent.m_txDesc.m_amount;
        m_parent.m_transaction->m_vOutputs.push_back(m_parent.createOutput(amount, currentHeight));
        
        confirmationData.m_publicPeerBlindingExcess = m_parent.getPublicExcess();

        // 4. Calculate message M
        // 5. Choose random nonce
        // 6. Make public nonce and blinding factor
        // 7. Compute Shnorr challenge e = H(M|K)
        // 8. Compute recepient Shnorr signature
        m_parent.createSignature2(confirmationData.m_peerSignature, confirmationData.m_publicPeerNonce);

        update_tx_description(TxDescription::InProgress);
        m_parent.m_gateway.send_tx_confirmation(m_parent.m_txDesc, move(confirmationData));
    }

    bool Negotiator::FSMDefinition::isValidSignature(const events::TxConfirmationCompleted& event)
    {
        // Verify sender's Schnor signature
        auto& data = event.data;
        Signature sigPeer;
        sigPeer.m_e = m_parent.m_kernel->m_Signature.m_e;
        sigPeer.m_k = data.m_senderSignature;
        return sigPeer.IsValidPartial(m_parent.m_publicPeerNonce, m_parent.m_publicPeerBlindingExcess);
    }

    void Negotiator::FSMDefinition::registerTx(const events::TxConfirmationCompleted& event)
    {
        if (!isValidSignature(event))
        {
            Negotiator::Fsm &fsm = static_cast<Negotiator::Fsm&>(*this);
            fsm.process_event(events::TxFailed{ true });
            return;
        }

        // 2. Calculate final signature
        Scalar::Native senderSignature;
        senderSignature = event.data.m_senderSignature;
        Scalar::Native receiverSignature = m_parent.createSignature();
        Scalar::Native finialSignature = senderSignature + receiverSignature;

        // 3. Calculate public key for excess
        Point::Native x = m_parent.getPublicExcess();
        x += m_parent.m_publicPeerBlindingExcess;
        // 4. Verify excess value in final transaction
        // 5. Create transaction kernel
        m_parent.m_kernel->m_Excess = x;
        m_parent.m_kernel->m_Signature.m_k = finialSignature;
        m_parent.m_transaction->m_vKernelsOutput.push_back(move(m_parent.m_kernel));

        /////
        m_parent.m_transaction->m_Offset = m_parent.m_offset;
        /////
        // 6. Create final transaction and send it to mempool
        m_parent.m_transaction->Sort();

        update_tx_description(TxDescription::InProgress);
        m_parent.m_gateway.register_tx(m_parent.m_txDesc, m_parent.m_transaction);
    }

    void Negotiator::FSMDefinition::inviteSender(const events::TxBill&)
    {

    }

    void Negotiator::FSMDefinition::confirmSender(const events::TxInvitationCompleted&)
    {

    }
}
