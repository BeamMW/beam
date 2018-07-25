#include "negotiator.h"
#include "core/block_crypt.h"
#include "wallet/wallet_serialization.h"

namespace beam::wallet
{
    using namespace ECC;
    using namespace std;

    Negotiator::Negotiator(INegotiatorGateway& gateway
                         , beam::IKeyChain::Ptr keychain
                         , const TxDescription& txDesc)
        : m_gateway{ gateway }
        , m_keychain{ keychain }
        , m_txDesc{ txDesc }
        , m_fsm{ std::ref(*this) }
    {
        assert(keychain);
    }

    Negotiator::FSMDefinition::FSMDefinition(Negotiator& parent)
        : m_parent{ parent }
    {
        update_tx_description(TxDescription::Pending);
        m_blindingExcess = ECC::Zero;
        if (!m_parent.m_txDesc.m_fsmState.empty())
        {
            Deserializer d;
            d.reset(&m_parent.m_txDesc.m_fsmState[0], m_parent.m_txDesc.m_fsmState.size());
            d & *this;
        }
    }

    void Negotiator::FSMDefinition::invitePeer(const events::TxInitiated&)
    {
        bool sender = m_parent.m_txDesc.m_sender;
        LOG_INFO() << ( sender ? "Sending " : "Receiving ") << PrintableAmount(m_parent.m_txDesc.m_amount) << " (fee: " << PrintableAmount(m_parent.m_txDesc.m_fee) << ")";
        
        Height currentHeight = m_parent.m_keychain->getCurrentHeight();

        createKernel(m_parent.m_txDesc.m_fee, currentHeight);
        m_parent.m_txDesc.m_minHeight = currentHeight;

        if (sender)
        {
            if (!prepareSenderUtxos(currentHeight))
            {
                Negotiator::Fsm &fsm = static_cast<Negotiator::Fsm&>(*this);
                fsm.process_event(events::TxFailed{});
                return;
            }
        }
        else
        {
            createOutputUtxo(m_parent.m_txDesc.m_amount, currentHeight);
        }

        update_tx_description(TxDescription::InProgress);
    }

    void Negotiator::FSMDefinition::sendInvite() const
    {
        bool sender = m_parent.m_txDesc.m_sender;
        Height currentHeight = m_parent.m_txDesc.m_minHeight;
        const TxID& txID = m_parent.m_txDesc.m_txId;

        Invite inviteMsg;
        inviteMsg.m_txId = txID;
        inviteMsg.m_amount = m_parent.m_txDesc.m_amount;
        inviteMsg.m_fee = m_parent.m_txDesc.m_fee;
        inviteMsg.m_height = currentHeight;
        inviteMsg.m_send = sender;
        inviteMsg.m_inputs = getTxInputs(txID);
        inviteMsg.m_outputs = getTxOutputs(txID);
        inviteMsg.m_publicPeerExcess = getPublicExcess();
        inviteMsg.m_publicPeerNonce = getPublicNonce();
        inviteMsg.m_offset = m_offset;

        m_parent.m_gateway.send_tx_invitation(m_parent.m_txDesc, move(inviteMsg));
    }

	void Negotiator::FSMDefinition::confirmPeer(const events::TxInvitationCompleted& event)
	{
        auto& msg = event.data;

        if (!isValidSignature(msg.m_peerSignature, msg.m_publicPeerNonce, msg.m_publicPeerExcess)
         || !m_publicPeerExcess.Import(msg.m_publicPeerExcess)
         || !m_publicPeerNonce.Import(msg.m_publicPeerNonce))
        {
            Negotiator::Fsm &fsm = static_cast<Negotiator::Fsm&>(*this);
            fsm.process_event(events::TxFailed{ true });
            return;
        }
        m_peerSignature = msg.m_peerSignature;
        update_tx_description(TxDescription::InProgress);
	}

    void Negotiator::FSMDefinition::sendConfirmTransaction() const
    {
        ConfirmTransaction confirmMsg;
        confirmMsg.m_txId = m_parent.m_txDesc.m_txId;
        confirmMsg.m_peerSignature = createSignature();

        m_parent.m_gateway.send_tx_confirmation(m_parent.m_txDesc, move(confirmMsg));
    }

    void Negotiator::FSMDefinition::confirmInvitation(const events::TxInvited&)
    {
        update_tx_description(TxDescription::Pending);
        bool sender = m_parent.m_txDesc.m_sender;
        LOG_INFO() << (sender ? "Sending " : "Receiving ") << PrintableAmount(m_parent.m_txDesc.m_amount) << " (fee: " << PrintableAmount(m_parent.m_txDesc.m_fee) << ")";
        Height currentHeight = m_parent.m_keychain->getCurrentHeight();

        createKernel(m_parent.m_txDesc.m_fee, m_parent.m_txDesc.m_minHeight);
        
        if (sender)
        {
            if (!prepareSenderUtxos(currentHeight))
            {
                Negotiator::Fsm &fsm = static_cast<Negotiator::Fsm&>(*this);
                fsm.process_event(events::TxFailed{true});
                return;
            }
        }
        else
        {
            createOutputUtxo(m_parent.m_txDesc.m_amount, currentHeight);
        }

        update_tx_description(TxDescription::InProgress);
    }

