// Copyright 2018 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "negotiator.h"
#include "core/block_crypt.h"
#include "wallet/wallet_serialization.h"

namespace beam { namespace wallet
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

        try
        {
            if (!m_txDesc.m_fsmState.empty())
            {
                Deserializer d;
                d.reset(&m_txDesc.m_fsmState[0], m_txDesc.m_fsmState.size());
                d & *this;
            }
        }
        catch (...)
        {
            m_txDesc.m_fsmState.clear();
        }
    }

    void Negotiator::saveState()
    {
        if (m_txDesc.canResume())
        {
            Serializer ser;
            ser & *this;
            ser.swap_buf(m_txDesc.m_fsmState);
        }
        else
        {
            m_txDesc.m_fsmState.clear();
        }
        m_keychain->saveTx(m_txDesc);
    }

    TxKernel* Negotiator::getKernel() const
    {
        if (m_txDesc.m_status == TxDescription::Registered)
        {
            // TODO: what should we do in case when we have more than one kernel
            if (m_fsm.m_kernel)
            {
                return m_fsm.m_kernel.get();
            }
            else if (m_fsm.m_transaction && !m_fsm.m_transaction->m_vKernelsOutput.empty())
            {
                return m_fsm.m_transaction->m_vKernelsOutput[0].get();
            }
        }
        return nullptr;
    }

    const TxID& Negotiator::getTxID() const
    {
        return m_txDesc.m_txId;
    }

    Negotiator::FSMDefinition::FSMDefinition(Negotiator& parent)
        : m_parent{ parent }
    {
        m_blindingExcess = Zero;
    }

    void Negotiator::FSMDefinition::invitePeer(const events::TxInitiated&)
    {
        bool sender = m_parent.m_txDesc.m_sender;
        LOG_INFO() << m_parent.m_txDesc.m_txId << (sender ? " Sending " : " Receiving ") << PrintableAmount(m_parent.m_txDesc.m_amount) << " (fee: " << PrintableAmount(m_parent.m_txDesc.m_fee) << ")";


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

        if (sender)
        {
            auto address = m_parent.m_keychain->getAddress(m_parent.m_txDesc.m_peerId);

            if (address.is_initialized() && address->m_own)
            {
                sendSelfTx();
                return;
            }
        }

		AllInOne(Stage::SendInvite);
    }

    void Negotiator::FSMDefinition::sendSelfTx()
    {
		// Create output UTXOs for main amount
        createOutputUtxo(m_parent.m_txDesc.m_amount, m_parent.m_txDesc.m_minHeight);

        // Create empty transaction
        m_transaction = std::make_shared<Transaction>();

		m_publicPeerExcess = Zero;
		ZeroObject(m_sigPeer);

		update_tx_description(TxDescription::InProgress);
		AllInOne(Stage::SendNewTx);
    }

	bool Negotiator::FSMDefinition::AllInOne(Stage::Enum e)
	{
		if (AllInOne2(e))
			return true;

		Negotiator::Fsm &fsm = static_cast<Negotiator::Fsm&>(*this);
		fsm.process_event(events::TxFailed{ true });

		return false;
	}

	bool Negotiator::FSMDefinition::AllInOne2(Stage::Enum e)
	{
		assert(m_kernel);
		m_kernel->m_Excess = Zero; // Kernel hash depends on the Excess (this is the intended behavior). However the Nonce should depend on the kernel, but NOT on the Nonce, since it's not known yet
		// (otherwise we'd have an additional iteration only for this)

		Hash::Value message;
		m_kernel->get_Hash(message);

		Signature::MultiSig msig;
		msig.GenerateNonce(message, m_blindingExcess);


		Point::Native pubExcess = Context::get().G * m_blindingExcess;

		if (Stage::SendInvite == e)
		{
			const TxID& txID = m_parent.m_txDesc.m_txId;

			Invite inviteMsg;
			inviteMsg.m_txId = txID;
			inviteMsg.m_amount = m_parent.m_txDesc.m_amount;
			inviteMsg.m_fee = m_parent.m_txDesc.m_fee;
			inviteMsg.m_height = m_parent.m_txDesc.m_minHeight;
			inviteMsg.m_send = m_parent.m_txDesc.m_sender;
			inviteMsg.m_inputs = getTxInputs(txID);
			inviteMsg.m_outputs = getTxOutputs(txID);
			inviteMsg.m_publicPeerExcess = pubExcess;
			inviteMsg.m_publicPeerNonce = msig.m_NoncePub;
			inviteMsg.m_offset = m_offset;

			m_parent.m_gateway.send_tx_invitation(m_parent.m_txDesc, move(inviteMsg));
			return true;
		}

		// peer Nonce and public excess must have already been received
		Point::Native pubPeerNonce, pubPeerExc;
		if (!pubPeerNonce.Import(m_sigPeer.m_NoncePub) ||
			!pubPeerExc.Import(m_publicPeerExcess))
			return false;

		// create our partial signature
		ECC::Point::Native pubNonceMy = msig.m_NoncePub;
		ECC::Point::Native pubExcMy = pubExcess;

		msig.m_NoncePub += pubPeerNonce;

		pubExcess += pubPeerExc;
		m_kernel->m_Excess = pubExcess;

		m_kernel->get_Hash(message); // update the kernel hash, after the correct Excess was specified

		Scalar::Native kSig;
		msig.SignPartial(kSig, message, m_blindingExcess);

		if (Stage::SendConfirmInvite == e)
		{
			ConfirmInvitation confirmMsg;
			confirmMsg.m_txId = m_parent.m_txDesc.m_txId;
			confirmMsg.m_publicPeerExcess = pubExcMy;
			confirmMsg.m_peerSignature.m_NoncePub = pubNonceMy;
			confirmMsg.m_peerSignature.m_k = kSig;

			m_parent.m_gateway.send_tx_confirmation(m_parent.m_txDesc, move(confirmMsg));
			return true;
		}

		// complete signature
		Signature& sig = m_kernel->m_Signature;
		sig.m_NoncePub = msig.m_NoncePub;

		Scalar::Native kSigTotal = m_sigPeer.m_k;
		kSigTotal += kSig;
		sig.m_k = kSigTotal;

		if (!sig.IsValidPartial(message, msig.m_NoncePub, pubExcess))
			return false;

		if (Stage::SendConfirmTx == e)
		{
			ConfirmTransaction confirmMsg;
			confirmMsg.m_txId = m_parent.m_txDesc.m_txId;
			confirmMsg.m_peerSignature = kSig;

			m_parent.m_gateway.send_tx_confirmation(m_parent.m_txDesc, move(confirmMsg));
			return true;
		}

		sig.m_NoncePub = msig.m_NoncePub; // sig complete
		assert(sig.IsValid(message, pubExcess));

		assert(Stage::SendNewTx == e);


		// Create transaction kernel and transaction
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
		if (!m_transaction->IsValid(ctx))
			return false;

		m_parent.m_gateway.register_tx(m_parent.m_txDesc, m_transaction);
		return true;
	}

    void Negotiator::FSMDefinition::confirmPeer(const events::TxInvitationCompleted& event)
    {
        auto& msg = event.data;

		m_sigPeer = msg.m_peerSignature;
		m_publicPeerExcess = msg.m_publicPeerExcess;

        update_tx_description(TxDescription::InProgress);

		AllInOne(Stage::SendConfirmTx);
    }

    void Negotiator::FSMDefinition::confirmInvitation(const events::TxInvited&)
    {
        update_tx_description(TxDescription::Pending);
        bool sender = m_parent.m_txDesc.m_sender;
        LOG_INFO() << m_parent.m_txDesc.m_txId << (sender ? " Sending " : " Receiving ") << PrintableAmount(m_parent.m_txDesc.m_amount) << " (fee: " << PrintableAmount(m_parent.m_txDesc.m_fee) << ")";
        Height currentHeight = m_parent.m_keychain->getCurrentHeight();

        createKernel(m_parent.m_txDesc.m_fee, m_parent.m_txDesc.m_minHeight);


        if (sender)
        {
            if (!prepareSenderUtxos(currentHeight))
            {
                Negotiator::Fsm &fsm = static_cast<Negotiator::Fsm&>(*this);
                fsm.process_event(events::TxFailed{ true });
                return;
            }
        }
        else
        {
            createOutputUtxo(m_parent.m_txDesc.m_amount, currentHeight);
        }

        LOG_INFO() << m_parent.m_txDesc.m_txId << " Invitation accepted";
        update_tx_description(TxDescription::InProgress);

		AllInOne(Stage::SendConfirmInvite);
    }

    void Negotiator::FSMDefinition::registerTx(const events::TxConfirmationCompleted& event)
    {
		update_tx_description(TxDescription::InProgress);

		m_sigPeer.m_k = event.data.m_peerSignature;
		AllInOne(Stage::SendNewTx);
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

    void Negotiator::FSMDefinition::cancelTx(const events::TxCanceled&)
    {
        if (m_parent.m_txDesc.m_status == TxDescription::Pending)
        {
            m_parent.m_keychain->deleteTx(m_parent.m_txDesc.m_txId);
        }
        else
        {
            update_tx_description(TxDescription::Cancelled);
            rollbackTx();
            m_parent.m_gateway.send_tx_failed(m_parent.m_txDesc);
        }
    }

    void Negotiator::FSMDefinition::rollbackTx()
    {
        LOG_INFO() << m_parent.m_txDesc.m_txId << " Transaction failed. Rollback...";
        m_parent.m_keychain->rollbackTx(m_parent.m_txDesc.m_txId);
    }

    void Negotiator::FSMDefinition::confirmOutputs(const events::TxRegistrationCompleted& event)
    {
        m_parent.m_gateway.send_tx_registered(m_parent.m_txDesc);
        confirmOutputs2(event);
    }

    void Negotiator::FSMDefinition::confirmOutputs2(const events::TxRegistrationCompleted&)
    {
        LOG_INFO() << m_parent.m_txDesc.m_txId << " Transaction registered";
        update_tx_description(TxDescription::Registered);

        auto coins = m_parent.m_keychain->getCoinsCreatedByTx(m_parent.m_txDesc.m_txId);

        for (auto& coin : coins)
        {
            coin.m_status = Coin::Unconfirmed;
        }
        m_parent.m_keychain->update(coins);

        m_parent.m_gateway.confirm_outputs(m_parent.m_txDesc);
    }

    void Negotiator::FSMDefinition::completeTx(const events::TxOutputsConfirmed&)
    {
        completeTx();
    }

    void Negotiator::FSMDefinition::completeTx()
    {
        LOG_INFO() << m_parent.m_txDesc.m_txId << " Transaction completed";
        update_tx_description(TxDescription::Completed);
    }

    void Negotiator::FSMDefinition::update_tx_description(TxDescription::Status s)
    {
        m_parent.m_txDesc.m_status = s;
        m_parent.m_txDesc.m_modifyTime = getTimestamp();
    }

    bool Negotiator::FSMDefinition::prepareSenderUtxos(const Height& currentHeight)
    {
        Amount amountWithFee = m_parent.m_txDesc.m_amount + m_parent.m_txDesc.m_fee;
        auto coins = m_parent.m_keychain->selectCoins(amountWithFee);
        if (coins.empty())
        {
            LOG_ERROR() << "You only have " << PrintableAmount(getAvailable(m_parent.m_keychain));
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
        Coin newUtxo{ amount, Coin::Draft, height };
        newUtxo.m_createTxId = m_parent.m_txDesc.m_txId;
        m_parent.m_keychain->store(newUtxo);

        Scalar::Native blindingFactor = m_parent.m_keychain->calcKey(newUtxo);
        auto[privateExcess, offset] = splitKey(blindingFactor, newUtxo.m_id);

        blindingFactor = -privateExcess;
        m_blindingExcess += blindingFactor;
        m_offset += offset;
    }

    void Negotiator::ProcessInvitation(Invite& inviteMsg)
    {
		m_fsm.m_publicPeerExcess = inviteMsg.m_publicPeerExcess;
		m_fsm.m_sigPeer.m_NoncePub = inviteMsg.m_publicPeerNonce;
        m_fsm.m_offset = inviteMsg.m_offset;
        m_fsm.m_transaction = std::make_shared<Transaction>();
        m_fsm.m_transaction->m_Offset = Zero;
        m_fsm.m_transaction->m_vInputs = move(inviteMsg.m_inputs);
        m_fsm.m_transaction->m_vOutputs = move(inviteMsg.m_outputs);
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
            if (c.m_createTxId == txID && c.m_status == Coin::Draft)
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
}}
