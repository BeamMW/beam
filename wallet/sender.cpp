#include "sender.h"

namespace beam::wallet
{
    using namespace ECC;
    using namespace std;

    void Sender::FSMDefinition::initTx(const msmf::none&)
    {
        // 1. Create transaction Uuid
        auto invitationData = make_shared<sender::InvitationData>();
        invitationData->m_txId = m_txId;

        m_coins = m_keychain->getCoins(m_amount); // need to lock 
        invitationData->m_amount = m_amount;
        m_kernel.m_Fee = 0;
        m_kernel.m_HeightMin = 0;
        m_kernel.m_HeightMax = static_cast<Height>(-1);
        m_kernel.get_HashForSigning(invitationData->m_message);
        
        // 2. Set lock_height for output (current chain height)
        // 3. Select inputs using desired selection strategy
        {
            m_blindingExcess = Zero;
            for (const auto& coin: m_coins)
            {
                assert(coin.m_status == Coin::Locked);
                Input::Ptr input = make_unique<Input>();

                Scalar::Native key{ coin.m_key };
                input->m_Commitment = Commitment(key, coin.m_amount);

                invitationData->m_inputs.push_back(move(input));
                m_blindingExcess += key;
            }
        }
        // 4. Create change_output
        // 5. Select blinding factor for change_output
        {
            Amount change = 0;
            for (const auto &coin : m_coins)
            {
                change += coin.m_amount;
            }

            change -= m_amount;

            Output::Ptr output = make_unique<Output>();
            output->m_Coinbase = false;

            Scalar::Native blindingFactor = m_keychain->getNextKey();
			output->Create(blindingFactor, change, true);
            
            m_changeOutput = Coin(blindingFactor, change, Coin::Unconfirmed, m_height, false);
            m_keychain->store(*m_changeOutput);

            blindingFactor = -blindingFactor;
            m_blindingExcess += blindingFactor;

            invitationData->m_outputs.push_back(move(output));
        }
        // 6. calculate tx_weight
        // 7. calculate fee
        // 8. Calculate total blinding excess for all inputs and outputs xS
        // 9. Select random nonce kS
        Signature::MultiSig msig;
        m_nonce = generateNonce();

        msig.m_Nonce = m_nonce;
        // 10. Multiply xS and kS by generator G to create public curve points xSG and kSG
        m_publicBlindingExcess = Context::get().G * m_blindingExcess;
        invitationData->m_publicSenderBlindingExcess = m_publicBlindingExcess;
            
        m_publicNonce = Context::get().G * m_nonce;
        invitationData->m_publicSenderNonce = m_publicNonce;
        // an attempt to implement "stingy" transaction
        m_gateway.send_tx_invitation(invitationData);
    }

    bool Sender::FSMDefinition::is_valid_signature(const TxInitCompleted& event)
    {
        auto data = event.data;
        // 4. Compute Sender Schnorr signature
        // 1. Calculate message m
        Signature::MultiSig msig;
        msig.m_Nonce = m_nonce;
        msig.m_NoncePub = m_publicNonce + data->m_publicReceiverNonce;
        Hash::Value message;
        m_kernel.get_HashForSigning(message);
        m_kernel.m_Signature.CoSign(m_senderSignature, message, m_blindingExcess, msig);
        

        // 2. Compute Schnorr challenge e
        Point::Native k;
        k = m_publicNonce + data->m_publicReceiverNonce;
        Scalar::Native e = m_kernel.m_Signature.m_e;
 
        // 3. Verify recepients Schnorr signature 
        Point::Native s, s2;
        Scalar::Native ne;
        Point::Native publicReceiverBlindingExcess;
        publicReceiverBlindingExcess = data->m_publicReceiverBlindingExcess;
        ne = -e;
        s = data->m_publicReceiverNonce;
        s += publicReceiverBlindingExcess * ne;

        s2 = Context::get().G * data->m_receiverSignature;
        Point p(s), p2(s2);

        return (p == p2);
    }

    bool Sender::FSMDefinition::is_invalid_signature(const TxInitCompleted& event)
    {
        return !is_valid_signature(event);
    }

    void Sender::FSMDefinition::confirm_tx(const TxInitCompleted& event)
    {
        auto data = event.data;
        // 4. Compute Sender Schnorr signature
        auto confirmationData = make_shared<sender::ConfirmationData>();
        confirmationData->m_txId = m_txId;
        Signature::MultiSig msig;
        msig.m_Nonce = m_nonce;
        msig.m_NoncePub = m_publicNonce + data->m_publicReceiverNonce;
        Hash::Value message;
        m_kernel.get_HashForSigning(message);
        Scalar::Native senderSignature;
        m_kernel.m_Signature.CoSign(senderSignature, message, m_blindingExcess, msig);
        confirmationData->m_senderSignature = senderSignature;
        m_gateway.send_tx_confirmation(confirmationData);
    }

    void Sender::FSMDefinition::rollback_tx(const TxFailed& )
    {
    }

    void Sender::FSMDefinition::cancel_tx(const TxInitCompleted& )
    {
        
    }

    void Sender::FSMDefinition::confirm_change_output(const TxConfirmationCompleted&)
    {
        m_gateway.send_output_confirmation();
    }

    void Sender::FSMDefinition::complete_tx(const TxOutputConfirmCompleted&)
    {
        cout << "Sender::complete_tx\n";
        for (auto& c : m_coins)
        {
            c.m_status = Coin::Spent;
        }
        if (m_changeOutput)
        {
            m_changeOutput->m_status = Coin::Unspent;
            m_coins.push_back(*m_changeOutput);
        }
        m_keychain->update(m_coins);
    }
}
