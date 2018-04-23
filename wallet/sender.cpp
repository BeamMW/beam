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

        auto coins = m_sender.m_keychain->getCoins(m_sender.m_amount); // need to lock 
        m_invitationData->m_amount = m_sender.m_amount;
        m_sender.m_kernel.m_Fee = 0;
        m_sender.m_kernel.m_HeightMin = 0;
        m_sender.m_kernel.m_HeightMax = -1;
        m_sender.m_kernel.get_Hash(m_invitationData->m_message);
        
        // 2. Set lock_height for output (current chain height)
        // 3. Select inputs using desired selection strategy
        {
            m_sender.m_blindingExcess = ECC::Zero;
            for (const auto& coin: coins)
            {
                Input::Ptr input = std::make_unique<Input>();
                input->m_Height = 0;
                input->m_Coinbase = false;

                ECC::Scalar::Native key(coin.m_key);
                ECC::Point::Native pt = ECC::Commitment(key, coin.m_amount);

                input->m_Commitment = pt;

                m_invitationData->m_inputs.push_back(std::move(input));
                
                m_sender.m_blindingExcess += key;
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

            change -= m_sender.m_amount;

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
            m_sender.m_blindingExcess += blindingFactor;

            m_invitationData->m_outputs.push_back(std::move(output));
        }
        // 6. calculate tx_weight
        // 7. calculate fee
        // 8. Calculate total blinding excess for all inputs and outputs xS
        // 9. Select random nonce kS
        ECC::Signature::MultiSig msig;
        SetRandom(m_sender.m_nonce);

        msig.m_Nonce = m_sender.m_nonce;
        // 10. Multiply xS and kS by generator G to create public curve points xSG and kSG
        m_invitationData->m_publicSenderBlindingExcess = ECC::Context::get().G * m_sender.m_blindingExcess;
        m_invitationData->m_publicSenderNonce = ECC::Context::get().G * m_sender.m_nonce;
        // an attempt to implement "stingy" transaction


        m_gateway.sendTxInitiation(std::move(m_invitationData));
    }

}