    void Negotiator::FSMDefinition::sendConfirmInvitation() const
    {
        ConfirmInvitation confirmMsg;
        confirmMsg.m_txId = m_parent.m_txDesc.m_txId;
        confirmMsg.m_publicPeerExcess = getPublicExcess();
        createSignature2(confirmMsg.m_peerSignature, confirmMsg.m_publicPeerNonce, NoLeak<Scalar>().V);

        m_parent.m_gateway.send_tx_confirmation(m_parent.m_txDesc, move(confirmMsg));
    }

	void Negotiator::FSMDefinition::registerTx(const events::TxConfirmationCompleted& event)
	{
		if (!registerTxInternal(event))
		{
			Negotiator::Fsm &fsm = static_cast<Negotiator::Fsm&>(*this);
			fsm.process_event(events::TxFailed{ true });
            return;
		}
        update_tx_description(TxDescription::InProgress);
	}

	bool Negotiator::FSMDefinition::registerTxInternal(const events::TxConfirmationCompleted& event)
	{
		if (!isValidSignature(event.data.m_peerSignature))
			return false;

        // Calculate final signature
        Scalar::Native senderSignature;
        senderSignature = event.data.m_peerSignature;
        Scalar::Native receiverSignature = createSignature();
        Scalar::Native finialSignature = senderSignature + receiverSignature;

        // Calculate public key for excess
		Point::Native x;
		if (!x.Import(getPublicExcess()))
			return false;
        x += m_publicPeerExcess;

        // Create transaction kernel and transaction
        m_kernel->m_Excess = x;
        m_kernel->m_Signature.m_k = finialSignature;
        m_transaction->m_vKernelsOutput.push_back(move(m_kernel));
        m_transaction->m_Offset = m_offset;

        {
            auto inputs = getTxInputs(m_parent.m_txDesc.m_txId);
            move(inputs.begin(), inputs.end(), back_inserter(m_transaction->m_vInputs));

            auto outputs = getTxOutputs(m_parent.m_txDesc.m_txId);
            move(outputs.begin(), outputs.end(), back_inserter(m_transaction->m_vOutputs));
        }
        
        m_transaction->Sort();

        // Verify final transaction
        TxBase::Context ctx;
        return m_transaction->IsValid(ctx);
    }

