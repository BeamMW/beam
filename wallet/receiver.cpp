#include "receiver.h"

namespace beam::wallet
{
    using namespace ECC;
    using namespace std;

    Receiver::FSMDefinition::FSMDefinition(receiver::IGateway &gateway, beam::IKeyChain::Ptr keychain, sender::InvitationData::Ptr initData)
        : m_gateway{ gateway }
        , m_keychain{ keychain }
        , m_txId{ initData->m_txId }
        , m_amount{ initData->m_amount }
        , m_message{ initData->m_message }
        , m_publicSenderBlindingExcess{ initData->m_publicSenderBlindingExcess }
        , m_publicSenderNonce{ initData->m_publicSenderNonce }
        , m_transaction{ make_shared<Transaction>() }
        , m_receiver_coin{keychain->getNextID(), m_amount, Coin::Unconfirmed, initData->m_height}
    {
        m_transaction->m_Offset = ECC::Zero;
        m_transaction->m_vInputs = move(initData->m_inputs);
        m_transaction->m_vOutputs = move(initData->m_outputs);
    }

    void Receiver::FSMDefinition::confirm_tx(const msmf::none&)
    {
        auto confirmationData = make_shared<receiver::ConfirmationData>();
        confirmationData->m_txId = m_txId;

        TxKernel::Ptr kernel = make_unique<TxKernel>();
        kernel->m_Fee = 0;
        kernel->m_HeightMin = 0;
        kernel->m_HeightMax = static_cast<Height>(-1);
        m_kernel = kernel.get();
        m_transaction->m_vKernelsOutput.push_back(move(kernel));

        // 1. Check fee
        // 2. Create receiver_output
        // 3. Choose random blinding factor for receiver_output
        Amount amount = m_amount;
        Output::Ptr output = make_unique<Output>();
        output->m_Coinbase = false;

        Scalar::Native blindingFactor = m_keychain->calcKey(m_receiver_coin);
        output->Create(blindingFactor, amount);
        auto [privateExcess, offset] = split_key(blindingFactor, m_receiver_coin.m_id);

        m_blindingExcess = -privateExcess;
        assert(m_transaction->m_Offset.m_Value == Zero);
        m_transaction->m_Offset = offset;

        m_transaction->m_vOutputs.push_back(move(output));
 
        // 4. Calculate message M
        // 5. Choose random nonce
        Signature::MultiSig msig;
        m_nonce = generateNonce();
        msig.m_Nonce = m_nonce;
        // 6. Make public nonce and blinding factor
        m_publicReceiverBlindingExcess = Context::get().G * m_blindingExcess;
        confirmationData->m_publicReceiverBlindingExcess = m_publicReceiverBlindingExcess;

        Point::Native publicNonce;
        publicNonce = Context::get().G * m_nonce;
        confirmationData->m_publicReceiverNonce = publicNonce;
        // 7. Compute Shnorr challenge e = H(M|K)

        msig.m_NoncePub = m_publicSenderNonce + confirmationData->m_publicReceiverNonce;
        // 8. Compute recepient Shnorr signature
        m_kernel->m_Signature.CoSign(m_receiverSignature, m_message, m_blindingExcess, msig);
        
        confirmationData->m_receiverSignature = m_receiverSignature;

        m_gateway.send_tx_confirmation(confirmationData);
    }

    bool Receiver::FSMDefinition::is_valid_signature(const TxConfirmationCompleted& event)
    {
        auto data = event.data;
        // 1. Verify sender's Schnor signature

		Signature sigPeer;
		sigPeer.m_e = m_kernel->m_Signature.m_e;
		sigPeer.m_k = data->m_senderSignature;
		return sigPeer.IsValidPartial(m_publicSenderNonce, m_publicSenderBlindingExcess);
    }

    bool Receiver::FSMDefinition::is_invalid_signature(const TxConfirmationCompleted& event)
    {
        return !is_valid_signature(event);
    }

    void Receiver::FSMDefinition::register_tx(const TxConfirmationCompleted& event)
    {
        // 2. Calculate final signature
        Scalar::Native senderSignature;
        senderSignature = event.data->m_senderSignature;
        Scalar::Native finialSignature = senderSignature + m_receiverSignature;

        // 3. Calculate public key for excess
        Point::Native x = m_publicReceiverBlindingExcess;
        x += m_publicSenderBlindingExcess;
        // 4. Verify excess value in final transaction
        // 5. Create transaction kernel
        m_kernel->m_Excess = x;
        m_kernel->m_Signature.m_k = finialSignature;

        // 6. Create final transaction and send it to mempool
        m_transaction->Sort();
        beam::TxBase::Context ctx;
        assert(m_transaction->IsValid(ctx));

        m_gateway.register_tx(m_txId, m_transaction);
    }

    void Receiver::FSMDefinition::rollback_tx(const TxFailed& event)
    {
        LOG_DEBUG() << "[Receiver] rollback_tx";
    }

    void Receiver::FSMDefinition::cancel_tx(const TxConfirmationCompleted& )
    {
        LOG_DEBUG() << "[Receiver] cancel_tx";
    }

    void Receiver::FSMDefinition::complete_tx(const TxRegistrationCompleted& )
    {
        LOG_DEBUG() << "[Receiver] complete tx";

		m_gateway.send_tx_registered(make_unique<Uuid>(m_txId));

		// TODO: add unconfirmed coins (m_receiver_coin)
    }
}