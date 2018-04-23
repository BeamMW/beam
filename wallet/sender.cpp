#include "sender.h"

namespace
{
    void GenerateRandom(void* p, uint32_t n)
    {
        for (uint32_t i = 0; i < n; i++)
            ((uint8_t*) p)[i] = (uint8_t) rand();
    }

    void SetRandom(ECC::uintBig& x)
    {
        GenerateRandom(x.m_pData, sizeof(x.m_pData));
    }

    void SetRandom(ECC::Scalar::Native& x)
    {
        ECC::Scalar s;
        while (true)
        {
            SetRandom(s.m_Value);
            if (!x.Import(s))
                break;
        }
    }
}

namespace beam::wallet
{
    void Sender::FSMDefinition::initTx(const msmf::none&)
    {
        // 1. Create transaction Uuid
        m_invitationData->m_txId = m_txId;

        auto coins = m_state.m_keychain->getCoins(m_state.m_amount); // need to lock 
        m_invitationData->m_amount = m_state.m_amount;
        m_state.m_kernel.m_Fee = 0;
        m_state.m_kernel.m_HeightMin = 0;
        m_state.m_kernel.m_HeightMax = -1;
        m_state.m_kernel.get_Hash(m_invitationData->m_message);
        
        // 2. Set lock_height for output (current chain height)
        // 3. Select inputs using desired selection strategy
        {
            m_state.m_blindingExcess = ECC::Zero;
            for (const auto& coin: coins)
            {
                Input::Ptr input = std::make_unique<Input>();
                input->m_Height = 0;
                input->m_Coinbase = false;

                ECC::Scalar::Native key(coin.m_key);
                ECC::Point::Native pt = ECC::Commitment(key, coin.m_amount);

                input->m_Commitment = pt;

                m_invitationData->m_inputs.push_back(std::move(input));
                
                m_state.m_blindingExcess += key;
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

            change -= m_state.m_amount;

            Output::Ptr output = std::make_unique<Output>();
            output->m_Coinbase = false;

            ECC::Scalar::Native blindingFactor;
            SetRandom(blindingFactor);
            ECC::Point::Native pt = ECC::Commitment(blindingFactor, change);
            output->m_Commitment = pt;

            output->m_pPublic.reset(new ECC::RangeProof::Public);
            output->m_pPublic->m_Value = change;
            output->m_pPublic->Create(blindingFactor);
            // TODO: need to store new key and amount in keyChain

            blindingFactor = -blindingFactor;
            m_state.m_blindingExcess += blindingFactor;

            m_invitationData->m_outputs.push_back(std::move(output));
        }
        // 6. calculate tx_weight
        // 7. calculate fee
        // 8. Calculate total blinding excess for all inputs and outputs xS
        // 9. Select random nonce kS
        ECC::Signature::MultiSig msig;
        SetRandom(m_state.m_nonce);

        msig.m_Nonce = m_state.m_nonce;
        // 10. Multiply xS and kS by generator G to create public curve points xSG and kSG
        m_state.m_publicBlindingExcess 
            = m_invitationData->m_publicSenderBlindingExcess
            = ECC::Context::get().G * m_state.m_blindingExcess;
        m_state.m_publicNonce 
            = m_invitationData->m_publicSenderNonce
            = ECC::Context::get().G * m_state.m_nonce;
        // an attempt to implement "stingy" transaction

        m_gateway.sendTxInitiation(m_invitationData);
    }

    bool Sender::FSMDefinition::isValidSignature(const TxInitCompleted& event)
    {
        auto data = event.data;
        // 4. Compute Sender Schnorr signature
        ECC::Signature::MultiSig msig;
        msig.m_Nonce = m_state.m_nonce;
        msig.m_NoncePub = m_state.m_publicNonce + data->m_publicReceiverNonce;
        ECC::Hash::Value message;
        m_state.m_kernel.get_Hash(message);
        m_state.m_kernel.m_Signature.CoSign(m_state.m_senderSignature, message, m_state.m_blindingExcess, msig);
        // 1. Calculate message m

        // 2. Compute Schnorr challenge e
        ECC::Point::Native k;
        k = m_state.m_publicNonce + data->m_publicReceiverNonce;
        ECC::Scalar::Native e = m_state.m_kernel.m_Signature.m_e;
 
        // 3. Verify recepients Schnorr signature 
        ECC::Point::Native s, s2;
        ECC::Scalar::Native ne;
        ne = -e;
        s = data->m_publicReceiverNonce;
        s += data->m_publicReceiverBlindingExcess * ne;

        s2 = ECC::Context::get().G * data->m_receiverSignature;
        ECC::Point p(s), p2(s2);

        return (p.cmp(p2) == 0);
    }

    bool Sender::FSMDefinition::isInvalidSignature(const TxInitCompleted& event)
    {
        return !isValidSignature(event);
    }

    void Sender::FSMDefinition::confirmTx(const TxInitCompleted& event)
    {
        auto data = event.data;
        // 4. Compute Sender Schnorr signature
        auto confirmationData = std::make_shared<sender::ConfirmationData>();
        m_confirmationData->m_txId = m_txId;
        ECC::Signature::MultiSig msig;
        msig.m_Nonce = m_state.m_nonce;
        msig.m_NoncePub = m_state.m_publicNonce + data->m_publicReceiverNonce;
        ECC::Hash::Value message;
        m_state.m_kernel.get_Hash(message);
        m_state.m_kernel.m_Signature.CoSign(m_confirmationData->m_senderSignature, message, m_state.m_blindingExcess, msig);
        m_gateway.sendTxConfirmation(m_confirmationData);
    }
}