    void Negotiator::FSMDefinition::sendNewTransaction() const
    {
        m_parent.m_gateway.register_tx(m_parent.m_txDesc, m_transaction);
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

    bool Negotiator::FSMDefinition::prepareSenderUtxos(const Height& currentHeight)
    {
        Amount amountWithFee = m_parent.m_txDesc.m_amount + m_parent.m_txDesc.m_fee;
        auto coins = m_parent.m_keychain->selectCoins(amountWithFee);
        if (coins.empty())
        {
            LOG_ERROR() << "You only have " << PrintableAmount(get_total());
            return false;
        }
        for (auto& coin : coins)
        {
            Scalar::Native blindingFactor = m_parent.m_keychain->calcKey(coin);
            m_blindingExcess += blindingFactor;
            coin.m_spentTxId = m_parent.m_txDesc.m_txId;
        }
        m_parent.m_keychain->update(coins);
        // calculate change amount and create corresponding output if needed
        Amount change = 0;
        for (const auto &coin : coins)
        {
            change += coin.m_amount;
        }
        change -= amountWithFee;
        if (change > 0)
        {
            createOutputUtxo(change, currentHeight);
            m_parent.m_txDesc.m_change = change;
        }
        return true;
    }

    void Negotiator::FSMDefinition::createKernel(Amount fee, Height minHeight)
    {
        m_kernel = make_unique<TxKernel>();
        m_kernel->m_Fee = fee;
        m_kernel->m_Height.m_Min = minHeight;
        m_kernel->m_Height.m_Max = MaxHeight;
    }

    void Negotiator::FSMDefinition::createOutputUtxo(Amount amount, Height height)
    {
        Coin newUtxo{ amount, Coin::Unconfirmed, height };
        newUtxo.m_createTxId = m_parent.m_txDesc.m_txId;
        m_parent.m_keychain->store(newUtxo);

        Scalar::Native blindingFactor = m_parent.m_keychain->calcKey(newUtxo);
        auto[privateExcess, offset] = splitKey(blindingFactor, newUtxo.m_id);

        blindingFactor = -privateExcess;
        m_blindingExcess += blindingFactor;
        m_offset += offset;
    }

	bool Negotiator::ProcessInvitation(Invite& inviteMsg)
	{
		if (!m_fsm.m_publicPeerExcess.Import(inviteMsg.m_publicPeerExcess) ||
			!m_fsm.m_publicPeerNonce.Import(inviteMsg.m_publicPeerNonce))
			return false;

        m_fsm.m_offset = inviteMsg.m_offset;
        m_fsm.m_transaction = std::make_shared<Transaction>();
        m_fsm.m_transaction->m_Offset = ECC::Zero;
        m_fsm.m_transaction->m_vInputs = move(inviteMsg.m_inputs);
        m_fsm.m_transaction->m_vOutputs = move(inviteMsg.m_outputs);

		return true;
	}

    Scalar Negotiator::FSMDefinition::createSignature() const
    {
        NoLeak<Point> publicNonce;
        Scalar partialSignature;
        NoLeak<Scalar> challenge;
        createSignature2(partialSignature, publicNonce.V, challenge.V);
        return partialSignature;
    }

    Scalar Negotiator::FSMDefinition::createSignature()
    {
        Point publicNonce;
        Scalar partialSignature;
        createSignature2(partialSignature, publicNonce, m_kernel->m_Signature.m_e);
        return partialSignature;
    }

    void Negotiator::FSMDefinition::createSignature2(Scalar& signature, Point& publicNonce, Scalar& challenge) const
    {
        Hash::Value message;
        m_kernel->get_HashForSigning(message);

        Signature::MultiSig msig;
        msig.GenerateNonce(message, m_blindingExcess);

		Point::Native pt = Context::get().G * msig.m_Nonce;
		publicNonce = pt;
        msig.m_NoncePub = m_publicPeerNonce + pt;

        Scalar::Native partialSignature;
        Signature sig;
        sig.CoSign(partialSignature, message, m_blindingExcess, msig);
        challenge = sig.m_e;
        signature = partialSignature;
    }

    Point Negotiator::FSMDefinition::getPublicExcess() const
    {
        return Point(Context::get().G * m_blindingExcess);
    }

    Point Negotiator::FSMDefinition::getPublicNonce() const
    {
        Hash::Value message;
        m_kernel->get_HashForSigning(message);

        Signature::MultiSig msig;
        msig.GenerateNonce(message, m_blindingExcess);

        return Point(Context::get().G * msig.m_Nonce);
    }

    bool Negotiator::FSMDefinition::isValidSignature(const Scalar& peerSignature) const
    {
        return isValidSignature(peerSignature, m_publicPeerNonce, m_publicPeerExcess);
    }

    bool Negotiator::FSMDefinition::isValidSignature(const Scalar& peerSignature, const Point& publicPeerNonce, const Point& publicPeerExcess) const
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

		Point::Native pkPeer, xcPeer;
		if (!pkPeer.Import(publicPeerNonce) ||
			!xcPeer.Import(publicPeerExcess))
			return false;

        msig.m_NoncePub = publicNonce + pkPeer;

        // temp signature to calc challenge
        Scalar::Native mySig;
        Signature peerSig;
        peerSig.CoSign(mySig, message, m_blindingExcess, msig);
        peerSig.m_k = peerSignature;
        return peerSig.IsValidPartial(pkPeer, xcPeer);
    }

    vector<Input::Ptr> Negotiator::FSMDefinition::getTxInputs(const TxID& txID) const
    {
        vector<Input::Ptr> inputs;
        m_parent.m_keychain->visit([this, &txID, &inputs](const Coin& c)->bool
        {
            if (c.m_spentTxId == txID && c.m_status == Coin::Locked)
            {
                Input::Ptr input = make_unique<Input>();

                Scalar::Native blindingFactor = m_parent.m_keychain->calcKey(c);
                input->m_Commitment = Commitment(blindingFactor, c.m_amount);

                inputs.push_back(move(input));
            }
            return true;
        });
        return inputs;
    }

    vector<Output::Ptr> Negotiator::FSMDefinition::getTxOutputs(const TxID& txID) const
    {
        vector<Output::Ptr> outputs;
        m_parent.m_keychain->visit([this, &txID, &outputs](const Coin& c)->bool
        {
            if (c.m_createTxId == txID && c.m_status == Coin::Unconfirmed)
            {
                Output::Ptr output = make_unique<Output>();
                output->m_Coinbase = false;

                Scalar::Native blindingFactor = m_parent.m_keychain->calcKey(c);
                output->Create(blindingFactor, c.m_amount);

                outputs.push_back(move(output));
            }
            return true;
        });
        return outputs;
    }
}
