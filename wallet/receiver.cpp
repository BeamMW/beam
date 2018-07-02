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
        Height currentHeight = m_parent.m_keychain->getCurrentHeight();

        m_parent.createKernel(m_parent.m_txDesc.m_fee, currentHeight);
        // TODO: check fee

        // create receiver output
        m_parent.m_transaction->m_vOutputs.push_back(m_parent.createOutput(m_parent.m_txDesc.m_amount, currentHeight));

        ConfirmInvitation confirmationData;
        confirmationData.m_txId = m_parent.m_txDesc.m_txId;
        confirmationData.m_publicPeerExcess = m_parent.getPublicExcess();
        m_parent.createSignature2(confirmationData.m_peerSignature, confirmationData.m_publicPeerNonce);

        update_tx_description(TxDescription::InProgress);
        m_parent.m_gateway.send_tx_confirmation(m_parent.m_txDesc, move(confirmationData));
    }

    void Negotiator::FSMDefinition::registerTx(const events::TxConfirmationCompleted& event)
    {
        if (!m_parent.isValidSignature(event.data.m_senderSignature))
        {
            Negotiator::Fsm &fsm = static_cast<Negotiator::Fsm&>(*this);
            fsm.process_event(events::TxFailed{ true });
            return;
        }

        // Calculate final signature
        Scalar::Native senderSignature;
        senderSignature = event.data.m_senderSignature;
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
        if (!m_parent.m_transaction->IsValid(TxBase::Context{}))
        {
            Negotiator::Fsm &fsm = static_cast<Negotiator::Fsm&>(*this);
            fsm.process_event(events::TxFailed{ true });
            return;
        }

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
