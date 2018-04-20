#include "sender.h"

namespace beam::wallet
{
    void Sender::FSMDefinition::initTx(const msmf::none&)
    {
        // 1. Create transaction Uuid
        m_invitationData.m_txId = m_txId;

        // {
        //     auto& res = m_invitationData;
        //     auto coins = m_keychain.getCoins(amount); // need to lock 
        //     res->m_amount = amount;
        //     m_state.m_kernel.m_Fee = 0;
        //     m_state.m_kernel.m_HeightMin = 0;
        //     m_state.m_kernel.m_HeightMax = -1;
        //     m_state.m_kernel.get_Hash(res->m_message);
            
        //     // 2. Set lock_height for output (current chain height)
        //     // auto tip = m_node.getChainTip();
        //     // uint64_t lockHeight = tip.height;
        //     // 3. Select inputs using desired selection strategy
        //     {
        //         m_state.m_blindingExcess = ECC::Zero;
        //         for (const auto& coin: coins)
        //         {
        //             Input::Ptr input = std::make_unique<Input>();
        //             input->m_Height = 0;
        //             input->m_Coinbase = false;

        //             ECC::Scalar::Native key(coin.m_key);
        //             ECC::Point::Native pt = ECC::Commitment(key, coin.m_amount);

        //             input->m_Commitment = pt;

        //             res->m_inputs.push_back(std::move(input));
                    
        //             m_state.m_blindingExcess += key;
        //         }
        //     }
        //     // 4. Create change_output
        //     // 5. Select blinding factor for change_output
        //     // m_transaction.m_vOutputs.push_back(createChangeOutput(coins));
        //     {
        //         Amount change = 0;
        //         for (const auto &coin : coins)
        //         {
        //             change += coin.m_amount;
        //         }

        //         change -= amount;

        //         Output::Ptr output = std::make_unique<Output>();
        //         output->m_Coinbase = false;

        //         ECC::Scalar::Native blindingFactor;
        //         SetRandom(blindingFactor);
        //         ECC::Point::Native pt = ECC::Commitment(blindingFactor, change);
        //         output->m_Commitment = pt;

        //         output->m_pPublic.reset(new ECC::RangeProof::Public);
        //         output->m_pPublic->m_Value = change;
        //         output->m_pPublic->Create(blindingFactor);
        //         // TODO: need to store new key and amount in keyChain

        //         blindingFactor = -blindingFactor;
        //         m_state.m_blindingExcess += blindingFactor;

        //         res->m_outputs.push_back(std::move(output));
        //     }
        //     // 6. calculate tx_weight
        //     // 7. calculate fee
        //     // 8. Calculate total blinding excess for all inputs and outputs xS
        //     // 9. Select random nonce kS
        //     ECC::Signature::MultiSig msig;
        //     SetRandom(m_state.m_nonce);
        //     //msig.GenerateNonce(res->m_message, m_state.m_blindingExcess);
        //     //m_state.m_nonce = msig.m_Nonce;
        //     msig.m_Nonce = m_state.m_nonce;
        //     // 10. Multiply xS and kS by generator G to create public curve points xSG and kSG
        //     res->m_publicSenderBlindingExcess = ECC::Context::get().G * m_state.m_blindingExcess;
        //     res->m_publicSenderNonce = ECC::Context::get().G * m_state.m_nonce;
        //     // an attempt to implement "stingy" transaction

        //     return res;
        // }

        m_gateway.sendTxInitiation(m_invitationData);
    }

}
