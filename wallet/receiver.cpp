#include "receiver.h"

namespace // TODO: make a separate function for random
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
    void Receiver::FSMDefinition::confirmTx(const msmf::none&)
    {
        m_confirmationData->m_txId = m_receiver.m_txId;

        TxKernel::Ptr kernel = std::make_unique<TxKernel>();
        kernel->m_Fee = 0;
        kernel->m_HeightMin = 0;
        kernel->m_HeightMax = -1;
        m_receiver.m_kernel = kernel.get();
        m_receiver.m_transaction.m_vKernels.push_back(std::move(kernel));
        


        // 1. Check fee
       

        // 2. Create receiver_output
        // 3. Choose random blinding factor for receiver_output
        ECC::Amount amount = m_receiver.m_amount;
        Output::Ptr output = std::make_unique<Output>();
        output->m_Coinbase = false;

        ECC::Scalar::Native blindingFactor;
        SetRandom(blindingFactor);
        ECC::Point::Native pt;
        pt = ECC::Commitment(blindingFactor, amount);
        output->m_Commitment = pt;

        output->m_pPublic.reset(new ECC::RangeProof::Public);
        output->m_pPublic->m_Value = amount;
        output->m_pPublic->Create(blindingFactor);

        blindingFactor = -blindingFactor;
        m_receiver.m_blindingExcess += blindingFactor;

        m_receiver.m_transaction.m_vOutputs.push_back(std::move(output));

        // 4. Calculate message M
        // 5. Choose random nonce
        ECC::Signature::MultiSig msig;
        SetRandom(m_receiver.m_nonce);
        //msig.GenerateNonce(m_state.m_message, m_state.m_blindingExcess);
        //m_state.m_nonce = msig.m_Nonce;
        msig.m_Nonce = m_receiver.m_nonce;
        // 6. Make public nonce and blinding factor
        m_receiver.m_publicReceiverBlindingExcess 
            = m_confirmationData->m_publicReceiverBlindingExcess 
            = ECC::Context::get().G * m_receiver.m_blindingExcess;

        m_confirmationData->m_publicReceiverNonce = ECC::Context::get().G * m_receiver.m_nonce;
        // 7. Compute Shnorr challenge e = H(M|K)

        msig.m_NoncePub = m_receiver.m_publicSenderNonce + m_confirmationData->m_publicReceiverNonce;
        // 8. Compute recepient Shnorr signature
        m_receiver.m_kernel->m_Signature.CoSign(m_receiver.m_receiverSignature, m_receiver.m_message, m_receiver.m_blindingExcess, msig);
        
        m_confirmationData->m_receiverSignature = m_receiver.m_receiverSignature;

        m_gateway.sendTxConfirmation(m_confirmationData);
    }
}