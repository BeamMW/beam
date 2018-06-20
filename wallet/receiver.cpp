#include "receiver.h"
#include "../core/block_crypt.h"
#include "wallet/wallet_serialization.h"

namespace beam::wallet
{
    using namespace ECC;
    using namespace std;

    Receiver::FSMDefinition::FSMDefinition(receiver::IGateway &gateway, beam::IKeyChain::Ptr keychain, TxDescription& txDesc, InviteReceiver& initData)
        : FSMDefinitionBase{txDesc}
        , m_gateway{ gateway }
        , m_keychain{ keychain }
        , m_message{initData.m_message}
        , m_publicSenderBlindingExcess{ initData.m_publicSenderBlindingExcess }
        , m_publicSenderNonce{ initData.m_publicSenderNonce }
        , m_transaction{ make_shared<Transaction>() }
        , m_receiver_coin{ initData.m_amount, Coin::Unconfirmed, initData.m_height}
        , m_height{ initData.m_height }
    {
        m_transaction->m_Offset = ECC::Zero;
        m_transaction->m_vInputs = move(initData.m_inputs);
        m_transaction->m_vOutputs = move(initData.m_outputs);
        update_tx_description(TxDescription::Pending);
    }

    void Receiver::FSMDefinition::confirm_tx(const msmf::none&)
    {
        LOG_INFO() << "Receiving " << PrintableAmount(m_txDesc.m_amount);
        ConfirmInvitation confirmationData;
        confirmationData.m_txId = m_txDesc.m_txId;

        m_kernel = make_unique<TxKernel>();
        m_kernel->m_Fee = 0;
        m_kernel->m_Height.m_Min = m_height;
        m_kernel->m_Height.m_Max = MaxHeight;

        // 1. Check fee
        // 2. Create receiver_output
        // 3. Choose random blinding factor for receiver_output
        Amount amount = m_txDesc.m_amount;
        Output::Ptr output = make_unique<Output>();
        output->m_Coinbase = false;
        m_keychain->store(m_receiver_coin);
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
		msig.GenerateNonce(m_message, m_blindingExcess);
        // 6. Make public nonce and blinding factor
        m_publicReceiverBlindingExcess = Context::get().G * m_blindingExcess;
        confirmationData.m_publicReceiverBlindingExcess = m_publicReceiverBlindingExcess;

        Point::Native publicNonce;
        publicNonce = Context::get().G * msig.m_Nonce;
        confirmationData.m_publicReceiverNonce = publicNonce;

        // 7. Compute Shnorr challenge e = H(M|K)

        msig.m_NoncePub = m_publicSenderNonce + confirmationData.m_publicReceiverNonce;
        // 8. Compute recepient Shnorr signature
        m_kernel->m_Signature.CoSign(m_receiverSignature, m_message, m_blindingExcess, msig);
        
        confirmationData.m_receiverSignature = m_receiverSignature;

        update_tx_description(TxDescription::InProgress);
        m_gateway.send_tx_confirmation(m_txDesc, confirmationData);
    }

    bool Receiver::FSMDefinition::is_valid_signature(const TxConfirmationCompleted& event)
    {
        auto& data = event.data;
        // 1. Verify sender's Schnor signature

		Signature sigPeer;
		sigPeer.m_e = m_kernel->m_Signature.m_e;
		sigPeer.m_k = data.m_senderSignature;
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
        senderSignature = event.data.m_senderSignature;
        Scalar::Native finialSignature = senderSignature + m_receiverSignature;

        // 3. Calculate public key for excess
        Point::Native x = m_publicReceiverBlindingExcess;
        x += m_publicSenderBlindingExcess;
        // 4. Verify excess value in final transaction
        // 5. Create transaction kernel
        m_kernel->m_Excess = x;
        m_kernel->m_Signature.m_k = finialSignature;

        m_transaction->m_vKernelsOutput.push_back(move(m_kernel));

        // 6. Create final transaction and send it to mempool
        m_transaction->Sort();
        beam::TxBase::Context ctx;
        assert(m_transaction->IsValid(ctx));
        update_tx_description(TxDescription::InProgress);
        m_gateway.register_tx(m_txDesc, m_transaction);
    }

    void Receiver::FSMDefinition::rollback_tx(const TxFailed& event)
    {
        LOG_DEBUG() << "Transaction failed. Rollback...";
        LOG_VERBOSE() << "[Receiver] rollback_tx";
        update_tx_description(TxDescription::Failed);
        rollback_tx();
    }

    void Receiver::FSMDefinition::cancel_tx(const TxConfirmationCompleted& )
    {
        LOG_DEBUG() << "Transaction failed. Rollback...";
        LOG_VERBOSE() << "[Receiver] cancel_tx";
        update_tx_description(TxDescription::Cancelled);
        rollback_tx();
    }

    void Receiver::FSMDefinition::complete_tx(const TxRegistrationCompleted& )
    {
        LOG_VERBOSE() << "[Receiver] complete tx";
        LOG_INFO() << "Transaction completed and sent to node";
        update_tx_description(TxDescription::Completed);
		m_gateway.send_tx_registered(m_txDesc);
    }

    void Receiver::FSMDefinition::rollback_tx()
    {
        m_gateway.send_tx_failed(m_txDesc);
        m_keychain->remove(m_receiver_coin);
    }

    void Receiver::FSMDefinition::update_tx_description(TxDescription::Status s)
    {
        m_txDesc.m_status = s;
        Serializer ser;
        ser & *this;
        ser.swap_buf(m_txDesc.m_fsmState);
        m_keychain->saveTx(m_txDesc);
    }
}