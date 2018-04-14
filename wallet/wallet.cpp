#include "wallet.h"
#include "core/serialization_adapters.h"
#include <boost/uuid/uuid.hpp>
#include "core/ecc_native.h"
#include <algorithm>

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

namespace ECC
{
    Context g_Ctx;
    const Context& Context::get() { return g_Ctx; }
}

namespace beam
{

    Coin::Coin(const ECC::Scalar& key, ECC::Amount amount)
        : m_amount(amount)
    {
        m_key = ECC::Scalar::Native(key);
    }

    // temporary impl of WalletToNetwork interface
    struct WalletToNetworkDummyImpl : public Wallet::ToNode
    {
        virtual void sendTransaction(const Transaction& tx)
        {
            // serealize tx and post and to the Node TX pool
            Serializer ser;
            ser & tx;

            auto buffer = ser.buffer();

            // and send buffer to other side
        }
    };

    Wallet::Wallet(ToWallet::Shared receiver, IKeyChain::Ptr keyChain)
        : m_receiver(receiver)
        , m_keyChain(keyChain)
    {

    }

    Wallet::Wallet()
        : m_net(std::make_unique<WalletToNetworkDummyImpl>())
    {

    }

    void Wallet::sendDummyTransaction()
    {
        // create dummy transaction here
        Transaction tx;
        m_net->sendTransaction(tx);
    }

    Wallet::Result Wallet::sendInvitation(const SendInvitationData& data)
    {
        auto invitationData = m_receiver->handleInvitation(data);

        // 1. Calculate message m
        ECC::Hash::Value message;
        message = 1U;
        // 2. Compute Schnorr challenge e
        ECC::Point::Native k;
        k = data.m_publicSenderNonce + invitationData->m_publicReceiverNonce;
        ECC::Scalar::Native e;
        ECC::Oracle() << message << k >> e;
        // 3. Verify recepients Schnorr signature
        ECC::Point::Native s, s2;
        s = invitationData->m_publicReceiverNonce;
        s += invitationData->m_publicReceiverBlindingExcess * e;

        s2 = ECC::Context::get().G * invitationData->m_receiverSignature;
        ECC::Point p(s), p2(s2);

        if (p.cmp(p2) != 0)
        {
            return false;
        }

        // 4. Compute Sender Schnorr signature
        ECC::Scalar::Native signature;
        signature = m_state.m_blindingExcess;
        signature *= e;

        auto confirmationData = std::make_shared<SendConfirmationData>();
        
        confirmationData->m_senderSignature = m_state.m_nonce + e;

        return sendConfirmation(*confirmationData);
    }

    Wallet::Result Wallet::sendConfirmation(const SendConfirmationData& data)
    {
        HandleConfirmationData::Ptr res = m_receiver->handleConfirmation(data);

        return true;
    }

    Wallet::SendInvitationData::Ptr Wallet::createInvitationData(const ECC::Amount& amount)
    {
        auto coins = m_keyChain->getCoins(amount); // need to lock 
        auto res = std::make_shared<SendInvitationData>();

        // 1. Create transaction Uuid
        boost::uuids::uuid id;
        std::copy(id.begin(), id.end(), res->m_id.begin());
        // 2. Set lock_height for output (current chain height)
        // auto tip = m_node.getChainTip();
        // uint64_t lockHeight = tip.height;
        // 3. Select inputs using desired selection strategy
        {
            m_state.m_transaction.m_vInputs.resize(coins.size());
            m_state.m_blindingExcess = ECC::Zero;
            for (const auto& coin: coins)
            {
                beam::Input::Ptr input{new beam::Input};
                input->m_Height = 0;
                input->m_Coinbase = false;

                ECC::Scalar::Native key(coin.m_key);
                ECC::Point::Native pt;
                pt = ECC::Commitment(key, coin.m_amount);

                input->m_Commitment = pt;

                m_state.m_transaction.m_vInputs.push_back(std::move(input));
                
                m_state.m_blindingExcess += key;
            }
        }
        // 4. Create change_output
        // 5. Select blinding factor for change_output
        // m_transaction.m_vOutputs.push_back(createChangeOutput(coins));
        {
            Amount change = 0;
            for (const auto &coin : coins)
            {
                change += coin.m_amount;
            }

            change -= amount;

            Output::Ptr output = std::make_unique<Output>();
            output->m_Coinbase = false;

            ECC::Scalar::Native blindingFactor;
            SetRandom(blindingFactor);
            ECC::Point::Native pt;
            pt = ECC::Commitment(blindingFactor, change);
            output->m_Commitment = pt;

            output->m_pPublic.reset(new ECC::RangeProof::Public);
            output->m_pPublic->m_Value = change;
            output->m_pPublic->Create(blindingFactor);
            // TODO: need to store new key and amount in keyChain

            blindingFactor = -blindingFactor;
            m_state.m_blindingExcess += blindingFactor;

            m_state.m_transaction.m_vOutputs.push_back(std::move(output));
        }
        // 6. calculate tx_weight
        // 7. calculate fee
        // 8. Calculate total blinding excess for all inputs and outputs xS
        // 9. Select random nonce kS

        SetRandom(m_state.m_nonce);
        // 10. Multiply xS and kS by generator G to create public curve points xSG and kSG
        res->m_publicSenderBlindingExcess = ECC::Context::get().G * m_state.m_blindingExcess;
        res->m_publicSenderNonce = ECC::Context::get().G * m_state.m_nonce;
        // an attempt to implement "stingy" transaction

        return res;
    }

    Wallet::Result Wallet::sendMoneyTo(const Config& config, uint64_t amount)
    {
        auto data = createInvitationData(amount);

        return sendInvitation(*data);
    }

    Wallet::HandleInvitationData::Ptr Wallet::ToWallet::handleInvitation(const SendInvitationData& data)
    {
        auto res = std::make_shared<HandleInvitationData>();
        
        ECC::Hash::Value message;
        message = 1U;
        // res->m_publicBlindingExcess = data.m_publicBlindingExcess;
        // res->m_publicNonce = data.m_publicNonce;

        // 1. Check fee
        // 2. Create receiver_output
        // 3. Choose random blinding factor for receiver_output
        SetRandom(m_state.m_blindingExcess);
        // 4. Calculate message M
        // 5. Choose random nonce
        SetRandom(m_state.m_nonce);
        // 6. Make public nonce and blinding factor
        res->m_publicReceiverBlindingExcess = ECC::Context::get().G * m_state.m_blindingExcess;
        res->m_publicReceiverNonce = ECC::Context::get().G * m_state.m_nonce;
        // 7. Compute Shnorr challenge e = H(M|K)
        ECC::Point::Native k;
        k = data.m_publicSenderNonce + res->m_publicReceiverNonce;

        ECC::Scalar::Native e;

        ECC::Oracle() << message << k >> e;

        // 8. Compute recepient Shnorr signature
        e *= m_state.m_blindingExcess;
        
        res->m_receiverSignature = m_state.m_nonce + e;

        return res;
    }

    Wallet::HandleConfirmationData::Ptr Wallet::ToWallet::handleConfirmation(const SendConfirmationData& data)
    {
        HandleConfirmationData::Ptr res = std::make_shared<HandleConfirmationData>();

        // 1. Verify sender's Schnor signature

        // 2. Calculate final signature
        // 3. Calculate public key for excess
        // 4. Verify excess value in final transaction
        // 5. Create transaction kernel
        // 6. Create final transaction and send it to mempool

        return res;
    }

}
