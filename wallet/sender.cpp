#include "sender.h"
#include "wallet/wallet_serialization.h"

namespace beam::wallet
{
    using namespace ECC;
    using namespace std;

    void Sender::FSMDefinition::init_tx(const msmf::none&)
    {
        LOG_INFO() << "Sending " << PrintableAmount(m_txDesc.m_amount);
        // 1. Create transaction Uuid
        InviteReceiver invitationData;
        invitationData.m_txId = m_txDesc.m_txId;
        Height currentHeight = m_keychain->getCurrentHeight();
        invitationData.m_height = currentHeight;

        auto coins = m_keychain->getCoins(m_txDesc.m_amount); // need to lock 
        if (coins.empty())
        {
            LOG_ERROR() << "You only have " << PrintableAmount(get_total());
            throw runtime_error("no money");
        }
        invitationData.m_amount = m_txDesc.m_amount;
        
        // create kernel
        m_kernel = make_unique<TxKernel>();
        m_kernel->m_Fee = 0;
        m_kernel->m_Height.m_Min = currentHeight;
        m_kernel->m_Height.m_Max = MaxHeight;

        m_kernel->get_HashForSigning(invitationData.m_message);
        
        // 2. Set lock_height for output (current chain height)
        // 3. Select inputs using desired selection strategy
        {
            m_blindingExcess = Zero;
            for (const auto& coin: coins)
            {
                assert(coin.m_status == Coin::Locked);
                Input::Ptr input = make_unique<Input>();

                Scalar::Native key{ m_keychain->calcKey(coin) };
                input->m_Commitment = Commitment(key, coin.m_amount);

                invitationData.m_inputs.push_back(move(input));
                m_blindingExcess += key;
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

            change -= m_txDesc.m_amount;
            if (change > 0)
            {
                auto changeCoin = beam::Coin(change, Coin::Unconfirmed, currentHeight);
                m_keychain->store(changeCoin);
                Output::Ptr output = make_unique<Output>();
                output->m_Coinbase = false;
                Scalar::Native blindingFactor = m_keychain->calcKey(changeCoin);
                output->Create(blindingFactor, change);

                blindingFactor = -blindingFactor;
                blindingFactor = -blindingFactor;
                m_blindingExcess += blindingFactor;

                invitationData.m_outputs.push_back(move(output));
            }
        }
        // 6. calculate tx_weight
        // 7. calculate fee
        // 8. Calculate total blinding excess for all inputs and outputs xS
        // 9. Select random nonce kS
        Signature::MultiSig msig;
		msig.GenerateNonce(invitationData.m_message, m_blindingExcess);
        
        // 10. Multiply xS and kS by generator G to create public curve points xSG and kSG
        Point::Native publicBlindingExcess = Context::get().G * m_blindingExcess;
        invitationData.m_publicSenderBlindingExcess = publicBlindingExcess;

        invitationData.m_publicSenderNonce = Context::get().G * msig.m_Nonce;

        update_tx_description(TxDescription::InProgress);
        m_gateway.send_tx_invitation(m_txDesc, move(invitationData));
    }

    bool Sender::FSMDefinition::is_valid_signature(const TxInitCompleted& event)
    {
        auto& data = event.data;
        // 4. Compute Sender Schnorr signature
        // 1. Calculate message m
        Signature::MultiSig msig;

        Hash::Value message;
        m_kernel->get_HashForSigning(message);

		msig.GenerateNonce(message, m_blindingExcess);
        Point::Native publicNonce = Context::get().G * msig.m_Nonce;
		msig.m_NoncePub = publicNonce + data.m_publicReceiverNonce;

        // temp signature to calc challenge
        Scalar::Native senderSignature;
		m_kernel->m_Signature.CoSign(senderSignature, message, m_blindingExcess, msig);
        
        // 3. Verify recepients Schnorr signature 
		Signature sigPeer;
		sigPeer.m_e = m_kernel->m_Signature.m_e;
		sigPeer.m_k = data.m_receiverSignature;
		return sigPeer.IsValidPartial(data.m_publicReceiverNonce, data.m_publicReceiverBlindingExcess);
    }

    bool Sender::FSMDefinition::is_invalid_signature(const TxInitCompleted& event)
    {
        return !is_valid_signature(event);
    }

    void Sender::FSMDefinition::confirm_tx(const TxInitCompleted& event)
    {
        auto& data = event.data;
        // 4. Compute Sender Schnorr signature

        ConfirmTransaction confirmationData;
        confirmationData.m_txId = m_txDesc.m_txId;
		
        Hash::Value message;
		m_kernel->get_HashForSigning(message);
		
        Signature::MultiSig msig;
        msig.GenerateNonce(message, m_blindingExcess);
        Point::Native publicNonce = Context::get().G * msig.m_Nonce;
        msig.m_NoncePub = publicNonce + data.m_publicReceiverNonce;

        Scalar::Native senderSignature;
        m_kernel->m_Signature.CoSign(senderSignature, message, m_blindingExcess, msig);

        confirmationData.m_senderSignature = senderSignature;
        update_tx_description(TxDescription::InProgress);
        m_gateway.send_tx_confirmation(m_txDesc, move(confirmationData));
    }

    void Sender::FSMDefinition::rollback_tx(const TxFailed& )
    {
        update_tx_description(TxDescription::Failed);
        rollback_tx();
    }

    void Sender::FSMDefinition::cancel_tx(const TxInitCompleted& )
    {
        update_tx_description(TxDescription::Cancelled);
        rollback_tx();
    }

	void Sender::FSMDefinition::rollback_tx()
	{
        LOG_DEBUG() << "Transaction failed. Rollback...";
        m_keychain->rollbackTx(m_txDesc.m_txId);
	}

    void Sender::FSMDefinition::complete_tx(const TxConfirmationCompleted&)
    {
        complete_tx();
    }

    void Sender::FSMDefinition::complete_tx()
    {
        LOG_DEBUG() << "Transaction completed";
        update_tx_description(TxDescription::Completed);
    }

    Amount Sender::FSMDefinition::get_total() const
    {
        auto currentHeight = m_keychain->getCurrentHeight();
        Amount total = 0;
        m_keychain->visit([&total, &currentHeight](const Coin& c)->bool
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
        m_txDesc.m_status = s;
        m_txDesc.m_modifyTime = getTimestamp();
        Serializer ser;
        ser & *this;
        ser.swap_buf(m_txDesc.m_fsmState);
        m_keychain->saveTx(m_txDesc);
    }

}
